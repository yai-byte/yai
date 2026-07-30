#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
trap 'rm -rf "$TMP_HOME"' EXIT

mkdir -p "$TMP_HOME/home" "$TMP_HOME/bin" "$TMP_HOME/assets"
FAKE_BIN="$TMP_HOME/bin"
REAL_CURL="$(command -v curl)"

# --- Fake AppImage assets ----------------------------------------------------

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

make_appimage "$TMP_HOME/assets/data_direct_x86_64.AppImage" "data direct x86_64"
make_appimage "$TMP_HOME/assets/data_gh_x86_64.AppImage"      "data gh x86_64"
make_appimage "$TMP_HOME/assets/apps_direct_x86_64.AppImage"  "apps direct x86_64"

DATA_DIRECT_URL="file://$TMP_HOME/assets/data_direct_x86_64.AppImage"
DATA_GH_URL="file://$TMP_HOME/assets/data_gh_x86_64.AppImage"
APPS_DIRECT_URL="file://$TMP_HOME/assets/apps_direct_x86_64.AppImage"

# --- Fixture content ---------------------------------------------------------

# data/data-direct-pkg: direct download URL as primary non-comment line
DATA_DIRECT_CONTENT="${DATA_DIRECT_URL}
"

# data/data-gh-pkg: GitHub repo URL only (in comment); no direct download
# The comment URL is a GitHub repo page, not a download URL.
DATA_GH_CONTENT="# https://github.com/acme/data-gh-pkg
https://example.com/unrelated"

# apps/<Name>.md content with YAML frontmatter
DATA_DIRECT_MD='---
name: data-direct-pkg
description: Package with direct URL from data/
license: MIT
links:
  - type: Download
    url: '"$DATA_DIRECT_URL"'
  - type: Homepage
    url: https://example.com/data-direct
---

# data-direct-pkg

Test fixture
'

DATA_GH_MD='---
name: data-gh-pkg
description: Package with GitHub repo from data/
license: MIT
links:
  - type: GitHub
    url: acme/data-gh-pkg
  - type: Homepage
    url: https://example.com/data-gh
---

# data-gh-pkg

Test fixture
'

APPS_DIRECT_MD='---
name: apps-direct-pkg
description: Package with direct URL from apps/
license: GPL-3.0
links:
  - type: Download
    url: '"$APPS_DIRECT_URL"'
  - type: Homepage
    url: https://example.com/apps-direct
---

# apps-direct-pkg

Test fixture
'

# GitHub API /contents/apps/<Name>.md response (with base64 content)
encode_b64() {
  printf '%s' "$1" | base64 -w0
}

API_DATA_DIRECT_BODY='{"name":"data-direct-pkg.md","content":"'$(encode_b64 "$DATA_DIRECT_MD")'","encoding":"base64"}'
API_DATA_GH_BODY='{"name":"data-gh-pkg.md","content":"'$(encode_b64 "$DATA_GH_MD")'","encoding":"base64"}'
API_APPS_DIRECT_BODY='{"name":"apps-direct-pkg.md","content":"'$(encode_b64 "$APPS_DIRECT_MD")'","encoding":"base64"}'
GH_RELEASE_BODY=$(cat <<EOF
{"tag_name":"v1.0.0","assets":[{"name":"data_gh_x86_64.AppImage","browser_download_url":"$DATA_GH_URL"}]}
EOF
)

export DATA_DIRECT_CONTENT DATA_GH_CONTENT DATA_DIRECT_MD DATA_GH_MD APPS_DIRECT_MD
export API_DATA_DIRECT_BODY API_DATA_GH_BODY API_APPS_DIRECT_BODY GH_RELEASE_BODY
export TMP_HOME REAL_CURL

# --- Fake curl: intercepts AppImage GitHub + data/ URLs ----------------------

cat > "$FAKE_BIN/curl" <<SH
#!/usr/bin/env bash
echo "FAKE_CURL_CALLED" >> "$TMP_HOME/fake-curl-debug.log"
echo "FAKE_CURL_PATH=$PATH" >> "$TMP_HOME/fake-curl-debug.log"
args=("\$@")
url=""
for ((i=0; i<\${#args[@]}; i++)); do
  a="\${args[i]}"
  echo "FAKE_CURL_ARG: \$a" >> "$TMP_HOME/fake-curl.log"
  case "\$a" in
    *raw.githubusercontent.com/AppImage/appimage.github.io/master/data/data-direct*)
      url="DATA_DIRECT" ;;
    *raw.githubusercontent.com/AppImage/appimage.github.io/master/data/data-gh*)
      url="DATA_GH" ;;
    *raw.githubusercontent.com/AppImage/appimage.github.io/master/apps/data-direct*)
      url="MD_DATA_DIRECT" ;;
    *raw.githubusercontent.com/AppImage/appimage.github.io/master/apps/data-gh*)
      url="MD_DATA_GH" ;;
    *raw.githubusercontent.com/AppImage/appimage.github.io/master/apps/apps-direct*)
      url="MD_APPS_DIRECT" ;;
    *api.github.com/repos/AppImage/appimage.github.io/contents/apps/data-direct*)
      url="API_DATA_DIRECT" ;;
    *api.github.com/repos/AppImage/appimage.github.io/contents/apps/data-gh*)
      url="API_DATA_GH" ;;
    *api.github.com/repos/AppImage/appimage.github.io/contents/apps/apps-direct*)
      url="API_APPS_DIRECT" ;;
    *api.github.com/repos/acme/data-gh-pkg/releases/latest*)
      url="GH_RELEASE" ;;
  esac
