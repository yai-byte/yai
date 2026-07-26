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
