#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
API_ROOT="$TMP_HOME/api"
MIRROR_ROOT="$TMP_HOME/mirror"
ORIGINAL_ROOT="$TMP_HOME/original"
ASSET="MirrorDemo-x86_64.AppImage"
NOSCHEME_ASSET="NoschemeDemo-x86_64.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

mkdir -p "$API_ROOT/repos/acme/mirror-demo/releases" "$API_ROOT/repos/acme/noscheme-demo/releases" "$MIRROR_ROOT" "$ORIGINAL_ROOT"

cat > "$MIRROR_ROOT/$ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "mirror demo 1.0.0"
  exit 0
fi
echo "mirror demo app"
APP
chmod +x "$MIRROR_ROOT/$ASSET"

mkdir -p "$MIRROR_ROOT/github.com/acme/noscheme-demo/releases/download/v1.0.0"
cat > "$MIRROR_ROOT/github.com/acme/noscheme-demo/releases/download/v1.0.0/$NOSCHEME_ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "noscheme demo 1.0.0"
  exit 0
fi
echo "noscheme demo app"
APP
chmod +x "$MIRROR_ROOT/github.com/acme/noscheme-demo/releases/download/v1.0.0/$NOSCHEME_ASSET"

cat > "$API_ROOT/repos/acme/mirror-demo/releases/latest" <<JSON
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

cat > "$API_ROOT/repos/acme/noscheme-demo/releases/latest" <<JSON
{
  "tag_name": "v1.0.0",
  "assets": [
    {
      "name": "$NOSCHEME_ASSET",
      "browser_download_url": "https://github.com/acme/noscheme-demo/releases/download/v1.0.0/$NOSCHEME_ASSET"
    }
  ]
}
JSON

HOME="$TMP_HOME" "$ROOT/yai" mirror custom "file://$MIRROR_ROOT/{asset}"
HOME="$TMP_HOME" "$ROOT/yai" mirror list | grep -q "custom"
HOME="$TMP_HOME" "$ROOT/yai" mirror list | grep -q "Strategy: mirror_first"
HOME="$TMP_HOME" "$ROOT/yai" mirror list | grep -q "chengc.*https://github.chenc.dev/{raw_url_noscheme}"

mkdir -p "$TMP_HOME/.config/yai"
cat > "$TMP_HOME/.config/yai/network.conf" <<'CONF'
china_network_prompted=1
provider=chengc
download_strategy=mirror_first
mirror_template=https://gh.chengcheng.de/{raw_url}
CONF
HOME="$TMP_HOME" "$ROOT/yai" mirror list | grep -q "Template: https://github.chenc.dev/{raw_url_noscheme}"
HOME="$TMP_HOME" "$ROOT/yai" mirror custom "file://$MIRROR_ROOT/{asset}"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install acme/mirror-demo

META="$TMP_HOME/.local/share/yai/apps/mirror-demo/metadata.json"
grep -Fq "\"download_url\": \"file://$MIRROR_ROOT/$ASSET\"" "$META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/mirror-demo" | grep -q "mirror demo app"
HOME="$TMP_HOME" "$ROOT/yai" remove mirror-demo

HOME="$TMP_HOME" "$ROOT/yai" mirror custom "file://$MIRROR_ROOT/{raw_url_noscheme}"
HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install acme/noscheme-demo

NOSCHEME_META="$TMP_HOME/.local/share/yai/apps/noscheme-demo/metadata.json"
grep -Fq "\"download_url\": \"file://$MIRROR_ROOT/github.com/acme/noscheme-demo/releases/download/v1.0.0/$NOSCHEME_ASSET\"" "$NOSCHEME_META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/noscheme-demo" | grep -q "noscheme demo app"
HOME="$TMP_HOME" "$ROOT/yai" remove noscheme-demo

HOME="$TMP_HOME" "$ROOT/yai" mirror off
HOME="$TMP_HOME" "$ROOT/yai" mirror list | grep -q "Current: direct"

mkdir -p "$TMP_HOME/.config/yai"
echo "acme/blocked" > "$TMP_HOME/.config/yai/github_blocklist.conf"
if HOME="$TMP_HOME" YAI_GITHUB_API_BASE="file://$API_ROOT" "$ROOT/yai" install acme/blocked 2>"$TMP_HOME/blocked.err"; then
  echo "expected blocked repo install to fail" >&2
  exit 1
fi
grep -q "451 Unavailable For Legal Reasons" "$TMP_HOME/blocked.err"

echo "mirror policy smoke test passed"
