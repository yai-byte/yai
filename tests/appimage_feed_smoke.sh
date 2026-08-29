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
MUSESCORE_CATALOG_URL="http://appimage.github.io.test/MuseScore/index.html"
MUSESCORE_HOME_PAGE="$TMP_HOME/musescore.org/index.html"
MUSESCORE_HOME_URL="http://musescore.test/index.html"
MUSESCORE_DISCOVERY_PAGE="$TMP_HOME/musescore.org/download/index.html"
MUSESCORE_DISCOVERY_URL="http://musescore.test/download/"
# HTTP fixture: directory rooted at /musescore.test/downloads (without the
# trailing slash on the URL). The hint URL generator produces /downloads for
# catalog-bridged projects; the fake curl detects a directory hit and emits
# effective URL = /downloads/ (so later href resolution is scoped correctly).
MUSESCORE_DOWNLOAD_PAGE_HTTP="$TMP_HOME/musescore.org/downloads/index.html"
MUSESCORE_DOWNLOAD_URL="http://musescore.test/downloads"
# Direct-install fixture: a plain file served via file://. Absolute-root
# hrefs (/en/...) are wrong under file:// (they become /en/ on the host
# filesystem instead of being anchored to the fixture root), so this file
# uses a directory-relative href that resolves correctly for both modes.
MUSESCORE_DOWNLOAD_PAGE_DIRECT="$TMP_HOME/musescore.org/direct-download.html"
MUSESCORE_LANDING_PAGE="$TMP_HOME/musescore.org/en/download/musescore-x86_64.AppImage"
MUSESCORE_LANDING_URL="http://musescore.test/en/download/musescore-x86_64.AppImage"

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

mkdir -p "$(dirname "$MUSESCORE_LANDING_PAGE")" "$(dirname "$MUSESCORE_DISCOVERY_PAGE")" "$(dirname "$MUSESCORE_DOWNLOAD_PAGE_HTTP")"
cat > "$MUSESCORE_DISCOVERY_PAGE" <<'HTML'
<!doctype html>
<html><body><a href="/downloads/">Download MuseScore</a></body></html>
HTML

# HTTP fixture: absolute-root href (matches the fixed MUSESCORE_LANDING_URL).
cat > "$MUSESCORE_DOWNLOAD_PAGE_HTTP" <<'HTML'
<!doctype html>
<html><body><a href="/en/download/musescore-x86_64.AppImage">Download MuseScore AppImage</a></body></html>
HTML

# Direct-install fixture (file://): directory-relative href so resolve_href_url
# stays inside the fixture tree rather than referencing the host filesystem root.
cat > "$MUSESCORE_DOWNLOAD_PAGE_DIRECT" <<'HTML'
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
sed -i "s|MUSESCORE_CATALOG_PAGE_URL|$MUSESCORE_CATALOG_URL|" "$FEED"

# Preload env values used inside the fake-curl heredoc so subshells pick them.
export TMP_HOME
export MUSESCORE_DOWNLOAD_URL

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

REAL_CURL="$(command -v curl)"
FAKE_BIN="$TMP_HOME/fake-bin"
mkdir -p "$FAKE_BIN"
# Map of fixture HTTP host → the directory under $TMP_HOME that serves as
# the document root. Any URL whose host matches is served from that dir;
# the fake server also denies live AppImage catalog / data / apps lookups
# so the resolver falls back to the fixture exactly (and the test remains
# fully deterministic even when the sandbox has live network access).
#
# IMPORTANT: The heredoc below is QUOTED ('SH'), so $VAR references are
# evaluated by bash at curl-invocation time from the environment the
# script already exports (TMP_HOME, MUSESCORE_DOWNLOAD_URL). Never use
# sed/post-processing to substitute variables here — it mangles `\n` in
# printf format strings.
export REAL_CURL
cat > "$FAKE_BIN/curl" <<'SH'
#!/usr/bin/env bash
# Log the full argv on exactly one line. The C++ caller embeds a literal
# newline in the --write-out format string (it prefixes __YAI_EFFECTIVE_URL__
# with \n), so a naive echo/printf split the log entry across lines and
# defeated later grep assertions that need --max-time and the URL to appear
# on the same grep-matchable line. Collapse whitespace (including the
# embedded \n) to a single space for logging; in-memory argv inspection
# below still uses the original split "$@"/"$*" values so pattern matching
# is unaffected.
{
  line="$*"
  line="${line//$'\n'/ }"
  printf '%s\n' "$line"
} >> "$TMP_HOME/website-curl.log"

