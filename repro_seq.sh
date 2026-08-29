#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="/workspace"
TMP_HOME="$(mktemp -d)"
echo "TMP_HOME=$TMP_HOME"
API_ROOT="$TMP_HOME/api"
ORIGINAL_ROOT="$TMP_HOME/original"
FEED="$TMP_HOME/feed.json"
ASSET="FeedApp-x86_64.AppImage"
DIRECT_ASSET="DirectFeed.AppImage"
WEBSITE_ASSET="WebsiteFeed-x86_64.AppImage"
MUSESCORE_ASSET="MuseScore-x86_64.AppImage"
MUSESCORE_V2_ASSET="MuseScore-v2-x86_64.AppImage"
WEBSITE_PAGE="$TMP_HOME/website.html"
DOWNLOAD_PAGE="$TMP_HOME/downloads.html"
REDDIT_PAGE="$TMP_HOME/reddit.com/downloads.html"

MUSESCORE_CATALOG_PAGE="$TMP_HOME/appimage.github.io/MuseScore/index.html"
MUSESCORE_CATALOG_URL="http://appimage.github.io.test/MuseScore/index.html"
MUSESCORE_HOME_PAGE="$TMP_HOME/musescore.org/index.html"
MUSESCORE_HOME_URL="http://musescore.test/index.html"
MUSESCORE_DISCOVERY_PAGE="$TMP_HOME/musescore.org/download/index.html"
MUSESCORE_DISCOVERY_URL="http://musescore.test/download/"
MUSESCORE_DOWNLOAD_PAGE="$TMP_HOME/musescore.org/downloads/index.html"
MUSESCORE_DOWNLOAD_URL="http://musescore.test/downloads/"
MUSESCORE_LANDING_PAGE="$TMP_HOME/musescore.org/en/download/musescore-x86_64.AppImage"
MUSESCORE_LANDING_URL="http://musescore.test/en/download/musescore-x86_64.AppImage"

mkdir -p "$(dirname "$TMP_HOME/api/repos/acme/feedapp/releases/latest")" \
         "$ORIGINAL_ROOT" \
         "$(dirname "$REDDIT_PAGE")" \
         "$(dirname "$MUSESCORE_CATALOG_PAGE")" "$(dirname "$MUSESCORE_HOME_PAGE")" \
         "$(dirname "$MUSESCORE_LANDING_PAGE")" "$(dirname "$MUSESCORE_DISCOVERY_PAGE")" \
         "$(dirname "$MUSESCORE_DOWNLOAD_PAGE")" \
         "$TMP_HOME/fake-bin"

cat > "$ORIGINAL_ROOT/$ASSET" <<APP
#!/usr/bin/env bash
echo "feed app"
APP
chmod +x "$ORIGINAL_ROOT/$ASSET"

cat > "$ORIGINAL_ROOT/$DIRECT_ASSET" <<APP
#!/usr/bin/env bash
echo "direct feed app"
APP
chmod +x "$ORIGINAL_ROOT/$DIRECT_ASSET"

cat > "$ORIGINAL_ROOT/$WEBSITE_ASSET" <<APP
#!/usr/bin/env bash
echo "website feed app"
APP
chmod +x "$ORIGINAL_ROOT/$WEBSITE_ASSET"

cat > "$ORIGINAL_ROOT/$MUSESCORE_ASSET" <<APP
#!/usr/bin/env bash
echo "musescore app"
APP
chmod +x "$ORIGINAL_ROOT/$MUSESCORE_ASSET"

cat > "$WEBSITE_PAGE" <<HTML
<!doctype html>
<html><body>
<a href="https://appimage.github.io/help">Help</a>
<a href="missing-download-page.html">Broken download page</a>
<a href="downloads.html">Download</a>
</body></html>
HTML

cat > "$DOWNLOAD_PAGE" <<HTML
<!doctype html>
<html><body><a href="file://$ORIGINAL_ROOT/$WEBSITE_ASSET">Download AppImage</a></body></html>
HTML

cat > "$REDDIT_PAGE" <<HTML
<!doctype html>
<html><body>wrong</body></html>
HTML

cat > "$MUSESCORE_CATALOG_PAGE" <<HTML
<!doctype html>
<html><body><a href="$MUSESCORE_HOME_URL">MuseScore official site</a></body></html>
HTML

cat > "$MUSESCORE_HOME_PAGE" <<HTML
<!doctype html>
<html><body>
<p>MuseScore official site</p>
<a href="/download/">Get MuseScore</a>
<a href="/downloads/">All MuseScore downloads</a>
</body></html>
HTML

cat > "$MUSESCORE_DISCOVERY_PAGE" <<HTML
<!doctype html>
<html><body><a href="/downloads/">Download MuseScore</a></body></html>
HTML

cat > "$MUSESCORE_DOWNLOAD_PAGE" <<HTML
<!doctype html>
<html><body><a href="/en/download/musescore-x86_64.AppImage">Download MuseScore AppImage</a></body></html>
HTML

cat > "$MUSESCORE_LANDING_PAGE" <<HTML
<!doctype html>
<html><body><a href="file://$ORIGINAL_ROOT/$MUSESCORE_ASSET">Direct MuseScore AppImage</a></body></html>
HTML

cat > "$FEED" <<JSON
{
  "version": 1,
  "items": [
    {
      "name": "MuseScore",
      "description": "Imported from an AppImage catalog page",
      "links": [
        {
          "type": "Homepage",
          "url": "$MUSESCORE_CATALOG_URL"
        }
      ]
    }
  ]
}
JSON

export TMP_HOME MUSESCORE_DOWNLOAD_URL
HOME="$TMP_HOME" "$ROOT/yai" repo add appimage "$FEED"

echo "=== Running musescore-direct install ==="
set +e
HOME="$TMP_HOME" \
"$ROOT/yai" install "file://$MUSESCORE_DOWNLOAD_PAGE" \
  --id musescore-direct \
  --name "MuseScore Direct" \
  2>"$TMP_HOME/musescore_direct_install.err"
rc=$?
set -e
echo "musescore-direct install exit=$rc"
echo "--- stderr ---"
cat "$TMP_HOME/musescore_direct_install.err"
echo "--- grep landing ---"
grep "landing" "$TMP_HOME/musescore_direct_install.err" || echo "NO LANDING MESSAGE"
if [[ -f "$TMP_HOME/.local/share/yai/apps/musescore-direct/metadata.json" ]]; then
  echo "--- metadata.json download_url ---"
  grep download_url "$TMP_HOME/.local/share/yai/apps/musescore-direct/metadata.json"
fi

rm -rf "$TMP_HOME"
