#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
trap 'rm -rf "$TMP_HOME"' EXIT
FAKE_BIN="$TMP_HOME/bin"
mkdir -p "$FAKE_BIN"
REAL_CURL="$(command -v curl)"
# Log argv; hang for timeout coverage and stream an oversized response for the
# limited-fetch hard-cap coverage.
cat > "$FAKE_BIN/curl" <<'SH'
#!/usr/bin/env bash
printf 'curl\t%s\n' "$*" >> "${FAKE_CURL_LOG:?}"
has_output=false
for arg in "$@"; do
  if [[ "$arg" == "--output" || "$arg" == "-o" ]]; then
    has_output=true
  fi
  if [[ "$arg" == *"/hang"* ]]; then
    sleep 30
    exit 0
  fi
done
if [[ "$*" == *"/oversized-limit-site.AppImage"* && "$has_output" == false ]]; then
  printf '%s\n' "$$" > "${FAKE_OVERSIZED_PID:?}"
  chunk="$(printf 'x%.0s' {1..4096})"
  for _ in {1..513}; do
    printf '%s' "$chunk"
  done
  sleep 30
  exit 0
fi
if [[ "$*" == *"https://download.example.org/index"* ]]; then
  printf '%s\n' \
    '<a href="/download-one">One</a><a href="/download-two">Two</a><a href="/download-three">Three</a><a href="/about">About</a><a href="/linux-special">Linux</a>'
  exit 0
fi
if [[ "$*" == *"download.example.org"* ]]; then
  printf '%s\n' '<html><body></body></html>'
  exit 0
fi
exec "${REAL_CURL:?}" "$@"
SH
chmod +x "$FAKE_BIN/curl"
export PATH="$FAKE_BIN:$PATH"
export FAKE_CURL_LOG="$TMP_HOME/curl.log"
export FAKE_OVERSIZED_PID="$TMP_HOME/oversized.pid"
export REAL_CURL
# Drive fetch via a tiny helper binary is heavy; instead assert yai website path
# uses --max-time by installing a website_page package whose only page hangs.
# Simpler: unit-level check that compiled yai passes --max-time when resolving.
# Use repo package pointing at https://example.invalid/hang (fake curl sleeps).
mkdir -p "$TMP_HOME/.local/share/yai/repos"
cat > "$TMP_HOME/.local/share/yai/repos/index.json" <<'JSON'
{
  "schema_version": 1,
  "updated_at": "test",
  "packages": [
    {
      "id": "hang-site",
      "name": "Hang Site",
      "summary": "timeout probe",
      "homepage": "https://example.invalid/hang",
      "license": "MIT",
      "source": {
        "type": "website_page",
        "url": "https://example.invalid/hang",
        "reason": "test"
      }
    },
    {
        "id": "limit-site",
        "name": "Limit Site",
        "summary": "hard output cap probe",
        "homepage": "file://OVERSIZED_PAGE",
        "license": "MIT",
        "source": {
          "type": "website_page",
          "url": "file://OVERSIZED_PAGE",
          "reason": "test"
        }
      },
      {
        "id": "priority-site",
        "name": "Priority Site",
        "summary": "URL path priority probe",
        "homepage": "https://download.example.org/index",
        "license": "MIT",
        "source": {
          "type": "website_page",
          "url": "https://download.example.org/index",
          "reason": "test"
        }
      }
  ]
}
JSON
OVERSIZED_PAGE="$TMP_HOME/oversized.html"
cat > "$OVERSIZED_PAGE" <<'HTML'
<html><body>
<a href="https://example.invalid/download/oversized-limit-site.AppImage">Download</a>
</body></html>
HTML
sed -i "s|OVERSIZED_PAGE|$OVERSIZED_PAGE|g" "$TMP_HOME/.local/share/yai/repos/index.json"
# Ensure config lists default repo so index is used — match how other smokes set HOME-only index.
# If hang-site install fails quickly (timeout/skip then "no AppImage"), that proves timeout works.
#
# fetch_text_limited (--max-filesize / -r): no Task-1 call path wires capped landing probes yet.
# Size-cap flags are covered when Task 2 wires resolved_appimage_candidate / landing fetch.
start_ts=$SECONDS
set +e
HOME="$TMP_HOME" "$ROOT/yai" install hang-site 2>"$TMP_HOME/err"
code=$?
set -e
elapsed=$((SECONDS - start_ts))
grep -q -- '--max-time' "$FAKE_CURL_LOG"
test "$code" -ne 0
if (( elapsed >= 20 )); then
  echo "FAIL: install took ${elapsed}s (expected wall clock < 20s)" >&2
  exit 1
fi

start_ts=$SECONDS
set +e
HOME="$TMP_HOME" "$ROOT/yai" install limit-site 2>"$TMP_HOME/limit.err"
limit_code=$?
set -e
limit_elapsed=$((SECONDS - start_ts))
test "$limit_code" -ne 0
test -s "$FAKE_OVERSIZED_PID"
if (( limit_elapsed >= 5 )); then
  echo "FAIL: limited fetch took ${limit_elapsed}s after exceeding 512 KiB" >&2
  exit 1
fi
if kill -0 "$(cat "$FAKE_OVERSIZED_PID")" 2>/dev/null; then
  echo "FAIL: oversized limited-fetch process is still running" >&2
  exit 1
fi

: > "$FAKE_CURL_LOG"
set +e
HOME="$TMP_HOME" "$ROOT/yai" install priority-site 2>"$TMP_HOME/priority.err"
priority_code=$?
set -e
test "$priority_code" -ne 0
about_line="$(grep -nF 'https://download.example.org/about' "$FAKE_CURL_LOG" | cut -d: -f1)"
linux_line="$(grep -nF 'https://download.example.org/linux-special' "$FAKE_CURL_LOG" | cut -d: -f1)"
test -n "$about_line"
test -n "$linux_line"
if (( linux_line >= about_line )); then
  echo "FAIL: URL host affected website path priority" >&2
  exit 1
fi