# 1) MuseScore speculative probe of the download listing page must fail once so
#    that the non-speculative follow-up with --max-time 15 is observable.
if [[ "$*" == *"$MUSESCORE_DOWNLOAD_URL"* &&
      " $* " == *" --max-time 5"* &&
      ! -e "$TMP_HOME/speculative-download-failed" ]]; then
  touch "$TMP_HOME/speculative-download-failed"
  exit 28
fi

# 2) Extract positional URL argument, -o/--output destination, and
#    --write-out format string.
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

# 3) No URL: pass-through to real curl (e.g. file:// URL handling lives in
#    yai itself, but curl may still be invoked with non-URL flags).
if [[ -z "$url" ]]; then
  exec "$REAL_CURL" "$@"
fi

# 4) Block live AppImage GitHub data/apps/catalog lookups so the test stays
#    hermetic. Without this, the parallel fallback to data/MuseScore reaches
#    the real jsdelivr-hosted build and bypasses the fixture entirely.
case "$url" in
  https://raw.githubusercontent.com/AppImage/*|https://api.github.com/repos/AppImage/appimage.github.io/*|https://appimagehub*|https://appimage.github.io/*)
    exit 22
    ;;
esac

# 5) Translate fixture HTTP URLs into on-disk fixture paths.
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

# 6) Unknown URL → fail immediately so the test stays hermetic.
#    Any HTTP(S) URL that is not a fixture host must not reach the real
#    network, otherwise parallel fallbacks or package-name URL hints reach
#    live hosted AppImages and the fixture-based assertions no longer hold.
#    (file:// URLs are handled by yai directly; they never pass through curl
#     with an http scheme, so this block only catches remote lookups.)
if [[ -z "$serve_path" ]]; then
  exit 22
fi

# 7) Directory-index resolution. Compute an effective URL that mirrors the
#    directory-redirect semantics of a real web server so later href
#    resolution from the resulting page uses a directory-typed base.
effective_url="$url"
if [[ -d "$serve_path" ]]; then
  # Fixture URL points at a directory without a trailing slash → the server
  # would redirect to <url>/; reflect that in the effective URL and serve
  # index.html out of that directory.
  effective_url="${url%/}/"
  serve_path="${serve_path%/}/index.html"
elif [[ "${serve_path: -1}" == "/" ]]; then
  serve_path="${serve_path%/}/index.html"
fi

if [[ ! -e "$serve_path" ]]; then
  exit 22
fi

# 8) Honour -o/--output otherwise print to stdout. Always append the
#    --write-out format expansion (real curl writes --write-out output to
#    stdout regardless of -o), substituting %{url_effective} with the
#    effective URL we derived above.
if [[ -n "$output" ]]; then
  case "$output" in
    /dev/null) : ;;
    *) cp -- "$serve_path" "$output" || exit 23 ;;
  esac
else
  cat -- "$serve_path"
fi
if [[ -n "$write_out" ]]; then
  # Expand the single curl variable that fetch_text_with_effective_url
  # depends on (the __YAI_EFFECTIVE_URL__ sentinel that marks where the
  # in-band content ends and postfix metadata begins).
  expanded="${write_out//'%{url_effective}'/$effective_url}"
  printf '%s' "$expanded"
fi
exit 0
SH
chmod +x "$FAKE_BIN/curl"


PATH="$FAKE_BIN:$PATH" HOME="$TMP_HOME" "$ROOT/yai" install no-github-app 2>"$TMP_HOME/website_install.err"
test -s "$TMP_HOME/website-curl.log"
grep -q "website search for no-github-app" "$TMP_HOME/website_install.err"
grep -q "website search selected" "$TMP_HOME/website_install.err"
# The newer resolver queues well-known AppImageHub / GitLab / releases hint
# URLs in addition to the official download page, so the exact checked /
# queued / skipped counts are higher than the pre-hint baseline and may even
# vary a little when in-flight speculative fetches time out at the drain
# bound. Assert only that the fixture website.html → downloads.html →
# WebsiteFeed AppImage pipeline produced a sane, bounded result and that
# the download link itself was discovered.
checked_n="$(sed -n 's/.*after checking \([0-9]\+\) page(s).*/\1/p' "$TMP_HOME/website_install.err" | head -1)"
queued_n="$(sed -n 's/.*, queued \([0-9]\+\),.*/\1/p' "$TMP_HOME/website_install.err" | head -1)"
skipped_n="$(sed -n 's/.*, skipped \([0-9]\+\)$/\1/p' "$TMP_HOME/website_install.err" | head -1)"
test -n "$checked_n"
test -n "$queued_n"
test -n "$skipped_n"
test "$checked_n" -ge 2
test "$checked_n" -le 32
test "$queued_n" -ge 2
test "$skipped_n" -ge 0
grep -q "WebsiteFeed-x86_64.AppImage" "$TMP_HOME/website_install.err"
while IFS= read -r curl_args; do
  if [[ " $curl_args " != *" -o "* && " $curl_args " != *" --output "* ]]; then
    echo "website search full-text-fetched AppImage candidate" >&2
    exit 1
  fi
