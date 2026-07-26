#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

cat > "$TMP_DIR/mtime_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

int main() {
    require(website_url_looks_stale("https://download.kde.org/stable/krita/older_versions_are_in_the_attic"),
            "older_versions");
    require(website_url_looks_stale("https://download.kde.org/Attic/krita/"), "Attic");
    require(website_url_looks_stale("https://example.invalid/old/pkg.AppImage"), "segment old");
    require(!website_url_looks_stale("https://example.invalid/download/pkg.AppImage"), "download false positive");
    require(!website_url_looks_stale("https://example.invalid/threshold/pkg.AppImage"), "threshold false positive");
    require(website_link_stale_penalty("https://x/older/y") == 1, "penalty");
    require(website_link_stale_penalty("https://x/y") == 0, "no penalty");

    const auto a = parse_directory_listing_mtime("2026-06-02 09:22");
    const auto b = parse_directory_listing_mtime("2026-05-26 14:33");
    require(a.has_value() && b.has_value() && *a > *b, "listing mtime order");
    require(!parse_directory_listing_mtime("not-a-date").has_value(), "bad listing mtime");

    std::cout << "website mtime unit smoke passed\n";
    return 0;
}
CPP

# Link the same resolver/core pieces other smokes need once symbols resolve.
# Start minimal; add sources until link succeeds.
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/mtime_unit" \
  "$TMP_DIR/mtime_unit.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  -lpthread

"$TMP_DIR/mtime_unit"
