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

    const std::string listing =
        "<html><body><table>"
        "<tr><th><a href=\"?C=N;O=D\">Name</a></th>"
        "<th><a href=\"?C=M;O=A\">Last modified</a></th></tr>"
        "<tr><td><a href=\"/stable/\">Parent Directory</a></td><td></td></tr>"
        "<tr><td><a href=\"5.3.2.1/\">5.3.2.1/</a></td>"
        "<td align=\"right\">2026-06-02 09:22  </td></tr>"
        "<tr><td><a href=\"6.0.2/\">6.0.2/</a></td>"
        "<td align=\"right\">2026-05-26 14:33  </td></tr>"
        "<tr><td><a href=\"older_versions_are_in_the_attic\">older</a></td>"
        "<td align=\"right\">2018-05-24 14:14  </td></tr>"
        "</table></body></html>";
    require(html_looks_like_directory_listing(listing), "detect listing");
    const auto links = html_directory_listing_links(
        listing, "file:///tmp/krita/");
    require(links.size() == 3, "three data rows");
    // Find 5.3.2.1 and 6.0.2 metas
    const WebsiteLinkMeta* v53 = nullptr;
    const WebsiteLinkMeta* v60 = nullptr;
    const WebsiteLinkMeta* old = nullptr;
    for (const auto& link : links) {
        if (link.url.find("5.3.2.1") != std::string::npos) v53 = &link;
        if (link.url.find("6.0.2/") != std::string::npos &&
            link.url.find("6.0.2.1") == std::string::npos) v60 = &link;
        if (link.url.find("older_versions") != std::string::npos) old = &link;
    }
    require(v53 && v60 && old, "rows present");
    require(v53->mtime.has_value() && v60->mtime.has_value() && *v53->mtime > *v60->mtime,
            "mtime newer for 5.3.2.1");
    require(old->stale_penalty == 1, "attic/older penalty");
    require(v53->stale_penalty == 0, "version dir not stale");

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
