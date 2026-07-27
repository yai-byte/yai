#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
export HOME="$TMP_DIR/home"
mkdir -p "$HOME"

cat > "$TMP_DIR/cache_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

int main() {
    const std::string id = "cache-pkg";
    const std::string arch = "x86_64";
    const std::string src = "file:///tmp/site/index.html#frag";
    const std::string key = website_resolve_cache_key(id, arch, src);
    require(key.find("cache-pkg") != std::string::npos, "key has id");
    require(key.find("#frag") == std::string::npos, "key strips fragment");

    require(load_website_resolve_cache().empty(), "empty when missing");

    WebsiteResolveCacheEntry e;
    e.package_id = id;
    e.arch = arch;
    e.source_url = strip_url_fragment_query(src);
    e.download_url = "file:///tmp/missing.AppImage";
    e.resolved_at = 1;
    upsert_website_resolve_cache_entry(e);

    const auto loaded = load_website_resolve_cache();
    require(loaded.size() == 1, "one entry");
    const auto found = find_website_resolve_cache_entry(loaded, id, arch, src);
    require(found.has_value(), "find by key");
    require(found->download_url.find("missing.AppImage") != std::string::npos, "url stored");

    require(website_resolve_cache_entry_expired(*found, found->resolved_at + kWebsiteResolveCacheTtlSeconds + 1),
            "expired after ttl");
    require(!website_resolve_cache_entry_expired(*found, found->resolved_at + 10), "fresh inside ttl");

    require(!website_cached_download_url_usable(found->download_url, {}), "missing file unusable");

    // Create a tiny AppImage path and accept it
    // (write file in /tmp via test harness before this binary — see shell below)
    require(website_cached_download_url_usable(
                "file://" + std::string(std::getenv("YAI_CACHE_TEST_APPIMAGE")), {}),
            "existing file usable");

    std::cout << "website resolve cache unit smoke passed\n";
    return 0;
}
CPP

APP="$TMP_DIR/ok.AppImage"
printf '#!/bin/sh\necho ok\n' > "$APP"
chmod +x "$APP"
export YAI_CACHE_TEST_APPIMAGE="$APP"

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/cache_unit" \
  "$TMP_DIR/cache_unit.cpp" \
  "$ROOT/src/website_resolve_cache.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  -lpthread

"$TMP_DIR/cache_unit"

# --- Integration: install uses disk cache ---
make -C "$ROOT" -j"$(nproc)"

TMP_HOME="$TMP_DIR/home"
rm -f "$TMP_HOME/.local/share/yai/website-resolve-cache.json"
ASSETS="$TMP_DIR/assets"
CACHE_SITE="$TMP_DIR/download/cache-site"
mkdir -p "$ASSETS" "$CACHE_SITE" "$TMP_HOME/.local/share/yai/repos"

make_appimage() {
  local path="$1"
  local msg="$2"
  cat > "$path" <<APP
#!/usr/bin/env bash
if [[ "\${1:-}" == "--appimage-version" ]]; then
  echo "$msg"
  exit 0
fi
echo "$msg"
APP
  chmod +x "$path"
}

make_appimage "$ASSETS/cache-app-x86_64.AppImage" "cache app v1"

cat > "$CACHE_SITE/index.html" <<HTML
<html><body><a href="file://$ASSETS/cache-app-x86_64.AppImage">AppImage</a></body></html>
HTML

cat > "$TMP_HOME/.local/share/yai/repos/index.json" <<JSON
{
  "schema_version": 1,
  "updated_at": "test",
  "packages": [
    {
      "id": "cache-site-pkg",
      "name": "Cache Site Pkg",
      "summary": "website_page cache fixture",
      "homepage": "file://$CACHE_SITE/index.html",
      "license": "GPL",
      "source": {
        "type": "website_page",
        "url": "file://$CACHE_SITE/index.html",
        "reason": "test"
      }
    }
  ]
}
JSON

REAL_CURL="$(command -v curl)"
CACHE_CURL_LOG="$TMP_DIR/cache-curl.log"
FAKE_BIN="$TMP_DIR/fake-bin"
mkdir -p "$FAKE_BIN"
cat > "$FAKE_BIN/curl" <<SH
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$CACHE_CURL_LOG"
exec "$REAL_CURL" "\$@"
SH
chmod +x "$FAKE_BIN/curl"

