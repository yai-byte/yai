#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
API_ROOT="$TMP_HOME/api"
ORIGINAL_ROOT="$TMP_HOME/original"
FEED="$TMP_HOME/feed.json"
LATEST="$API_ROOT/repos/acme/feedapp/releases/latest"
ASSET="FeedApp-x86_64.AppImage"
DIRECT_ASSET="DirectFeed.AppImage"
WEBSITE_ASSET="WebsiteFeed-x86_64.AppImage"
REDDIT_ASSET="RedditWrong-x86_64.AppImage"
MUSESCORE_ASSET="MuseScore-x86_64.AppImage"
MUSESCORE_V2_ASSET="MuseScore-v2-x86_64.AppImage"
WEBSITE_PAGE="$TMP_HOME/website.html"
DOWNLOAD_PAGE="$TMP_HOME/downloads.html"
REDDIT_PAGE="$TMP_HOME/reddit.com/downloads.html"
MUSESCORE_CATALOG_PAGE="$TMP_HOME/appimage.github.io/MuseScore/index.html"
MUSESCORE_HOME_PAGE="$TMP_HOME/musescore.org/index.html"
MUSESCORE_DOWNLOAD_PAGE="$TMP_HOME/musescore.org/downloads.html"
MUSESCORE_LANDING_PAGE="$TMP_HOME/musescore.org/en/download/musescore-x86_64.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

mkdir -p "$(dirname "$LATEST")" "$ORIGINAL_ROOT"

cat > "$ORIGINAL_ROOT/$ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "feed app 1.0.0"
  exit 0
fi
echo "feed app"
APP
chmod +x "$ORIGINAL_ROOT/$ASSET"

cat > "$ORIGINAL_ROOT/$DIRECT_ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "direct feed 1.0.0"
  exit 0
fi
echo "direct feed app"
APP
chmod +x "$ORIGINAL_ROOT/$DIRECT_ASSET"

cat > "$ORIGINAL_ROOT/$WEBSITE_ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "website feed 1.0.0"
  exit 0
fi
echo "website feed app"
APP
chmod +x "$ORIGINAL_ROOT/$WEBSITE_ASSET"

cat > "$ORIGINAL_ROOT/$REDDIT_ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "reddit wrong 1.0.0"
  exit 0
fi
echo "reddit wrong app"
APP
chmod +x "$ORIGINAL_ROOT/$REDDIT_ASSET"

cat > "$ORIGINAL_ROOT/$MUSESCORE_ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "musescore 1.0.0"
  exit 0
fi
echo "musescore app"
APP
chmod +x "$ORIGINAL_ROOT/$MUSESCORE_ASSET"

cat > "$ORIGINAL_ROOT/$MUSESCORE_V2_ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "musescore 2.0.0"
  exit 0
fi
echo "musescore v2 app"
APP
chmod +x "$ORIGINAL_ROOT/$MUSESCORE_V2_ASSET"

cat > "$WEBSITE_PAGE" <<'HTML'
<!doctype html>
<html><body>
<a href="{url}">Template placeholder</a>
<a href="https://appimage.github.io/help">Help</a>
<a href="https://appimage.github.io/categories/Qt">Qt category</a>
<a href="https://kdenlive.org/en/news/">Official news noise</a>
<a href="http://github.com/AppImage/download">Generic AppImage GitHub noise</a>
<a href="https://subsurface-divelog.org/downloads/smtk2ssrf-4.7.8-x86_64.AppImage">Unrelated project AppImage noise</a>
<a href="reddit.com/downloads.html">Reddit discussion download noise</a>
<a href="missing-download-page.html">Broken download page</a>
<a href="downloads.html">Download</a>
</body></html>
HTML

cat > "$DOWNLOAD_PAGE" <<HTML
<!doctype html>
<html><body><a href="file://$ORIGINAL_ROOT/$WEBSITE_ASSET">Download AppImage</a></body></html>
HTML

mkdir -p "$(dirname "$REDDIT_PAGE")"
cat > "$REDDIT_PAGE" <<HTML
<!doctype html>
<html><body><a href="file://$ORIGINAL_ROOT/$REDDIT_ASSET">Wrong AppImage from Reddit</a></body></html>
HTML

mkdir -p "$(dirname "$MUSESCORE_CATALOG_PAGE")" "$(dirname "$MUSESCORE_HOME_PAGE")"
cat > "$MUSESCORE_CATALOG_PAGE" <<HTML
<!doctype html>
<html><body><a href="file://$MUSESCORE_HOME_PAGE">MuseScore official site</a></body></html>
HTML

cat > "$MUSESCORE_HOME_PAGE" <<'HTML'
<!doctype html>
<html><body><p>MuseScore official site</p></body></html>
HTML

