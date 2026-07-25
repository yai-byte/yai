#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
API_ROOT="$TMP_HOME/api"
ORIGINAL_ROOT="$TMP_HOME/original"
INDEX="$TMP_HOME/index.json"
LATEST="$API_ROOT/repos/acme/updatable/releases/latest"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

mkdir -p "$(dirname "$LATEST")" "$ORIGINAL_ROOT"

write_appimage() {
  local path="$1"
  local label="$2"
  cat > "$path" <<APP
#!/usr/bin/env bash
if [[ "\${1:-}" == "--appimage-version" ]]; then
  echo "$label"
  exit 0
fi
echo "$label app"
APP
  chmod +x "$path"
}

write_bad_appimage() {
  local path="$1"
  cat > "$path" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" || "${1:-}" == "--appimage-extract" ]]; then
  echo "broken appimage" >&2
  exit 1
fi
echo "broken app"
exit 1
APP
  chmod +x "$path"
}

write_latest() {
  local tag="$1"
  local asset="$2"
  cat > "$LATEST" <<JSON
{
  "tag_name": "$tag",
  "assets": [
    {
      "name": "$asset",
      "browser_download_url": "file://$ORIGINAL_ROOT/$asset"
    }
  ]
}
JSON
}

write_latest_pair() {
  local tag="$1"
  local first_asset="$2"
  local second_asset="$3"
  cat > "$LATEST" <<JSON
{
  "tag_name": "$tag",
  "assets": [
    {
      "name": "$first_asset",
      "browser_download_url": "file://$ORIGINAL_ROOT/$first_asset"
    },
    {
      "name": "$second_asset",
      "browser_download_url": "file://$ORIGINAL_ROOT/$second_asset"
    }
  ]
}
JSON
}

cat > "$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "updatable",
      "name": "Updatable Demo",
      "summary": "Stage five update demo",
      "homepage": "https://example.com/updatable",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "updatable",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    }
  ]
}
JSON

write_appimage "$ORIGINAL_ROOT/Updatable-v1-x86_64.AppImage" "v1.0.0"
write_latest "v1.0.0" "Updatable-v1-x86_64.AppImage"

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install updatable

HOME="$TMP_HOME" "$TMP_HOME/.local/bin/updatable" | grep -q "v1.0.0 app"

write_appimage "$ORIGINAL_ROOT/LocalNoUpdate.AppImage" "local-no-update"
HOME="$TMP_HOME" "$ROOT/yai" install "$ORIGINAL_ROOT/LocalNoUpdate.AppImage" --id local-no-update
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/local-no-update" | grep -q "local-no-update app"

write_appimage "$ORIGINAL_ROOT/Updatable-v2-x86_64.AppImage" "v2.0.0"
write_latest "v2.0.0" "Updatable-v2-x86_64.AppImage"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" update > "$TMP_HOME/update_all_preview.out"

grep -q $'updatable\tv1.0.0\tv2.0.0\tupgradable' "$TMP_HOME/update_all_preview.out"
grep -q $'local-no-update\t-\t-\tunsupported\tlocal AppImage path has no queryable update source' "$TMP_HOME/update_all_preview.out"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" update 'updat*' > "$TMP_HOME/update_preview.out"

grep -q $'updatable\tv1.0.0\tv2.0.0\tupgradable' "$TMP_HOME/update_preview.out"
test ! -e "$TMP_HOME/.local/share/yai/apps/updatable/versions/previous"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/updatable" | grep -q "v1.0.0 app"

printf 'n\n' | HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" upgrade --all > "$TMP_HOME/batch_cancel.out" 2> "$TMP_HOME/batch_cancel.err"

grep -q "Upgrade cancelled" "$TMP_HOME/batch_cancel.out"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/updatable" | grep -q "v1.0.0 app"
test ! -e "$TMP_HOME/.local/share/yai/apps/updatable/versions/previous"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" upgrade --all --yes

META="$TMP_HOME/.local/share/yai/apps/updatable/metadata.json"
PREVIOUS="$TMP_HOME/.local/share/yai/apps/updatable/versions/previous"
grep -q '"version": "v2.0.0"' "$META"
test -f "$PREVIOUS/current.AppImage"
test -f "$PREVIOUS/metadata.json"
grep -q '"version": "v1.0.0"' "$PREVIOUS/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/updatable" | grep -q "v2.0.0 app"

write_bad_appimage "$ORIGINAL_ROOT/Updatable-v3-x86_64.AppImage"
write_latest "v3.0.0" "Updatable-v3-x86_64.AppImage"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" update 'updat*' > "$TMP_HOME/broken_preview.out"