done
echo "FAKE_CURL_MATCH: \$url" >> "$TMP_HOME/fake-curl.log"
case "\$url" in
  DATA_DIRECT) printf '%s' "\$DATA_DIRECT_CONTENT"
               exit 0 ;;
  DATA_GH)     printf '%s' "\$DATA_GH_CONTENT"
               exit 0 ;;
  MD_DATA_DIRECT) printf '%s' "\$DATA_DIRECT_MD"
                  exit 0 ;;
  MD_DATA_GH)     printf '%s' "\$DATA_GH_MD"
                  exit 0 ;;
  MD_APPS_DIRECT) printf '%s' "\$APPS_DIRECT_MD"
                  exit 0 ;;
  API_DATA_DIRECT)  printf '%s' "\$API_DATA_DIRECT_BODY"
                     exit 0 ;;
  API_DATA_GH)      printf '%s' "\$API_DATA_GH_BODY"
                     exit 0 ;;
  API_APPS_DIRECT)  printf '%s' "\$API_APPS_DIRECT_BODY"
                     exit 0 ;;
  GH_RELEASE)       printf '%s' "\$GH_RELEASE_BODY"
                     exit 0 ;;
esac

echo "FAKE_CURL_FALLBACK" >> "$TMP_HOME/fake-curl.log"
exec "$REAL_CURL" "\$@"
SH

chmod +x "$FAKE_BIN/curl"
export PATH="$FAKE_BIN:$PATH"

# --- Debug: log all curl invocations --------------------------------------

DEBUG_LOG="$TMP_HOME/curl-debug.log"
: > "$DEBUG_LOG"

# Wrap curl to log invocations
mv "$FAKE_BIN/curl" "$FAKE_BIN/curl-real"
cat > "$FAKE_BIN/curl" <<WRAP
#!/usr/bin/env bash
echo "WRAP_CURL_CALLED" >> "$TMP_HOME/wrapper-debug.log"
printf 'CURL:' >> "$DEBUG_LOG"
for a in "\$@"; do printf ' [%s]' "\$a" >> "$DEBUG_LOG"; done
printf '\n' >> "$DEBUG_LOG"
exec "$FAKE_BIN/curl-real" "\$@"
WRAP
chmod +x "$FAKE_BIN/curl"

# --- Build a local index with packages that lack primary download -------------