mkdir -p "$(dirname "$MUSESCORE_LANDING_PAGE")"
cat > "$MUSESCORE_DOWNLOAD_PAGE" <<'HTML'
<!doctype html>
<html><body><a href="en/download/musescore-x86_64.AppImage">Download MuseScore AppImage</a></body></html>
HTML

cat > "$MUSESCORE_LANDING_PAGE" <<HTML
<!doctype html>
<html><body><a href="file://$ORIGINAL_ROOT/$MUSESCORE_ASSET">Direct MuseScore AppImage</a></body></html>
HTML

cat > "$LATEST" <<JSON
{
  "tag_name": "v1.0.0",
  "assets": [
    {
      "name": "$ASSET",
      "browser_download_url": "file://$ORIGINAL_ROOT/$ASSET"
    }
  ]
}
JSON

cat > "$FEED" <<'JSON'
{
  "version": 1,
  "items": [
    {
      "name": "Feed App",
      "description": "Imported from AppImage feed. This longer description should not be stored as the summary.",
      "license": "MIT",
      "links": [
        {
          "type": "GitHub",
          "url": "acme/feedapp"
        },
        {
          "type": "Homepage",
          "url": "https://example.com/feedapp"
        }
      ]
    },
    {
      "name": "Direct Feed App",
      "description": "Imported from a direct AppImage download link",
      "license": "MIT",
      "links": [
        {
          "type": "Download",
          "url": "DIRECT_APPIMAGE_URL"
        }
      ]
    },
    {
      "name": "No GitHub App",
      "description": "Imported from an official website page",
      "links": [
        {
          "type": "Homepage",
          "url": "WEBSITE_PAGE_URL{url}"
        }
      ]
    },
    {
      "name": "MuseScore",
      "description": "Imported from an AppImage catalog page",
      "links": [
        {
          "type": "Homepage",
          "url": "MUSESCORE_CATALOG_PAGE_URL{url}"
        }
      ]
    },
    {
      "name": "No Source App",
      "description": "Listed without any source links",
      "links": null
    }
  ]
}
JSON
sed -i "s|DIRECT_APPIMAGE_URL|file://$ORIGINAL_ROOT/$DIRECT_ASSET|" "$FEED"
sed -i "s|WEBSITE_PAGE_URL|file://$WEBSITE_PAGE|" "$FEED"
sed -i "s|MUSESCORE_CATALOG_PAGE_URL|file://$MUSESCORE_CATALOG_PAGE|" "$FEED"

HOME="$TMP_HOME" "$ROOT/yai" repo add appimage "$FEED"
HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q $'appimage\t'
grep -q '"summary": "Imported from AppImage feed."' "$TMP_HOME/.local/share/yai/repos/index.json"
if grep -q "This longer description" "$TMP_HOME/.local/share/yai/repos/index.json"; then
  echo "feed import stored a full description as summary" >&2
  exit 1
fi
HOME="$TMP_HOME" "$ROOT/yai" search feed | grep -q "feed-app"
HOME="$TMP_HOME" "$ROOT/yai" search direct | grep -q "direct-feed-app"
HOME="$TMP_HOME" "$ROOT/yai" search github | grep -q "no-github-app"
HOME="$TMP_HOME" "$ROOT/yai" search musescore | grep -q "musescore"
HOME="$TMP_HOME" "$ROOT/yai" search source | grep -q "no-source-app"
HOME="$TMP_HOME" "$ROOT/yai" info feed-app | grep -q "Source: github_release acme/feedapp"
HOME="$TMP_HOME" "$ROOT/yai" info direct-feed-app | grep -q "Source: direct_url"
HOME="$TMP_HOME" "$ROOT/yai" info no-github-app | grep -q "Source: website_page"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install feed-app

HOME="$TMP_HOME" "$TMP_HOME/.local/bin/feed-app" | grep -q "feed app"
HOME="$TMP_HOME" "$ROOT/yai" remove feed-app

HOME="$TMP_HOME" "$ROOT/yai" install direct-feed-app
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/direct-feed-app" | grep -q "direct feed app"
HOME="$TMP_HOME" "$ROOT/yai" remove direct-feed-app

HOME="$TMP_HOME" "$ROOT/yai" install no-github-app 2>"$TMP_HOME/website_install.err"
grep -q "website search for no-github-app" "$TMP_HOME/website_install.err"
grep -q "website search selected" "$TMP_HOME/website_install.err"
grep -q "after checking 3 page(s), queued 3, skipped 1" "$TMP_HOME/website_install.err"
grep -q "WebsiteFeed-x86_64.AppImage" "$TMP_HOME/website_install.err"
if grep -q "checking website page" "$TMP_HOME/website_install.err"; then
  echo "website search printed per-page progress while stderr was redirected" >&2
  exit 1
