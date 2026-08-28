#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

APP_DIR="$TMP_HOME/.local/share/yai/apps/legacy-app"
mkdir -p "$APP_DIR"

cat > "$APP_DIR/current.AppImage" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "legacy v1"
  exit 0
fi
echo "legacy app"
APP
chmod +x "$APP_DIR/current.AppImage"

cat > "$APP_DIR/metadata.json" <<META
{
  "id": "legacy-app",
  "name": "Legacy App",
  "version": "v1",
  "install_mode": "direct",
  "source_kind": "local_path",
  "source_url": "$APP_DIR/current.AppImage",
  "download_url": "$APP_DIR/current.AppImage",
  "http_etag": "",
  "http_last_modified": "",
  "http_content_length": "",
  "github_owner": "",
  "github_repo": "",
  "github_asset": "",
  "files": {
    "appimage": "$APP_DIR/current.AppImage",
    "wrapper": "$TMP_HOME/.local/bin/legacy-app",
    "desktop": "$TMP_HOME/.local/share/applications/yai-legacy-app.desktop",
    "extracted_dir": "$APP_DIR/extracted"
  }
}
META

HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "legacy-app"
HOME="$TMP_HOME" "$ROOT/yai" repair legacy-app

META_JSON="$APP_DIR/metadata.json"
test -f "$META_JSON"
grep -q '"id": "legacy-app"' "$META_JSON"
grep -q '"source_kind": "local_path"' "$META_JSON"
grep -q '"http_etag": ""' "$META_JSON"
grep -q '"http_last_modified": ""' "$META_JSON"
grep -q '"http_content_length": ""' "$META_JSON"
grep -q '"checksum_status": "unknown"' "$META_JSON"
grep -Eq '"sha256": "[0-9a-f]{64}"' "$META_JSON"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/legacy-app" | grep -q "legacy app"

HOME="$TMP_HOME" "$ROOT/yai" remove legacy-app

echo "metadata json smoke test passed"
