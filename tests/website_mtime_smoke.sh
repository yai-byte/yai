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

    WebsiteLinkMeta newer{"file:///a/krita-5.3.2.1-x86_64.AppImage", 200, 0};
    WebsiteLinkMeta older{"file:///b/krita-6.0.2-x86_64.AppImage", 100, 0};
    WebsiteLinkMeta stale{"file:///old/krita-9.0.0-x86_64.AppImage", 300, 1};
    WebsiteLinkMeta stale_arch{"file:///attic/krita-x86_64.AppImage", 50, 1};
    WebsiteLinkMeta fresh_generic{"file:///stable/krita.AppImage", 40, 0};
    require(website_candidate_better(newer, older, "x86_64"), "mtime wins");
    require(website_candidate_better(newer, stale, "x86_64"), "non-stale wins");
    require(website_candidate_better(fresh_generic, stale_arch, "x86_64"),
            "non-stale generic beats stale arch");
    require(
        best_website_appimage_url({stale, older, newer}, "x86_64")
            .find("5.3.2.1") != std::string::npos,
        "best picks newer non-stale");
    require(
        best_website_appimage_url({stale_arch, fresh_generic}, "x86_64")
            .find("krita.AppImage") != std::string::npos,
        "best prefers non-stale generic over stale arch");
    require(
        best_website_appimage_url({stale}, "x86_64").find("9.0.0") != std::string::npos,
        "stale-only fallback");

    require(!probe_url_last_modified_mtime("file:///tmp/listing-driven.AppImage").has_value(),
            "file:// HEAD stays listing-driven");

    // Listing top-N follow prune
    require(website_url_priority("https://x.example/download/foo") == 2, "priority download");
    require(website_url_priority("https://x.example/AppImage/bar") == 3, "priority appimage");
    require(website_url_priority("https://x.example/other") == 0, "priority other");

    WebsiteLinkMeta d1{"file:///download/v1/", 100, 0};
    WebsiteLinkMeta d2{"file:///download/v2/", 200, 0};
    WebsiteLinkMeta d3{"file:///download/v3/", 300, 0};
    WebsiteLinkMeta d4{"file:///download/v4/", 400, 0};
    WebsiteLinkMeta d5{"file:///download/v5/", 500, 0};
    WebsiteLinkMeta no_mt{"file:///download/unknown/", std::nullopt, 0};
    WebsiteLinkMeta attic{"file:///download/older_versions_are_in_the_attic/", 50, 1};
    WebsiteLinkMeta attic2{"file:///download/old/archive/", 40, 1};

    require(website_follow_meta_better(d5, d4), "newer follow better");
    require(website_follow_meta_better(d1, attic), "non-stale follow better");
    require(website_follow_meta_better(d2, no_mt), "known mtime beats unknown");

    require(!website_listing_follow_prune_applies({no_mt, attic}), "no prune without non-stale mtime");
    require(website_listing_follow_prune_applies({d1, no_mt}), "prune applies with one timed non-stale");

    const auto kept = select_listing_follow_metas_for_enqueue(
        {d1, d2, d3, d4, d5, no_mt, attic, attic2}, 3, 1);
    require(kept.size() == 4, "3 non-stale + 1 stale");
    require(kept[0].url.find("v5") != std::string::npos, "first is newest");
    require(kept[1].url.find("v4") != std::string::npos, "second");
    require(kept[2].url.find("v3") != std::string::npos, "third");
    require(kept[3].stale_penalty == 1, "one stale kept");
    require(kept[3].url.find("older_versions") != std::string::npos, "best stale by mtime");
    for (const auto& m : kept) {
        require(m.url.find("unknown") == std::string::npos, "unknown mtime dropped when prune on");
        require(m.url.find("/old/archive") == std::string::npos, "second stale dropped");
    }

    const auto passthrough = select_listing_follow_metas_for_enqueue({no_mt, attic}, 3, 1);
    require(passthrough.size() == 2, "no-prune passthrough keeps all");

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

# --- Integration: Krita-like autoindex crawl prefers newer listing mtime ---
# Path contains "download" so should_follow_download_page accepts version dirs.
# Root also lists an older AppImage file so early-return-on-first-hit picks wrong.
TMP_HOME="$TMP_DIR/home"
ASSETS="$TMP_DIR/assets"
KRITA="$TMP_DIR/download/krita"
mkdir -p "$TMP_HOME" "$ASSETS" \
  "$KRITA/5.3.2.1" \
  "$KRITA/6.0.2" \
  "$KRITA/older_versions_are_in_the_attic"

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