fi
if grep -q "queued candidate page" "$TMP_HOME/website_install.err"; then
  echo "website search printed queued page details while stderr was redirected" >&2
  exit 1
fi
if grep -q "found AppImage candidate" "$TMP_HOME/website_install.err"; then
  echo "website search printed candidate details while stderr was redirected" >&2
  exit 1
fi
if grep -q "missing-download-page.html" "$TMP_HOME/website_install.err"; then
  echo "website search printed stale skipped URL details while stderr was redirected" >&2
  exit 1
fi
if grep -q "appimage.github.io/help" "$TMP_HOME/website_install.err"; then
  echo "appimage navigation link was fetched" >&2
  exit 1
fi
if grep -q "appimage.github.io/categories/Qt" "$TMP_HOME/website_install.err"; then
  echo "appimage category link was fetched" >&2
  exit 1
fi
if grep -q "kdenlive.org/en/news" "$TMP_HOME/website_install.err"; then
  echo "official non-download page was fetched" >&2
  exit 1
fi
if grep -q "github.com/AppImage" "$TMP_HOME/website_install.err"; then
  echo "generic AppImage GitHub page was fetched" >&2
  exit 1
fi
if grep -q "subsurface-divelog.org" "$TMP_HOME/website_install.err"; then
  echo "website search selected an unrelated project AppImage" >&2
  exit 1
fi
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/no-github-app" | grep -q "website feed app"
if HOME="$TMP_HOME" "$TMP_HOME/.local/bin/no-github-app" | grep -q "reddit wrong app"; then
  echo "website search followed reddit instead of official download pages" >&2
  exit 1
fi
HOME="$TMP_HOME" "$ROOT/yai" remove no-github-app

HOME="$TMP_HOME" \
"$ROOT/yai" install "file://$MUSESCORE_DOWNLOAD_PAGE" \
  --id musescore-direct \
  --name "MuseScore Direct" \
  2>"$TMP_HOME/musescore_direct_install.err"
grep -q "downloaded an AppImage landing page" "$TMP_HOME/musescore_direct_install.err"
grep -Fq "\"download_url\": \"file://$ORIGINAL_ROOT/$MUSESCORE_ASSET\"" "$TMP_HOME/.local/share/yai/apps/musescore-direct/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/musescore-direct" | grep -q "musescore app"
HOME="$TMP_HOME" "$ROOT/yai" remove musescore-direct

HOME="$TMP_HOME" "$ROOT/yai" install musescore 2>"$TMP_HOME/musescore_install.err"
grep -q "website search selected" "$TMP_HOME/musescore_install.err"
grep -q "MuseScore-x86_64.AppImage" "$TMP_HOME/musescore_install.err"
if grep -q "curl:" "$TMP_HOME/musescore_install.err"; then
  echo "curl errors leaked into install status" >&2
  exit 1
fi
if grep -q "% Total" "$TMP_HOME/musescore_install.err"; then
  echo "curl progress meter leaked into install status" >&2
  exit 1
fi
grep -Fq "\"download_url\": \"file://$ORIGINAL_ROOT/$MUSESCORE_ASSET\"" "$TMP_HOME/.local/share/yai/apps/musescore/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/musescore" | grep -q "musescore app"
cat > "$MUSESCORE_LANDING_PAGE" <<HTML
<!doctype html>
<html><body><input id="download-link" type="hidden" value="file://$ORIGINAL_ROOT/$MUSESCORE_V2_ASSET" /></body></html>
HTML
HOME="$TMP_HOME" "$ROOT/yai" update musescore >"$TMP_HOME/musescore_update.out"
grep -q $'musescore\tMuseScore-x86_64.AppImage\tMuseScore-v2-x86_64.AppImage\tupgradable' "$TMP_HOME/musescore_update.out"
HOME="$TMP_HOME" "$ROOT/yai" upgrade musescore 2>"$TMP_HOME/musescore_upgrade.err"
grep -q "website search selected" "$TMP_HOME/musescore_upgrade.err"
grep -q "MuseScore-v2-x86_64.AppImage" "$TMP_HOME/musescore_upgrade.err"
grep -Fq "\"download_url\": \"file://$ORIGINAL_ROOT/$MUSESCORE_V2_ASSET\"" "$TMP_HOME/.local/share/yai/apps/musescore/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/musescore" | grep -q "musescore v2 app"
HOME="$TMP_HOME" "$ROOT/yai" remove musescore

echo "appimage feed smoke test passed"
