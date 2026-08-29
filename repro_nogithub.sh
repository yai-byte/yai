#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="/workspace"
TMP_HOME="$(mktemp -d)"
echo "TMP_HOME=$TMP_HOME"

ORIGINAL_ROOT="$TMP_HOME/original"
WEBSITE_ASSET="WebsiteFeed-x86_64.AppImage"
WEBSITE_PAGE="$TMP_HOME/website.html"
DOWNLOAD_PAGE="$TMP_HOME/downloads.html"
REDDIT_PAGE="$TMP_HOME/reddit.com/downloads.html"
FEED="$TMP_HOME/feed.json"

mkdir -p "$ORIGINAL_ROOT" "$(dirname "$REDDIT_PAGE")"

cat > "$ORIGINAL_ROOT/$WEBSITE_ASSET" <<'APP'
#!/usr/bin/env bash
echo "website feed app"
APP
chmod +x "$ORIGINAL_ROOT/$WEBSITE_ASSET"

cat > "$WEBSITE_PAGE" <<'HTML'
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

mkdir -p "$(dirname "$REDDIT_PAGE")"
cat > "$REDDIT_PAGE" <<HTML
<!doctype html>
<html><body><a href="file://$TMP_HOME/invalid">Wrong</a></body></html>
HTML

cat > "$FEED" <<JSON
{
  "version": 1,
  "items": [
    {
      "name": "No GitHub App",
      "description": "Imported from an official website page",
      "links": [
        {
          "type": "Homepage",
          "url": "file://$WEBSITE_PAGE"
        }
      ]
    }
  ]
}
JSON

HOME="$TMP_HOME" "$ROOT/yai" repo add appimage "$FEED"

REAL_CURL="$(command -v curl)"
FAKE_BIN="$TMP_HOME/fake-bin"
mkdir -p "$FAKE_BIN"
export REAL_CURL TMP_HOME
export MUSESCORE_DOWNLOAD_URL="unused"

cat > "$FAKE_BIN/curl" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$TMP_HOME/website-curl.log"

url=""
output=""
write_out=""
prev=""
for arg in "$@"; do
  case "$arg" in
    -o|--output) prev="output" ;;
    -o=*|--output=*) output="${arg#*=}" ;;
    --write-out|--writeout|-w) prev="writeout" ;;
    --write-out=*|--writeout=*|-w=*) write_out="${arg#*=}" ;;
    *)
      if [[ -n "$prev" ]]; then
        case "$prev" in
          output) output="$arg" ;;
          writeout) write_out="$arg" ;;
        esac
        prev=""
      elif [[ "$arg" == http://* || "$arg" == https://* ]]; then
        url="$arg"
      fi
      ;;
  esac
done

if [[ -z "$url" ]]; then
  exec "$REAL_CURL" "$@"
fi

case "$url" in
  https://raw.githubusercontent.com/AppImage/*|https://api.github.com/repos/AppImage/*|https://appimagehub*|https://appimage.github.io/*)
    exit 22
    ;;
esac

# Any unknown HTTPS URL → fail quickly (no live network)
# We don't serve fixture pages for no-github-app; the feed uses file:// URLs.
exit 22
SH
chmod +x "$FAKE_BIN/curl"

set +e
PATH="$FAKE_BIN:$PATH" HOME="$TMP_HOME" "$ROOT/yai" install no-github-app 2>"$TMP_HOME/website_install.err"
rc=$?
set -e
echo "no-github-app install exit: $rc"
echo "--- stderr last 20 ---"
tail -20 "$TMP_HOME/website_install.err"
echo "--- website-curl.log lines ---"
wc -l < "$TMP_HOME/website-curl.log" 2>/dev/null || echo 0

echo "--- grep website search selected ---"
grep "website search selected" "$TMP_HOME/website_install.err" || echo "NOT FOUND"
echo "--- WebsiteFeed asset in stderr ---"
grep "WebsiteFeed-x86_64.AppImage" "$TMP_HOME/website_install.err" || echo "NOT FOUND"

if [[ $rc -eq 0 ]]; then
  echo "--- running no-github-app ---"
  HOME="$TMP_HOME" "$TMP_HOME/.local/bin/no-github-app" 2>&1 || echo "run failed"
fi

rm -rf "$TMP_HOME"