make_appimage "$ASSETS/krita-5.3.2.1-x86_64.AppImage" "krita 5.3.2.1"
make_appimage "$ASSETS/krita-6.0.2-x86_64.AppImage" "krita 6.0.2"
make_appimage "$ASSETS/krita-old-x86_64.AppImage" "krita old"

# Use index.html hrefs: curl file://.../dir/ only returns the filename "index.html".
cat > "$KRITA/index.html" <<HTML
<html><body><table>
<tr><th>Name</th><th>Last modified</th></tr>
<tr><td><a href="file://$ASSETS/krita-6.0.2-x86_64.AppImage">krita-6.0.2-x86_64.AppImage</a></td>
    <td align="right">2026-05-26 14:33  </td></tr>
<tr><td><a href="5.3.2.1/index.html">5.3.2.1/</a></td><td align="right">2026-06-02 09:22  </td></tr>
<tr><td><a href="6.0.2/index.html">6.0.2/</a></td><td align="right">2026-05-26 14:33  </td></tr>
<tr><td><a href="older_versions_are_in_the_attic/index.html">older_versions_are_in_the_attic/</a></td>
    <td align="right">2018-05-24 14:14  </td></tr>
</table></body></html>
HTML

cat > "$KRITA/5.3.2.1/index.html" <<HTML
<html><body><a href="file://$ASSETS/krita-5.3.2.1-x86_64.AppImage">AppImage</a></body></html>
HTML
cat > "$KRITA/6.0.2/index.html" <<HTML
<html><body><a href="file://$ASSETS/krita-6.0.2-x86_64.AppImage">AppImage</a></body></html>
HTML
cat > "$KRITA/older_versions_are_in_the_attic/index.html" <<HTML
<html><body><a href="file://$ASSETS/krita-old-x86_64.AppImage">AppImage</a></body></html>
HTML

mkdir -p "$TMP_HOME/.local/share/yai/repos"
# Package id/name must not contain krita/kdenlive/gimp (avoids network download hints).
cat > "$TMP_HOME/.local/share/yai/repos/index.json" <<JSON
{
  "schema_version": 1,
  "updated_at": "test",
  "packages": [
    {
      "id": "mtime-listing",
      "name": "Mtime Listing",
      "summary": "Krita-like autoindex fixture",
      "homepage": "file://$KRITA/index.html",
      "license": "GPL",
      "source": {
        "type": "website_page",
        "url": "file://$KRITA/index.html",
        "reason": "test"
      }
    },
    {
      "id": "mtime-stale-only",
      "name": "Mtime Stale Only",
      "summary": "Attic-only fixture",
      "homepage": "file://$KRITA/older_versions_are_in_the_attic/index.html",
      "license": "GPL",
      "source": {
        "type": "website_page",
        "url": "file://$KRITA/older_versions_are_in_the_attic/index.html",
        "reason": "test"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" "$ROOT/yai" install mtime-listing 2>"$TMP_DIR/mtime_listing.err"
META="$TMP_HOME/.local/share/yai/apps/mtime-listing/metadata.json"
grep -Fq '5.3.2.1' "$META" || {
  echo "expected download_url to prefer 5.3.2.1, metadata:" >&2
  cat "$META" >&2
  echo "stderr:" >&2
  cat "$TMP_DIR/mtime_listing.err" >&2
  exit 1
}
grep -E 'download_url.*.*5\.3\.2\.1' "$META" >/dev/null
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/mtime-listing" | grep -q "krita 5.3.2.1"
HOME="$TMP_HOME" "$ROOT/yai" remove mtime-listing

HOME="$TMP_HOME" "$ROOT/yai" install mtime-stale-only 2>"$TMP_DIR/mtime_stale.err"
test -x "$TMP_HOME/.local/share/yai/apps/mtime-stale-only/current.AppImage"
grep -Fq 'krita-old-x86_64.AppImage' \
  "$TMP_HOME/.local/share/yai/apps/mtime-stale-only/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/mtime-stale-only" | grep -q "krita old"
HOME="$TMP_HOME" "$ROOT/yai" remove mtime-stale-only

echo "website mtime integration smoke passed"
