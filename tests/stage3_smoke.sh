#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
API_ROOT="$TMP_HOME/api"
MIRROR_ROOT="$TMP_HOME/mirror"
ORIGINAL_ROOT="$TMP_HOME/original"
ASSET="Demo-x86_64.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

mkdir -p "$API_ROOT/repos/acme/demo/releases" "$MIRROR_ROOT" "$ORIGINAL_ROOT"

cat > "$MIRROR_ROOT/$ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "demo 1.2.3"
  exit 0
fi
echo "demo app"
APP
chmod +x "$MIRROR_ROOT/$ASSET"

cat > "$ORIGINAL_ROOT/Demo-arm64.AppImage" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "demo arm64 1.2.3"
  exit 0
fi
echo "demo arm64 app"
APP
chmod +x "$ORIGINAL_ROOT/Demo-arm64.AppImage"

cat > "$ORIGINAL_ROOT/Demo-riscv64.AppImage" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "demo riscv64 1.2.3"
  exit 0
fi
echo "demo riscv64 app"
APP
chmod +x "$ORIGINAL_ROOT/Demo-riscv64.AppImage"

cat > "$API_ROOT/repos/acme/demo/releases/latest" <<JSON
{
  "tag_name": "v1.2.3",
  "assets": [
    {
      "name": "Demo-arm64.AppImage",
      "browser_download_url": "file://$ORIGINAL_ROOT/Demo-arm64.AppImage"
    },
    {
      "name": "Demo-riscv64.AppImage",
      "browser_download_url": "file://$ORIGINAL_ROOT/Demo-riscv64.AppImage"
    },
    {
      "name": "$ASSET",
      "browser_download_url": "file://$ORIGINAL_ROOT/$ASSET"
    }
  ]
}
JSON

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install acme/demo \
  --download mirror_first \
  --mirror-template "file://$MIRROR_ROOT/{asset}"

META="$TMP_HOME/.local/share/yai/apps/demo/metadata.json"
grep -q '"source_kind": "github_release"' "$META"
grep -q '"version": "v1.2.3"' "$META"
grep -q '"github_owner": "acme"' "$META"
grep -q '"github_repo": "demo"' "$META"
grep -Fq "\"github_asset\": \"$ASSET\"" "$META"
grep -Fq "\"source_url\": \"file://$ORIGINAL_ROOT/$ASSET\"" "$META"
grep -Fq "\"download_url\": \"file://$MIRROR_ROOT/$ASSET\"" "$META"

HOME="$TMP_HOME" "$TMP_HOME/.local/bin/demo" | grep -q "demo app"
HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "demo"
HOME="$TMP_HOME" "$ROOT/yai" remove demo

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install acme/demo --arch arm64 --id demo-arm64

ARM_META="$TMP_HOME/.local/share/yai/apps/demo-arm64/metadata.json"
grep -Fq '"github_asset": "Demo-arm64.AppImage"' "$ARM_META"
grep -Fq "\"source_url\": \"file://$ORIGINAL_ROOT/Demo-arm64.AppImage\"" "$ARM_META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/demo-arm64" | grep -q "demo arm64 app"
HOME="$TMP_HOME" "$ROOT/yai" remove demo-arm64

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install acme/demo --arch riscv64 --id demo-riscv64

RISCV_META="$TMP_HOME/.local/share/yai/apps/demo-riscv64/metadata.json"
grep -Fq '"arch": "riscv64"' "$RISCV_META"
grep -Fq '"github_asset": "Demo-riscv64.AppImage"' "$RISCV_META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/demo-riscv64" | grep -q "demo riscv64 app"
HOME="$TMP_HOME" "$ROOT/yai" remove demo-riscv64

if HOME="$TMP_HOME" "$ROOT/yai" install acme/demo --arch mips64 2>"$TMP_HOME/bad_arch.err"; then
  echo "install accepted unsupported --arch value" >&2
  exit 1
fi
grep -q -- "--arch must be auto, x86_64, aarch64, x86, armv7, riscv64, ppc64le, s390x, or loongarch64" "$TMP_HOME/bad_arch.err"

echo "stage3 smoke test passed"