INDEX_JSON="$TMP_HOME/home/.local/share/yai/repos/index.json"
mkdir -p "$(dirname "$INDEX_JSON")"
cat > "$INDEX_JSON" <<JSON
{
  "schema_version": 1,
  "updated_at": "test",
  "packages": [
    {
      "id": "data-direct-pkg",
      "name": "data-direct-pkg",
      "summary": "Has direct download URL in data/",
      "homepage": "https://example.com/data-direct",
      "license": "MIT",
      "source": {
        "type": "unavailable",
        "reason": "needs data/ fallback"
      }
    },
    {
      "id": "data-gh-pkg",
      "name": "data-gh-pkg",
      "summary": "Has GitHub repo in data/",
      "homepage": "https://example.com/data-gh",
      "license": "MIT",
      "source": {
        "type": "unavailable",
        "reason": "needs data/ GitHub repo fallback"
      }
    },
    {
      "id": "apps-direct-pkg",
      "name": "apps-direct-pkg",
      "summary": "Has direct download URL in apps/",
      "homepage": "https://example.com/apps-direct",
      "license": "GPL-3.0",
      "source": {
        "type": "unavailable",
        "reason": "needs apps/ fallback"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME/home" "$ROOT/yai" install data-direct-pkg --dry-run >/dev/null 2>/dev/null || true
HOME="$TMP_HOME/home" "$ROOT/yai" install data-gh-pkg --dry-run >/dev/null 2>/dev/null || true
HOME="$TMP_HOME/home" "$ROOT/yai" install apps-direct-pkg --dry-run >/dev/null 2>/dev/null || true

# --- Test 1: data/ fallback provides a direct download URL -------------------

HOME="$TMP_HOME/home" "$ROOT/yai" install data-direct-pkg \
  >"$TMP_HOME/data-direct.out" 2>"$TMP_HOME/data-direct.err" || {
  echo "data/ direct fallback install failed:" >&2
  cat "$TMP_HOME/data-direct.out" "$TMP_HOME/data-direct.err" >&2
  echo "=== CURL DEBUG ==="
  cat "$DEBUG_LOG" >&2 || true
  exit 1
}
test -x "$TMP_HOME/home/.local/bin/data-direct-pkg" || {
  echo "data/ direct: install succeeded but binary not at ~/.local/bin" >&2
  ls -la "$TMP_HOME/home/.local/bin/" >&2 || true
  exit 1
}
bash "$TMP_HOME/home/.local/bin/data-direct-pkg" | grep -q "data direct x86_64"
HOME="$TMP_HOME/home" "$ROOT/yai" remove data-direct-pkg

grep -Fq "trying AppImage GitHub data/ lookup" "$TMP_HOME/data-direct.err" || {
  echo "data/ direct fallback: expected data/ lookup log line" >&2
  exit 1
}
grep -Fq "found direct download URL in data/" "$TMP_HOME/data-direct.err" || {
  echo "data/ direct fallback: expected 'found direct download URL' log" >&2
  exit 1
}

# --- Test 2: data/ fallback provides a GitHub repo for GitHub release -------

HOME="$TMP_HOME/home" "$ROOT/yai" install data-gh-pkg \
  >"$TMP_HOME/data-gh.out" 2>"$TMP_HOME/data-gh.err" || {
  echo "data/ GitHub fallback install failed:" >&2
  cat "$TMP_HOME/data-gh.out" "$TMP_HOME/data-gh.err" >&2
  echo "=== CURL DEBUG ==="
  cat "$DEBUG_LOG" >&2 || true
  exit 1
}
test -x "$TMP_HOME/home/.local/bin/data-gh-pkg" || {
  echo "data/ gh: install succeeded but binary not at ~/.local/bin" >&2
  ls -la "$TMP_HOME/home/.local/bin/" >&2 || true
  exit 1
}
bash "$TMP_HOME/home/.local/bin/data-gh-pkg" | grep -q "data gh x86_64"
HOME="$TMP_HOME/home" "$ROOT/yai" remove data-gh-pkg

grep -Fq "trying AppImage GitHub data/ lookup" "$TMP_HOME/data-gh.err" || {
  echo "data/ GitHub fallback: expected data/ lookup log" >&2
  cat "$TMP_HOME/data-gh.err" >&2
  exit 1
}
grep -Fq "found GitHub repo in data/" "$TMP_HOME/data-gh.err" || {
  echo "data/ GitHub fallback: expected 'found GitHub repo in data/' log" >&2
  echo "=== data-gh.err ===" >&2
  cat "$TMP_HOME/data-gh.err" >&2
  echo "=== data-gh.out ===" >&2
  cat "$TMP_HOME/data-gh.out" >&2
  echo "=== CURL DEBUG ===" >&2
  cat "$DEBUG_LOG" >&2 || true
  exit 1
}

# --- Test 3: apps/ fallback provides a direct download URL ------------------

HOME="$TMP_HOME/home" "$ROOT/yai" install apps-direct-pkg \
  >"$TMP_HOME/apps-direct.out" 2>"$TMP_HOME/apps-direct.err" || {
  echo "apps/ fallback install failed:" >&2
  cat "$TMP_HOME/apps-direct.out" "$TMP_HOME/apps-direct.err" >&2
  exit 1
}
test -x "$TMP_HOME/home/.local/bin/apps-direct-pkg" || {
  echo "apps/: install succeeded but binary not at ~/.local/bin" >&2
  ls -la "$TMP_HOME/home/.local/bin/" >&2 || true
  exit 1
}
bash "$TMP_HOME/home/.local/bin/apps-direct-pkg" | grep -q "apps direct x86_64"
HOME="$TMP_HOME/home" "$ROOT/yai" remove apps-direct-pkg

grep -Fq "trying AppImage GitHub data/ lookup" "$TMP_HOME/apps-direct.err" || {
  echo "apps/ fallback: expected data/ lookup log (first fallback attempted)" >&2
  exit 1
}
grep -Fq "trying AppImage GitHub apps/ lookup" "$TMP_HOME/apps-direct.err" || {
  echo "apps/ fallback: expected apps/ lookup log" >&2
  exit 1
}
grep -Fq "found direct download URL in apps/" "$TMP_HOME/apps-direct.err" || {
  echo "apps/ fallback: expected 'found direct download URL in apps/' log" >&2
  exit 1
}

echo "appimage data/apps resolve smoke passed"