grep -q $'updatable\tv2.0.0\tv3.0.0\tupgradable' "$TMP_HOME/broken_preview.out"
grep -q '"version": "v2.0.0"' "$META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/updatable" | grep -q "v2.0.0 app"

if HOME="$TMP_HOME" YAI_GITHUB_API_BASE="file://$API_ROOT" "$ROOT/yai" upgrade 'updat*'; then
  echo "expected broken upgrade to fail" >&2
  exit 1
fi
grep -q '"version": "v2.0.0"' "$META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/updatable" | grep -q "v2.0.0 app"

HOME="$TMP_HOME" "$ROOT/yai" rollback 'updat*'
grep -q '"version": "v1.0.0"' "$META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/updatable" | grep -q "v1.0.0 app"

HOME="$TMP_HOME" "$ROOT/yai" remove 'updat*'
HOME="$TMP_HOME" "$ROOT/yai" remove local-no-update

write_appimage "$ORIGINAL_ROOT/ArchUpdatable-v1-arm64.AppImage" "arm64-v1"
write_latest "v10.0.0" "ArchUpdatable-v1-arm64.AppImage"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install acme/updatable --arch arm64 --id arch-updatable

ARCH_META="$TMP_HOME/.local/share/yai/apps/arch-updatable/metadata.json"
grep -q '"arch": "aarch64"' "$ARCH_META"
grep -q '"github_asset": "ArchUpdatable-v1-arm64.AppImage"' "$ARCH_META"

write_appimage "$ORIGINAL_ROOT/ArchUpdatable-v2-x86_64.AppImage" "x86_64-v2"
write_appimage "$ORIGINAL_ROOT/ArchUpdatable-v2-arm64.AppImage" "arm64-v2"
write_latest_pair "v11.0.0" "ArchUpdatable-v2-x86_64.AppImage" "ArchUpdatable-v2-arm64.AppImage"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" upgrade 'arch-*'

grep -q '"arch": "aarch64"' "$ARCH_META"
grep -q '"github_asset": "ArchUpdatable-v2-arm64.AppImage"' "$ARCH_META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/arch-updatable" | grep -q "arm64-v2 app"
HOME="$TMP_HOME" "$ROOT/yai" remove 'arch-*'

write_appimage "$ORIGINAL_ROOT/DirectUpdatable-v1-x86_64.AppImage" "direct-v1"
cat > "$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "direct-updatable",
      "name": "Direct Updatable",
      "summary": "Direct URL update demo",
      "homepage": "https://example.com/direct-updatable",
      "license": "Unknown",
      "source": {
        "type": "direct_url",
        "url": "file://$ORIGINAL_ROOT/DirectUpdatable-v1-x86_64.AppImage"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" install direct-updatable

DIRECT_META="$TMP_HOME/.local/share/yai/apps/direct-updatable/metadata.json"
grep -q '"source_kind": "repo_direct_url"' "$DIRECT_META"
grep -q '"version": "DirectUpdatable-v1-x86_64.AppImage"' "$DIRECT_META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/direct-updatable" | grep -q "direct-v1 app"

write_appimage "$ORIGINAL_ROOT/DirectUpdatable-v2-x86_64.AppImage" "direct-v2"
cat > "$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-21T00:00:00Z",
  "packages": [
    {
      "id": "direct-updatable",
      "name": "Direct Updatable",
      "summary": "Direct URL update demo",
      "homepage": "https://example.com/direct-updatable",
      "license": "Unknown",
      "source": {
        "type": "direct_url",
        "url": "file://$ORIGINAL_ROOT/DirectUpdatable-v2-x86_64.AppImage"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" update direct-updatable > "$TMP_HOME/direct_update_preview.out"

grep -q $'direct-updatable\tDirectUpdatable-v1-x86_64.AppImage\tDirectUpdatable-v2-x86_64.AppImage\tupgradable' "$TMP_HOME/direct_update_preview.out"
test ! -e "$TMP_HOME/.local/share/yai/apps/direct-updatable/versions/previous"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/direct-updatable" | grep -q "direct-v1 app"

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" upgrade direct-updatable

grep -q '"version": "DirectUpdatable-v2-x86_64.AppImage"' "$DIRECT_META"
grep -q '"version": "DirectUpdatable-v1-x86_64.AppImage"' "$TMP_HOME/.local/share/yai/apps/direct-updatable/versions/previous/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/direct-updatable" | grep -q "direct-v2 app"
HOME="$TMP_HOME" "$ROOT/yai" remove direct-updatable

echo "stage5 smoke test passed"
