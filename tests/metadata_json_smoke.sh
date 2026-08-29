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

# Unified fixture: installed state always uses the modern JSON metadata
# format (see development docs §8.2). No legacy metadata.conf reader is
# supported any more; repair should rewrite the file with fresh timestamps,
# sha256 and validator fields but keep the installed identity.
META_JSON="$APP_DIR/metadata.json"
cat > "$META_JSON" <<META
{
  "id": "legacy-app",
  "name": "Legacy App",
  "version": "v1",
  "arch": "x86_64",
  "install_mode": "direct",
  "installed_at": "2026-01-01T00:00:00Z",
  "source_kind": "local_path",
  "source_url": "$APP_DIR/current.AppImage",
  "download_url": "$APP_DIR/current.AppImage",
  "http_etag": "",
  "http_last_modified": "",
  "http_content_length": "",
  "github_owner": "",
  "github_repo": "",
  "github_asset": "",
  "sha256": "",
  "checksum_status": "unknown",
  "files": {
    "appimage": "$APP_DIR/current.AppImage",
    "wrapper": "$TMP_HOME/.local/bin/legacy-app",
    "desktop": "$TMP_HOME/.local/share/applications/yai-legacy-app.desktop",
    "extracted_dir": "$APP_DIR/extracted"
  }
}
META

# A stray legacy metadata.conf must not be required and must be removed by repair.
touch "$APP_DIR/metadata.conf"

HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "legacy-app"
HOME="$TMP_HOME" "$ROOT/yai" repair legacy-app

test -f "$META_JSON"
test ! -e "$APP_DIR/metadata.conf"
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
