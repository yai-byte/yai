#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
EXTRACT_AND_RUN_SRC="$TMP_HOME/Needs-Extract-And-Run.AppImage"
LAUNCH_FUSE_SRC="$TMP_HOME/Launch-Fuse-Failure.AppImage"
EXTRACTED_SRC="$TMP_HOME/Needs-Extracted.AppImage"
HANGING_SRC="$TMP_HOME/Hanging-Launch.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

cat > "$EXTRACT_AND_RUN_SRC" <<'APP'
#!/usr/bin/env bash
if [[ "${APPIMAGE_EXTRACT_AND_RUN:-}" == "1" && "${1:-}" == "--appimage-version" ]]; then
  echo "extract-and-run runtime"
  exit 0
fi
if [[ "${APPIMAGE_EXTRACT_AND_RUN:-}" == "1" ]]; then
  echo "extract-and-run app"
  exit 0
fi
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "error loading libfuse.so.2" >&2
  exit 1
fi
echo "direct app"
APP
chmod +x "$EXTRACT_AND_RUN_SRC"

cat > "$LAUNCH_FUSE_SRC" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "launch-fuse 1.0.0"
  exit 0
fi
if [[ "${APPIMAGE_EXTRACT_AND_RUN:-}" == "1" ]]; then
  echo "launch fuse fallback app"
  exit 0
fi
echo "dlopen(): error loading libfuse.so.2" >&2
echo "AppImages require FUSE to run." >&2
exit 1
APP
chmod +x "$LAUNCH_FUSE_SRC"

cat > "$EXTRACTED_SRC" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-extract" ]]; then
  mkdir -p squashfs-root
  cat > squashfs-root/AppRun <<'RUN'
#!/usr/bin/env bash
echo "extracted app"
RUN
  chmod +x squashfs-root/AppRun
  exit 0
fi
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "Cannot mount AppImage, please check your FUSE setup." >&2
  exit 1
fi
echo "direct app"
APP
chmod +x "$EXTRACTED_SRC"

cat > "$HANGING_SRC" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "hanging launch 1.0.0"
  exit 0
fi
if [[ "${1:-}" == "--appimage-extract" ]]; then
  echo "no embedded desktop"
  exit 1
fi
sleep 5
echo "hanging launch app"
APP
chmod +x "$HANGING_SRC"

HOME="$TMP_HOME" "$ROOT/yai" install "file://$EXTRACT_AND_RUN_SRC" --id extract-run --name "Extract Run"
grep -q '"install_mode": "extract_and_run"' "$TMP_HOME/.local/share/yai/apps/extract-run/metadata.json"
grep -q "APPIMAGE_EXTRACT_AND_RUN=1" "$TMP_HOME/.local/bin/extract-run"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/extract-run" | grep -q "extract-and-run app"

HOME="$TMP_HOME" "$ROOT/yai" install "file://$LAUNCH_FUSE_SRC" --id launch-fuse --name "Launch Fuse"
grep -q '"install_mode": "extract_and_run"' "$TMP_HOME/.local/share/yai/apps/launch-fuse/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/launch-fuse" | grep -q "launch fuse fallback app"

HOME="$TMP_HOME" "$ROOT/yai" install "file://$EXTRACTED_SRC" --id extracted-app --name "Extracted App"
grep -q '"install_mode": "extracted"' "$TMP_HOME/.local/share/yai/apps/extracted-app/metadata.json"
test -x "$TMP_HOME/.local/share/yai/apps/extracted-app/extracted/AppRun"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/extracted-app" | grep -q "extracted app"

start_seconds="$(date +%s)"
HOME="$TMP_HOME" "$ROOT/yai" install "file://$HANGING_SRC" --id hanging-launch --name "Hanging Launch"
elapsed_seconds="$(( $(date +%s) - start_seconds ))"
if (( elapsed_seconds > 4 )); then
  echo "launch probe did not time out promptly" >&2
  exit 1
fi
grep -q '"install_mode": "direct"' "$TMP_HOME/.local/share/yai/apps/hanging-launch/metadata.json"

rm "$TMP_HOME/.local/bin/extract-run"
HOME="$TMP_HOME" "$ROOT/yai" repair extract-run
test -x "$TMP_HOME/.local/bin/extract-run"

HOME="$TMP_HOME" "$ROOT/yai" doctor | grep -q "Doctor finished"

HOME="$TMP_HOME" "$ROOT/yai" remove extract-run
HOME="$TMP_HOME" "$ROOT/yai" remove launch-fuse
HOME="$TMP_HOME" "$ROOT/yai" remove extracted-app
HOME="$TMP_HOME" "$ROOT/yai" remove hanging-launch

echo "stage2 smoke test passed"
