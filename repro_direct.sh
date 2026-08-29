#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="/workspace"
TMP_HOME="$(mktemp -d)"
echo "TMP_HOME=$TMP_HOME"
ORIGINAL_ROOT="$TMP_HOME/original"
MUSESCORE_ASSET="MuseScore-x86_64.AppImage"

MUSESCORE_DOWNLOAD_PAGE="$TMP_HOME/musescore.org/downloads/index.html"
mkdir -p "$(dirname "$MUSESCORE_DOWNLOAD_PAGE")" "$ORIGINAL_ROOT"

cat > "$ORIGINAL_ROOT/$MUSESCORE_ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "musescore 1.0.0"
  exit 0
fi
echo "musescore app"
APP
chmod +x "$ORIGINAL_ROOT/$MUSESCORE_ASSET"

cat > "$MUSESCORE_DOWNLOAD_PAGE" <<HTML
<!doctype html>
<html><body><a href="file://$ORIGINAL_ROOT/$MUSESCORE_ASSET">Download MuseScore AppImage</a></body></html>
HTML

set +e
HOME="$TMP_HOME" "$ROOT/yai" install "file://$MUSESCORE_DOWNLOAD_PAGE" \
  --id musescore-direct \
  --name "MuseScore Direct" \
  2>"$TMP_HOME/musescore_direct_install.err"
rc=$?
set -e
echo "musescore-direct install exit: $rc"
echo "--- stderr ---"
cat "$TMP_HOME/musescore_direct_install.err"
if [[ $rc -eq 0 ]]; then
  echo "--- metadata.json ---"
  cat "$TMP_HOME/.local/share/yai/apps/musescore-direct/metadata.json" 2>/dev/null || echo "no metadata"
fi
echo "--- grep for landing page ---"
grep -n "downloaded an AppImage landing page" "$TMP_HOME/musescore_direct_install.err" 2>/dev/null || echo "NOT FOUND"
rm -rf "$TMP_HOME"
