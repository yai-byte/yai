#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
export HOME="$TMP_DIR/home"
mkdir -p "$HOME"

cat > "$TMP_DIR/url_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

int main() {
    RepoPackage p;
    p.id = "pkg";
    p.name = "Pkg";
    p.source_type = "direct_url";
    p.source_url = "https://example/a.AppImage";

    require(!repo_package_has_download_url_for_arch(p, "x86_64"), "empty");
    repo_package_set_download_url(p, "x86_64", "https://example/x.AppImage", false, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/x.AppImage", "single");
    require(p.download_url == "https://example/x.AppImage", "mirrored");
    require(!p.resolved_at.empty(), "timestamp");

    // no overwrite
    repo_package_set_download_url(p, "x86_64", "https://example/other.AppImage", false, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/x.AppImage", "kept");

    // overwrite
    repo_package_set_download_url(p, "x86_64", "https://example/other.AppImage", true, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/other.AppImage", "overwritten");

    // multi-arch: missing arch must not fall back to download_url when map exists
    repo_package_set_download_url(p, "aarch64", "https://example/a.AppImage", true, false);
    require(!repo_package_download_url_for_arch(p, "armv7").has_value(), "no wrong-arch fallback");

    // serialize round-trip via parse_repo_package
    const std::string obj = serialize_repo_package(p);
    require(obj.find("download_urls") != std::string::npos, "has map");
    const RepoPackage again = parse_repo_package(obj);
    require(repo_package_download_url_for_arch(again, "aarch64").value_or("").find("a.AppImage") != std::string::npos, "roundtrip");

    // single-field compatibility: only download_url
    RepoPackage single;
    single.id = "s";
    single.name = "s";
    single.source_type = "direct_url";
    single.source_url = "https://example/s.AppImage";
    single.download_url = "https://example/only.AppImage";
    require(repo_package_download_url_for_arch(single, "x86_64").value_or("") == "https://example/only.AppImage", "compat");

    // no-overwrite must respect single-field download_url (read-priority semantics)
    repo_package_set_download_url(single, "x86_64", "https://example/new.AppImage", false, true);
    require(single.download_url == "https://example/only.AppImage", "compat no-overwrite download_url");
    require(single.download_urls.empty(), "compat no-overwrite map stays empty");
    require(repo_package_download_url_for_arch(single, "x86_64").value_or("") == "https://example/only.AppImage",
            "compat no-overwrite read");

    std::cout << "repo index url unit smoke passed\n";
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/url_unit" \
  "$TMP_DIR/url_unit.cpp" \
  "$ROOT/src/repo_index_urls.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/terminal_color.cpp" \
  "$ROOT/src/website_resolve_cache.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  -pthread

"$TMP_DIR/url_unit"

# --- Task 2: persist + merge across repo update ---

cat > "$TMP_DIR/persist_merge_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

static std::string minimal_index_json(const RepoPackage& package) {
    return std::string(
               "{\n"
               "  \"schema_version\": 1,\n"
               "  \"updated_at\": \"2026-01-01T00:00:00Z\",\n"
               "  \"packages\": [\n") +
           serialize_repo_package(package) +
           "\n  ]\n"
           "}\n";
}

int main() {
    require(repo_index_is_locally_writable(), "writable default");

    RepoPackage base;
    base.id = "demo";
    base.name = "Demo";
    base.source_type = "direct_url";
    base.source_url = "https://example/demo.AppImage";

    ensure_directory(repos_dir_path());
    write_text_file(named_repo_index_path("local"), minimal_index_json(base));
    write_text_file(repos_dir_path() / "index.json", minimal_index_json(base));
    write_repo_entries({RepoEntry{"local", "/tmp/local-feed.json"}});

    RepoPackage enriched = base;
    repo_package_set_download_url(
        enriched, "x86_64", "https://example/demo-x86_64.AppImage", false, true);
    upsert_repo_package_download_urls(enriched);

    const std::vector<RepoPackage> loaded = load_repo_packages();
    require(loaded.size() == 1, "one package");
    require(
        repo_package_download_url_for_arch(loaded[0], "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "upsert persisted url");
    require(!loaded[0].resolved_at.empty(), "upsert persisted resolved_at");

    const std::string named_after = read_text_file(named_repo_index_path("local"));
    require(named_after.find("download_urls") != std::string::npos, "named cache enriched");

    RepoPackage incoming = base;
    const RepoPackage previous = loaded[0];
    const RepoPackage merged = merge_repo_package_download_url_fields(incoming, previous);
    require(
        repo_package_download_url_for_arch(merged, "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "merge keeps url");
    require(merged.resolved_at == previous.resolved_at, "merge keeps resolved_at");

    write_text_file(named_repo_index_path("local"), minimal_index_json(merged));
    rebuild_repo_index_from_cached_files({RepoEntry{"local", "/tmp/local-feed.json"}});

    const std::vector<RepoPackage> after_rebuild = load_repo_packages();
    require(after_rebuild.size() == 1, "rebuild one package");
    require(
        repo_package_download_url_for_arch(after_rebuild[0], "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "rebuild keeps url");

    // Simulate store_repo_index_updates / repo_add merge via shared helper
    write_text_file(named_repo_index_path("local"), minimal_index_json(previous));
    const std::string upstream = minimal_index_json(base);
    const fs::path named = named_repo_index_path("local");
    require(fs::exists(named), "named exists before update");
    const RepoEntry local_entry{"local", "/tmp/local-feed.json"};
    write_text_file(named, merge_named_repo_index_text(local_entry, upstream));
    rebuild_repo_index_from_cached_files({local_entry});
    const std::vector<RepoPackage> after_update = load_repo_packages();
    require(
        repo_package_download_url_for_arch(after_update[0], "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "update merge keeps url");

    // Re-add: named cache still enriched; fresh upstream has no URL fields
    write_text_file(named, minimal_index_json(previous));
    write_text_file(named, merge_named_repo_index_text(local_entry, upstream));
    rebuild_repo_index_from_cached_files({local_entry});
    const std::vector<RepoPackage> after_readd = load_repo_packages();
    require(
        repo_package_download_url_for_arch(after_readd[0], "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "re-add merge keeps url");
    require(read_text_file(named).find("download_urls") != std::string::npos, "re-add named keeps urls");

    std::cout << "repo index persist merge smoke passed\n";
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/persist_merge_unit" \
  "$TMP_DIR/persist_merge_unit.cpp" \
  "$ROOT/src/repo_index_urls.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/terminal_color.cpp" \
  "$ROOT/src/website_resolve_cache.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  -pthread

"$TMP_DIR/persist_merge_unit"

# --- Task 3: prefer index URL, --recrawl, write-back, fallback ---

make -C "$ROOT" -j"$(nproc)" >/dev/null

TASK3_HOME="$TMP_DIR/task3-home"
TASK3_ASSETS="$TMP_DIR/task3-assets"
TASK3_SITE="$TMP_DIR/task3-site"
TASK3_WORKDIR="$TMP_DIR/task3-workdir"
mkdir -p "$TASK3_HOME/.local/share/yai/repos" "$TASK3_ASSETS" "$TASK3_SITE" "$TASK3_WORKDIR"

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

make_appimage "$TASK3_ASSETS/prefer-v1-x86_64.AppImage" "prefer v1"
make_appimage "$TASK3_ASSETS/prefer-v2-x86_64.AppImage" "prefer v2"

cat > "$TASK3_SITE/index.html" <<HTML
<html><body><a href="file://$TASK3_ASSETS/prefer-v1-x86_64.AppImage">AppImage</a></body></html>
HTML

INDEX_JSON="$TASK3_HOME/.local/share/yai/repos/index.json"
cat > "$INDEX_JSON" <<JSON
{
  "schema_version": 1,
  "updated_at": "test",
  "packages": [
    {
      "id": "prefer-site-pkg",
      "name": "Prefer Site Pkg",
      "summary": "website_page prefer-index fixture",
      "homepage": "file://$TASK3_SITE/index.html",
      "license": "GPL",
      "source": {
        "type": "website_page",
        "url": "file://$TASK3_SITE/index.html",
        "reason": "test"
      }
    }
  ]
}
JSON

# Unknown option still rejected
if HOME="$TASK3_HOME" "$ROOT/yai" download prefer-site-pkg --not-a-real-flag 2>"$TMP_DIR/task3-unknown.err"; then
  echo "expected unknown download option to fail" >&2
  exit 1
fi
grep -qi 'unknown' "$TMP_DIR/task3-unknown.err" || {
  echo "expected unknown-option error text:" >&2
  cat "$TMP_DIR/task3-unknown.err" >&2
  exit 1
}

# 1) First download (no index URL) crawls + write-back
rm -f "$TASK3_WORKDIR/prefer-site-pkg.AppImage"
(
  cd "$TASK3_WORKDIR"
  HOME="$TASK3_HOME" "$ROOT/yai" download prefer-site-pkg >"$TMP_DIR/task3-dl1.out" 2>"$TMP_DIR/task3-dl1.err"
) || {
  echo "first download failed:" >&2
  cat "$TMP_DIR/task3-dl1.out" "$TMP_DIR/task3-dl1.err" >&2
  exit 1
}
test -f "$TASK3_WORKDIR/prefer-site-pkg.AppImage"
bash "$TASK3_WORKDIR/prefer-site-pkg.AppImage" | grep -q "prefer v1"
grep -Fq "prefer-v1-x86_64.AppImage" "$INDEX_JSON" || {
  echo "first download must write-back download_url into index:" >&2
  cat "$INDEX_JSON" >&2
  exit 1
}
INDEX_URL_V1="$(python3 - "$INDEX_JSON" <<'PY'
import json, sys
pkg = json.load(open(sys.argv[1]))["packages"][0]
print(pkg.get("download_url") or next(iter(pkg.get("download_urls", {}).values()), ""))
PY
)"
[[ "$INDEX_URL_V1" == *"prefer-v1-x86_64.AppImage"* ]] || {
  echo "unexpected write-back URL: $INDEX_URL_V1" >&2
  exit 1
}

# 2) Change HTML to v2; clear website cache; without --recrawl keep index URL
make_appimage "$TASK3_ASSETS/prefer-v2-x86_64.AppImage" "prefer v2"
cat > "$TASK3_SITE/index.html" <<HTML
<html><body><a href="file://$TASK3_ASSETS/prefer-v2-x86_64.AppImage">AppImage</a></body></html>
HTML
rm -f "$TASK3_HOME/.local/share/yai/website-resolve-cache.json"
rm -f "$TASK3_WORKDIR/prefer-site-pkg.AppImage"
(
  cd "$TASK3_WORKDIR"
  HOME="$TASK3_HOME" "$ROOT/yai" download prefer-site-pkg >"$TMP_DIR/task3-dl2.out" 2>"$TMP_DIR/task3-dl2.err"
) || {
  echo "second download failed:" >&2
  cat "$TMP_DIR/task3-dl2.out" "$TMP_DIR/task3-dl2.err" >&2
  exit 1
}
bash "$TASK3_WORKDIR/prefer-site-pkg.AppImage" | grep -q "prefer v1" || {
  echo "second download without --recrawl must prefer index v1 URL" >&2
  cat "$TMP_DIR/task3-dl2.out" "$TMP_DIR/task3-dl2.err" >&2
  exit 1
}
INDEX_AFTER_2="$(python3 - "$INDEX_JSON" <<'PY'
import json, sys
pkg = json.load(open(sys.argv[1]))["packages"][0]
print(pkg.get("download_url") or next(iter(pkg.get("download_urls", {}).values()), ""))
PY
)"
[[ "$INDEX_AFTER_2" == "$INDEX_URL_V1" ]] || {
  echo "index URL must stay unchanged without --recrawl: $INDEX_AFTER_2" >&2
  exit 1
}

# 3) --recrawl uses crawl (v2); index URL still unchanged
rm -f "$TASK3_HOME/.local/share/yai/website-resolve-cache.json"
rm -f "$TASK3_WORKDIR/prefer-site-pkg.AppImage"
(
  cd "$TASK3_WORKDIR"
  HOME="$TASK3_HOME" "$ROOT/yai" download prefer-site-pkg --recrawl \
    >"$TMP_DIR/task3-dl3.out" 2>"$TMP_DIR/task3-dl3.err"
) || {
  echo "--recrawl download failed:" >&2
  cat "$TMP_DIR/task3-dl3.out" "$TMP_DIR/task3-dl3.err" >&2
  exit 1
}
bash "$TASK3_WORKDIR/prefer-site-pkg.AppImage" | grep -q "prefer v2" || {
  echo "--recrawl must crawl to v2 AppImage" >&2
  cat "$TMP_DIR/task3-dl3.out" "$TMP_DIR/task3-dl3.err" >&2
  exit 1
}
INDEX_AFTER_3="$(python3 - "$INDEX_JSON" <<'PY'
import json, sys
pkg = json.load(open(sys.argv[1]))["packages"][0]
print(pkg.get("download_url") or next(iter(pkg.get("download_urls", {}).values()), ""))
PY
)"
[[ "$INDEX_AFTER_3" == "$INDEX_URL_V1" ]] || {
  echo "--recrawl must not overwrite existing index URL: $INDEX_AFTER_3" >&2
  exit 1
}

# 4) Broken index URL falls back to website_page; index URL unchanged
python3 - "$INDEX_JSON" <<'PY'
import json, sys
path = sys.argv[1]
data = json.load(open(path))
pkg = data["packages"][0]
broken = "file:///missing-prefer-site.AppImage"
pkg["download_url"] = broken
pkg["download_urls"] = {"x86_64": broken}
json.dump(data, open(path, "w"), indent=2)
open(path, "a").write("\n")
PY
rm -f "$TASK3_HOME/.local/share/yai/website-resolve-cache.json"
rm -f "$TASK3_WORKDIR/prefer-site-pkg.AppImage"
(
  cd "$TASK3_WORKDIR"
  HOME="$TASK3_HOME" "$ROOT/yai" download prefer-site-pkg \
    >"$TMP_DIR/task3-dl4.out" 2>"$TMP_DIR/task3-dl4.err"
) || {
  echo "fallback download failed:" >&2
  cat "$TMP_DIR/task3-dl4.out" "$TMP_DIR/task3-dl4.err" >&2
  exit 1
}
bash "$TASK3_WORKDIR/prefer-site-pkg.AppImage" | grep -q "prefer v2" || {
  echo "broken index URL must fall back to crawl (v2)" >&2
  cat "$TMP_DIR/task3-dl4.out" "$TMP_DIR/task3-dl4.err" >&2
  exit 1
}
grep -Fq "file:///missing-prefer-site.AppImage" "$INDEX_JSON" || {
  echo "fallback must leave broken index URL unchanged:" >&2
  cat "$INDEX_JSON" >&2
  exit 1
}

echo "repo index prefer/recrawl/fallback smoke passed"
