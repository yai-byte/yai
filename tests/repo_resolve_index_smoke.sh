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