: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" install cache-site-pkg 2>"$TMP_DIR/first.err"
META="$TMP_HOME/.local/share/yai/apps/cache-site-pkg/metadata.json"
grep -Fq 'cache-app-x86_64.AppImage' "$META" || {
  echo "first install: expected AppImage in metadata:" >&2
  cat "$META" >&2
  exit 1
}
grep -q 'cache-site/index.html' "$CACHE_CURL_LOG" || grep -q 'website search selected' "$TMP_DIR/first.err" || {
  echo "first install: expected listing crawl" >&2
  cat "$CACHE_CURL_LOG" >&2
  cat "$TMP_DIR/first.err" >&2
  exit 1
}
grep -Fq "file://$CACHE_SITE/index.html" \
  "$TMP_HOME/.local/share/yai/website-resolve-cache.json" || {
  echo "cache must key on listing source_url, not AppImage URL" >&2
  cat "$TMP_HOME/.local/share/yai/website-resolve-cache.json" >&2
  exit 1
}
FIRST_DL="$(grep -o '"download_url"[[:space:]]*:[[:space:]]*"[^"]*"' "$META" | head -1)"

HOME="$TMP_HOME" "$ROOT/yai" remove cache-site-pkg

: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" install cache-site-pkg 2>"$TMP_DIR/second.err"
if grep -q 'cache-site/index.html' "$CACHE_CURL_LOG"; then
  echo "second install must not fetch listing index.html" >&2
  cat "$CACHE_CURL_LOG" >&2
  exit 1
fi
grep -Fq 'cache-app-x86_64.AppImage' "$META" || {
  echo "second install: expected AppImage in metadata:" >&2
  cat "$META" >&2
  exit 1
}
SECOND_DL="$(grep -o '"download_url"[[:space:]]*:[[:space:]]*"[^"]*"' "$META" | head -1)"
if [[ "$FIRST_DL" != "$SECOND_DL" ]]; then
  echo "second install download_url must match first: $FIRST_DL vs $SECOND_DL" >&2
  exit 1
fi
test -x "$TMP_HOME/.local/share/yai/apps/cache-site-pkg/current.AppImage"

HOME="$TMP_HOME" "$ROOT/yai" remove cache-site-pkg

python3 - "$TMP_HOME/.local/share/yai/website-resolve-cache.json" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, encoding="utf-8") as handle:
    entries = json.load(handle)
for entry in entries:
    if entry.get("package_id") == "cache-site-pkg":
        entry["download_url"] = "file:///tmp/yai-cache-poison-missing.AppImage"
        break
else:
    raise SystemExit("cache-site-pkg entry missing for poison test")
with open(path, "w", encoding="utf-8") as handle:
    json.dump(entries, handle, indent=2)
    handle.write("\n")
PY

: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" install cache-site-pkg 2>"$TMP_DIR/poison.err"
grep -Fq 'cache-app-x86_64.AppImage' "$META" || {
  echo "poisoned cache: install must recover via crawl" >&2
  cat "$META" >&2
  cat "$TMP_DIR/poison.err" >&2
  exit 1
}
grep -q 'cache-site/index.html' "$CACHE_CURL_LOG" || grep -q 'website search selected' "$TMP_DIR/poison.err" || {
  echo "poisoned cache: expected listing crawl recovery" >&2
  cat "$CACHE_CURL_LOG" >&2
  cat "$TMP_DIR/poison.err" >&2
  exit 1
}
HOME="$TMP_HOME" "$ROOT/yai" remove cache-site-pkg

# --- Update: re-resolve discovers newer AppImage; upsert refreshes disk cache ---
: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" install cache-site-pkg 2>"$TMP_DIR/update-base.err"
META="$TMP_HOME/.local/share/yai/apps/cache-site-pkg/metadata.json"

make_appimage "$ASSETS/cache-app-v2-x86_64.AppImage" "cache app v2"
cat > "$CACHE_SITE/index.html" <<HTML
<html><body><a href="file://$ASSETS/cache-app-v2-x86_64.AppImage">AppImage</a></body></html>
HTML

: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" update cache-site-pkg >"$TMP_DIR/update-out.txt" 2>"$TMP_DIR/update.err"
grep -q $'cache-site-pkg\t.*\tupgradable' "$TMP_DIR/update-out.txt" || {
  echo "update: expected upgradable to v2" >&2
  cat "$TMP_DIR/update-out.txt" >&2
  cat "$TMP_DIR/update.err" >&2
  exit 1
}

: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" upgrade cache-site-pkg --yes >"$TMP_DIR/upgrade-v2-out.txt" 2>"$TMP_DIR/upgrade-v2.err"
grep -Fq 'cache-app-v2-x86_64.AppImage' "$META" || {
  echo "upgrade: expected v2 AppImage in metadata" >&2
  cat "$META" >&2
  cat "$TMP_DIR/upgrade-v2.err" >&2
  exit 1
}
grep -q 'cache-site/index.html' "$CACHE_CURL_LOG" || grep -q 'website search selected' "$TMP_DIR/upgrade-v2.err" || {
  echo "upgrade: expected listing crawl" >&2
  cat "$CACHE_CURL_LOG" >&2
  cat "$TMP_DIR/upgrade-v2.err" >&2
  exit 1
}
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/cache-site-pkg" | grep -q "cache app v2"

# Cache should now point at v2 for a subsequent install after remove.
HOME="$TMP_HOME" "$ROOT/yai" remove cache-site-pkg
: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" install cache-site-pkg 2>"$TMP_DIR/reinstall-v2.err"
grep -Fq 'cache-app-v2-x86_64.AppImage' "$META" || {
  echo "reinstall after upgrade: expected cached v2" >&2
  cat "$META" >&2
  exit 1
}
if grep -q 'cache-site/index.html' "$CACHE_CURL_LOG"; then
  echo "reinstall after upgrade: listing crawl should be skipped via disk cache" >&2
  cat "$CACHE_CURL_LOG" >&2
  exit 1
fi
HOME="$TMP_HOME" "$ROOT/yai" remove cache-site-pkg

# --- TTL miss: expired entry must crawl even if AppImage still exists ---
: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" install cache-site-pkg 2>/dev/null
HOME="$TMP_HOME" "$ROOT/yai" remove cache-site-pkg
python3 - "$TMP_HOME/.local/share/yai/website-resolve-cache.json" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, encoding="utf-8") as handle:
    entries = json.load(handle)
