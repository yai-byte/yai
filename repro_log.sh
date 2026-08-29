#!/usr/bin/env bash
set -uo pipefail
export YAI_LANG=en
ROOT="/workspace"
TMP_HOME="$(mktemp -d)"
echo "TMP_HOME=$TMP_HOME"

# Minimal: copy fake curl + fixture setup
ORIGINAL_ROOT="$TMP_HOME/original"
MUSESCORE_ASSET="MuseScore-x86_64.AppImage"
MUSESCORE_CATALOG_PAGE="$TMP_HOME/appimage.github.io/MuseScore/index.html"
MUSESCORE_CATALOG_URL="http://appimage.github.io.test/MuseScore/index.html"
MUSESCORE_HOME_PAGE="$TMP_HOME/musescore.org/index.html"
MUSESCORE_HOME_URL="http://musescore.test/index.html"
MUSESCORE_DOWNLOAD_PAGE_HTTP="$TMP_HOME/musescore.org/downloads/index.html"
MUSESCORE_DOWNLOAD_URL="http://musescore.test/downloads"
MUSESCORE_LANDING_PAGE="$TMP_HOME/musescore.org/en/download/musescore-x86_64.AppImage"
MUSESCORE_LANDING_URL="http://musescore.test/en/download/musescore-x86_64.AppImage"
FEED="$TMP_HOME/feed.json"

mkdir -p "$ORIGINAL_ROOT" "$(dirname "$MUSESCORE_CATALOG_PAGE")" "$(dirname "$MUSESCORE_HOME_PAGE")" "$(dirname "$MUSESCORE_LANDING_PAGE")" "$(dirname "$MUSESCORE_DOWNLOAD_PAGE_HTTP")" "$TMP_HOME/musescore.org/download" "$TMP_HOME/fake-bin"

cat > "$ORIGINAL_ROOT/$MUSESCORE_ASSET" <<'APP'
#!/usr/bin/env bash
echo "musescore app"
APP
chmod +x "$ORIGINAL_ROOT/$MUSESCORE_ASSET"

cat > "$MUSESCORE_CATALOG_PAGE" <<HTML
<!doctype html>
<html><body><a href="$MUSESCORE_HOME_URL">MuseScore official site</a></body></html>
HTML

cat > "$MUSESCORE_HOME_PAGE" <<'HTML'
<!doctype html>
<html><body>
<p>MuseScore official site</p>
<a href="/download/">Get MuseScore</a>
<a href="/downloads/">All MuseScore downloads</a>
</body></html>
HTML

cat > "$TMP_HOME/musescore.org/download/index.html" <<'HTML'
<!doctype html>
<html><body><a href="/downloads/">Download MuseScore</a></body></html>
HTML

cat > "$MUSESCORE_DOWNLOAD_PAGE_HTTP" <<'HTML'
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
      "links": [{"type": "Homepage", "url": "$MUSESCORE_CATALOG_URL"}]
    }
  ]
}
JSON

export TMP_HOME MUSESCORE_DOWNLOAD_URL
HOME="$TMP_HOME" "$ROOT/yai" repo add appimage "$FEED"

REAL_CURL="$(command -v curl)"
FAKE_BIN="$TMP_HOME/fake-bin"
export REAL_CURL
cat > "$FAKE_BIN/curl" <<'SH'
#!/usr/bin/env bash
{
  line="$*"
  line="${line//$'\n'/ }"
  printf '%s\n' "$line"
} >> "$TMP_HOME/website-curl.log"

if [[ "$*" == *"$MUSESCORE_DOWNLOAD_URL"* &&
      " $* " == *" --max-time 5"* &&
      ! -e "$TMP_HOME/speculative-download-failed" ]]; then
  touch "$TMP_HOME/speculative-download-failed"
  exit 28
fi

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

if [[ -z "$url" ]]; then exec "$REAL_CURL" "$@"; fi

case "$url" in
  https://raw.githubusercontent.com/AppImage/*|https://api.github.com/repos/AppImage/*|https://appimagehub*|https://appimage.github.io/*) exit 22 ;;
esac

serve_path=""
case "$url" in
  http://appimage.github.io.test/*) rel="${url#http://appimage.github.io.test/}"; serve_path="$TMP_HOME/appimage.github.io/${rel}" ;;
  http://musescore.test/*) rel="${url#http://musescore.test/}"; serve_path="$TMP_HOME/musescore.org/${rel}" ;;
esac
if [[ -z "$serve_path" ]]; then exec "$REAL_CURL" "$@"; fi

effective_url="$url"
if [[ -d "$serve_path" ]]; then
  effective_url="${url%/}/"
  serve_path="${serve_path%/}/index.html"
elif [[ "${serve_path: -1}" == "/" ]]; then
  serve_path="${serve_path%/}/index.html"
fi
if [[ ! -e "$serve_path" ]]; then exit 22; fi

if [[ -n "$output" ]]; then
  case "$output" in /dev/null) : ;; *) cp -- "$serve_path" "$output" || exit 23 ;; esac
else
  cat -- "$serve_path"
fi
if [[ -n "$write_out" ]]; then
  expanded="${write_out//'%{url_effective}'/$effective_url}"
  printf '%s' "$expanded"
fi
exit 0
SH
chmod +x "$FAKE_BIN/curl"

: > "$TMP_HOME/website-curl.log"
rm -f "$TMP_HOME/speculative-download-failed"
PATH="$FAKE_BIN:$PATH" HOME="$TMP_HOME" timeout 60 "$ROOT/yai" install musescore >/dev/null 2>"$TMP_HOME/musescore_install.err"
echo "install exit=$?"

echo "=== All lines in curl log ==="
cat -n "$TMP_HOME/website-curl.log"

echo ""
echo "=== grep -F '$MUSESCORE_DOWNLOAD_URL' (with max-time 5 & 15) ==="
echo "-- max-time 5 lines:"
grep -F "$MUSESCORE_DOWNLOAD_URL" "$TMP_HOME/website-curl.log" | grep -- '--max-time 5' || echo "NONE"
echo "-- max-time 15 lines:"
grep -F "$MUSESCORE_DOWNLOAD_URL" "$TMP_HOME/website-curl.log" | grep -- '--max-time 15' || echo "NONE"

rm -rf "$TMP_HOME"
