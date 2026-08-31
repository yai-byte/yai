#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

# --- Part A: a real install records its metadata as JSON, in the documented shape ---

ORIG_DIR="$TMP_HOME/original"
mkdir -p "$ORIG_DIR"

cat > "$ORIG_DIR/JsonShape-x86_64.AppImage" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "json-shape v1"
  exit 0
fi
echo "json-shape app"
APP
chmod +x "$ORIG_DIR/JsonShape-x86_64.AppImage"

HOME="$TMP_HOME" "$ROOT/yai" install "file://$ORIG_DIR/JsonShape-x86_64.AppImage" --id json-shape >/dev/null

META_JSON="$TMP_HOME/.local/share/yai/apps/json-shape/metadata.json"
test -f "$META_JSON"
grep -q '"id": "json-shape"' "$META_JSON"
grep -q '"source_kind": "url"' "$META_JSON"
grep -q '"install_mode": "direct"' "$META_JSON"
# A file:// source has no ETag, but size and mtime are recorded so an in-place
# rewrite of that file stays detectable afterwards.
grep -q '"http_etag": ""' "$META_JSON"
grep -Eq '"http_last_modified": ".+"' "$META_JSON"
grep -Eq '"http_content_length": "[0-9]+"' "$META_JSON"
grep -q '"checksum_status": "unknown"' "$META_JSON"
grep -Eq '"sha256": "[0-9a-f]{64}"' "$META_JSON"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/json-shape" | grep -q "json-shape app"

# --- Part B: a directory with no metadata.json is a leftover, not an installed package ---

LEFT_DIR="$TMP_HOME/.local/share/yai/apps/legacy-app"
mkdir -p "$LEFT_DIR"

cat > "$LEFT_DIR/current.AppImage" <<'APP'
#!/usr/bin/env bash
echo "legacy app"
APP
chmod +x "$LEFT_DIR/current.AppImage"

# The legacy key=value file is no longer a recognized install record.
cat > "$LEFT_DIR/metadata.conf" <<'META'
id=legacy-app
name=Legacy App
source_kind=local_path
version=v1
install_mode=direct
META

# Not installed, so `list` must stay silent about it.
if HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "legacy-app"; then
  echo "FAIL: a leftover directory must not be listed as installed"
  exit 1
fi

# Metadata-dependent commands must identify it and point at the way out.
if HOME="$TMP_HOME" "$ROOT/yai" repair legacy-app 2>"$TMP_HOME/repair.err"; then
  echo "FAIL: repair must reject a leftover directory"
  exit 1
fi
grep -q "metadata.json" "$TMP_HOME/repair.err"
grep -q "yai remove legacy-app" "$TMP_HOME/repair.err"

# doctor surfaces the leftover so the user can reclaim the disk space.
HOME="$TMP_HOME" "$ROOT/yai" doctor >"$TMP_HOME/doctor.out" 2>&1 || true
grep -q "legacy-app" "$TMP_HOME/doctor.out"
grep -qi "leftover" "$TMP_HOME/doctor.out"

# remove is the way out, even though nothing was ever installed.
HOME="$TMP_HOME" "$ROOT/yai" remove legacy-app --yes >/dev/null
test ! -d "$LEFT_DIR"

# --- Part C: the real install is unaffected and still removable the normal way ---

HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "json-shape"
HOME="$TMP_HOME" "$ROOT/yai" remove json-shape --yes >/dev/null
test ! -d "$TMP_HOME/.local/share/yai/apps/json-shape"

echo "metadata json smoke test passed"