done < <(grep -F "$ORIGINAL_ROOT/$WEBSITE_ASSET" "$TMP_HOME/website-curl.log" || true)
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
"$ROOT/yai" install "file://$MUSESCORE_DOWNLOAD_PAGE_DIRECT" \
  --id musescore-direct \
  --name "MuseScore Direct" \
  2>"$TMP_HOME/musescore_direct_install.err"
grep -q "downloaded an AppImage landing page" "$TMP_HOME/musescore_direct_install.err"
grep -Fq "\"download_url\": \"file://$ORIGINAL_ROOT/$MUSESCORE_ASSET\"" "$TMP_HOME/.local/share/yai/apps/musescore-direct/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/musescore-direct" | grep -q "musescore app"
HOME="$TMP_HOME" "$ROOT/yai" remove musescore-direct

: > "$TMP_HOME/website-curl.log"
rm -f "$TMP_HOME/speculative-download-failed"
PATH="$FAKE_BIN:$PATH" HOME="$TMP_HOME" "$ROOT/yai" install musescore 2>"$TMP_HOME/musescore_install.err"
grep -q "website search selected" "$TMP_HOME/musescore_install.err"
grep -q "MuseScore-x86_64.AppImage" "$TMP_HOME/musescore_install.err"
test -e "$TMP_HOME/speculative-download-failed"
grep -F "$MUSESCORE_DOWNLOAD_URL" "$TMP_HOME/website-curl.log" | grep -q -- '--max-time 5'
grep -F "$MUSESCORE_DOWNLOAD_URL" "$TMP_HOME/website-curl.log" | grep -q -- '--max-time 15'
landing_probe_seen=false
while IFS= read -r curl_args; do
  if [[ " $curl_args " == *" -o "* || " $curl_args " == *" --output "* ]]; then
    continue
  fi
  landing_probe_seen=true
  if [[ " $curl_args " != *" --max-filesize "* && " $curl_args " != *" -r "* ]]; then
    echo "website search used an uncapped MuseScore landing probe" >&2
    exit 1
  fi
done < <(grep -F "$MUSESCORE_LANDING_URL" "$TMP_HOME/website-curl.log" || true)
if [[ "$landing_probe_seen" != true ]]; then
  echo "website search did not record a capped MuseScore landing probe" >&2
  exit 1
fi
while IFS= read -r curl_args; do
  if [[ " $curl_args " != *" -o "* && " $curl_args " != *" --output "* ]]; then
    echo "website search full-text-fetched MuseScore AppImage asset" >&2
    exit 1
  fi
done < <(grep -F "$ORIGINAL_ROOT/$MUSESCORE_ASSET" "$TMP_HOME/website-curl.log" || true)
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
: > "$TMP_HOME/website-curl.log"
PATH="$FAKE_BIN:$PATH" HOME="$TMP_HOME" "$ROOT/yai" update musescore >"$TMP_HOME/musescore_update.out"
grep -q $'musescore\tMuseScore-x86_64.AppImage\tMuseScore-v2-x86_64.AppImage\tupgradable' "$TMP_HOME/musescore_update.out"
PATH="$FAKE_BIN:$PATH" HOME="$TMP_HOME" "$ROOT/yai" upgrade musescore 2>"$TMP_HOME/musescore_upgrade.err"
grep -q "website search selected" "$TMP_HOME/musescore_upgrade.err"
grep -q "MuseScore-v2-x86_64.AppImage" "$TMP_HOME/musescore_upgrade.err"
grep -Fq "\"download_url\": \"file://$ORIGINAL_ROOT/$MUSESCORE_V2_ASSET\"" "$TMP_HOME/.local/share/yai/apps/musescore/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/musescore" | grep -q "musescore v2 app"
HOME="$TMP_HOME" "$ROOT/yai" remove musescore

echo "appimage feed smoke test passed"