for entry in entries:
    if entry.get("package_id") == "cache-site-pkg":
        entry["resolved_at"] = 1
        break
else:
    raise SystemExit("cache-site-pkg entry missing for TTL test")
with open(path, "w", encoding="utf-8") as handle:
    json.dump(entries, handle, indent=2)
    handle.write("\n")
PY
: > "$CACHE_CURL_LOG"
PATH="$FAKE_BIN:$PATH" \
  HOME="$TMP_HOME" "$ROOT/yai" install cache-site-pkg 2>"$TMP_DIR/ttl.err"
grep -q 'cache-site/index.html' "$CACHE_CURL_LOG" || grep -q 'website search selected' "$TMP_DIR/ttl.err" || {
  echo "TTL expired: expected listing crawl" >&2
  cat "$CACHE_CURL_LOG" >&2
  cat "$TMP_DIR/ttl.err" >&2
  exit 1
}
python3 - "$TMP_HOME/.local/share/yai/website-resolve-cache.json" <<'PY'
import json
import sys
import time

path = sys.argv[1]
with open(path, encoding="utf-8") as handle:
    entries = json.load(handle)
for entry in entries:
    if entry.get("package_id") == "cache-site-pkg":
        if int(entry.get("resolved_at", 0)) < int(time.time()) - 60:
            raise SystemExit(f"resolved_at not refreshed: {entry.get('resolved_at')}")
        break
else:
    raise SystemExit("cache-site-pkg entry missing after TTL reinstall")
PY
HOME="$TMP_HOME" "$ROOT/yai" remove cache-site-pkg

echo "website resolve cache update integration smoke passed"
echo "website resolve cache smoke passed"
