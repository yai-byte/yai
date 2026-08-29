#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="/workspace"
TMP_HOME="$(mktemp -d)"
API_ROOT="$TMP_HOME/api"
ORIGINAL_ROOT="$TMP_HOME/original"
FEED="$TMP_HOME/feed.json"
MUSESCORE_ASSET="MuseScore-x86_64.AppImage"
MUSESCORE_V2_ASSET="MuseScore-v2-x86_64.AppImage"

MUSESCORE_CATALOG_PAGE="$TMP_HOME/appimage.github.io/MuseScore/index.html"
MUSESCORE_CATALOG_URL="http://appimage.github.io.test/MuseScore/index.html"
MUSESCORE_HOME_PAGE="$TMP_HOME/musescore.org/index.html"
MUSESCORE_HOME_URL="http://musescore.test/index.html"
MUSESCORE_DISCOVERY_PAGE="$TMP_HOME/musescore.org/download.html"
MUSESCORE_DISCOVERY_URL="http://musescore.test/download.html"
MUSESCORE_DOWNLOAD_PAGE="$TMP_HOME/musescore.org/downloads.html"
MUSESCORE_DOWNLOAD_URL="http://musescore.test/downloads.html"
MUSESCORE_LANDING_PAGE="$TMP_HOME/musescore.org/en/download/musescore-x86_64.AppImage"
MUSESCORE_LANDING_URL="http://musescore.test/en/download/musescore-x86_64.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
  echo "cleanup done"
}
trap cleanup EXIT

mkdir -p "$(dirname "$MUSESCORE_CATALOG_PAGE")" "$(dirname "$MUSESCORE_HOME_PAGE")" "$(dirname "$MUSESCORE_LANDING_PAGE")" "$ORIGINAL_ROOT"

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

cat > "$MUSESCORE_CATALOG_PAGE" <<HTML
<!doctype html>
<html><body><a href="$MUSESCORE_HOME_URL">MuseScore official site</a></body></html>
HTML

cat > "$MUSESCORE_HOME_PAGE" <<'HTML'
<!doctype html>
<html><body>
<p>MuseScore official site</p>
<a href="download.html">Get MuseScore</a>
<a href="downloads.html">All MuseScore downloads</a>
</body></html>
HTML

cat > "$MUSESCORE_DISCOVERY_PAGE" <<'HTML'
<!doctype html>
<html><body><a href="downloads.html">Download MuseScore</a></body></html>
HTML

cat > "$MUSESCORE_DOWNLOAD_PAGE" <<'HTML'
<!doctype html>
<html><body><a href="en/download/musescore-x86_64.AppImage">Download MuseScore AppImage</a></body></html>
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

export TMP_HOME
export MUSESCORE_DOWNLOAD_URL

HOME="$TMP_HOME" "$ROOT/yai" repo add appimage "$FEED"

REAL_CURL="$(command -v curl)"
FAKE_BIN="$TMP_HOME/fake-bin"
mkdir -p "$FAKE_BIN"
export REAL_CURL

cat > "$FAKE_BIN/curl" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$TMP_HOME/website-curl.log"

if [[ "$*" == *"$MUSESCORE_DOWNLOAD_URL"* &&
      " $* " == *" --max-time 5"* &&
      ! -e "$TMP_HOME/speculative-download-failed" ]]; then
  touch "$TMP_HOME/speculative-download-failed"
  exit 28
fi

url=""
output=""
prev=""
for arg in "$@"; do
  case "$arg" in
    -o|--output) prev="output" ;;
    -o=*|--output=*) output="${arg#*=}" ;;
    *)
      if [[ -n "$prev" ]]; then
        if [[ "$prev" == "output" ]]; then output="$arg"; fi
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
  https://raw.githubusercontent.com/AppImage/*|https://api.github.com/repos/AppImage/appimage.github.io/*|https://appimagehub*|https://appimage.github.io/*)
    exit 22
    ;;
esac

serve_path=""
case "$url" in
  http://appimage.github.io.test/*)
    rel="${url#http://appimage.github.io.test/}"
    serve_path="$TMP_HOME/appimage.github.io/${rel}"
    ;;
  http://musescore.test/*)
    rel="${url#http://musescore.test/}"
    serve_path="$TMP_HOME/musescore.org/${rel}"
    ;;
esac

if [[ -z "$serve_path" ]]; then
  exec "$REAL_CURL" "$@"
fi

if [[ -d "$serve_path" || "${serve_path: -1}" == "/" ]]; then
  serve_path="${serve_path%/}/index.html"
fi

if [[ ! -e "$serve_path" ]]; then
  exit 22
fi

if [[ -n "$output" ]]; then
  case "$output" in
    /dev/null) : ;;
    *) cp -- "$serve_path" "$output" || exit 23 ;;
  esac
else
  cat -- "$serve_path"
fi
exit 0
SH
chmod +x "$FAKE_BIN/curl"

: > "$TMP_HOME/website-curl.log"
rm -f "$TMP_HOME/speculative-download-failed"
echo "=== Installing musescore ==="
set +e
PATH="$FAKE_BIN:$PATH" HOME="$TMP_HOME" timeout 60 "$ROOT/yai" install musescore 2>"$TMP_HOME/musescore_install.err"
rc=$?
set -e
echo "=== musescore install exit: $rc ==="
echo "=== musescore_install.err ==="
cat "$TMP_HOME/musescore_install.err"
echo "=== website-curl.log (last 50 lines) ==="
tail -50 "$TMP_HOME/website-curl.log"
echo "=== speculative-download-failed? $(test -e "$TMP_HOME/speculative-download-failed" && echo YES || echo NO) ==="
echo "=== files in musescore app dir ==="
ls -la "$TMP_HOME/.local/share/yai/apps/musescore/" 2>/dev/null || echo "no musescore app dir"
echo "=== website search for: $(grep "website search" "$TMP_HOME/musescore_install.err" || echo "NO WEBSITE SEARCH MSG") ==="
