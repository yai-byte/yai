#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
APPIMAGE_SRC="$TMP_HOME/Fake-App.AppImage"
LOCAL_APPIMAGE_SRC="$TMP_HOME/Local-App.AppImage"
PARALLEL_ONE_SRC="$TMP_HOME/Parallel-One.AppImage"
PARALLEL_TWO_SRC="$TMP_HOME/Parallel-Two.AppImage"
RELATIVE_APPIMAGE_DIR="$TMP_HOME/relative"
RELATIVE_APPIMAGE_SRC="$RELATIVE_APPIMAGE_DIR/Bare.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

cat > "$APPIMAGE_SRC" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-extract" ]]; then
  mkdir -p squashfs-root
  cat > squashfs-root/fake.desktop <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Fake Upstream App
Comment=Upstream summary from AppImage
Exec=/tmp/should-not-be-used --unsafe
Icon=fake
Categories=Graphics;Utility;
StartupNotify=false
DESKTOP
  cat > squashfs-root/fake.png <<'PNG'
fake icon
PNG
  exit 0
fi
echo "fake appimage"
APP
chmod +x "$APPIMAGE_SRC"

cat > "$LOCAL_APPIMAGE_SRC" <<'APP'
#!/usr/bin/env bash
echo "local appimage"
APP
chmod +x "$LOCAL_APPIMAGE_SRC"

cat > "$PARALLEL_ONE_SRC" <<'APP'
#!/usr/bin/env bash
echo "parallel one local appimage"
APP
chmod +x "$PARALLEL_ONE_SRC"

cat > "$PARALLEL_TWO_SRC" <<'APP'
#!/usr/bin/env bash
echo "parallel two local appimage"
APP
chmod +x "$PARALLEL_TWO_SRC"

mkdir -p "$RELATIVE_APPIMAGE_DIR"
cat > "$RELATIVE_APPIMAGE_SRC" <<'APP'
#!/usr/bin/env bash
echo "bare local appimage"
APP
chmod +x "$RELATIVE_APPIMAGE_SRC"

YAI_LANG=zh "$ROOT/yai" help | grep -q "用法"
YAI_LANG=en LANG=zh_CN.UTF-8 "$ROOT/yai" help | grep -q "Usage"
env -u YAI_LANG LANG=zh_CN.UTF-8 "$ROOT/yai" help | grep -q "语言"
env -u YAI_LANG LANG=C "$ROOT/yai" help | grep -q "Usage"

HOME="$TMP_HOME" "$ROOT/yai" install "file://$APPIMAGE_SRC" --id fake-app --name "Fake App"

test -x "$TMP_HOME/.local/share/yai/apps/fake-app/current.AppImage"
test -x "$TMP_HOME/.local/bin/fake-app"
test -f "$TMP_HOME/.local/share/applications/yai-fake-app.desktop"
grep -q "Name=Fake Upstream App" "$TMP_HOME/.local/share/applications/yai-fake-app.desktop"
grep -q "Comment=Upstream summary from AppImage" "$TMP_HOME/.local/share/applications/yai-fake-app.desktop"
grep -Fq "Exec=$TMP_HOME/.local/bin/fake-app %U" "$TMP_HOME/.local/share/applications/yai-fake-app.desktop"
grep -Fq "Icon=$TMP_HOME/.local/share/yai/apps/fake-app/desktop-icon.png" "$TMP_HOME/.local/share/applications/yai-fake-app.desktop"
test -f "$TMP_HOME/.local/share/yai/apps/fake-app/desktop-icon.png"
! grep -q "/tmp/should-not-be-used" "$TMP_HOME/.local/share/applications/yai-fake-app.desktop"

HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "fake-app"
HOME="$TMP_HOME" "$ROOT/yai" repair 'fake-*'
HOME="$TMP_HOME" "$ROOT/yai" remove 'fake-*'

test ! -e "$TMP_HOME/.local/bin/fake-app"
test ! -e "$TMP_HOME/.local/share/applications/yai-fake-app.desktop"
test ! -e "$TMP_HOME/.local/share/yai/apps/fake-app"

HOME="$TMP_HOME" "$ROOT/yai" install "$LOCAL_APPIMAGE_SRC" --id local-app --name "Local App"
LOCAL_META="$TMP_HOME/.local/share/yai/apps/local-app/metadata.json"
grep -q '"source_kind": "local_path"' "$LOCAL_META"
grep -Fq "\"source_url\": \"$LOCAL_APPIMAGE_SRC\"" "$LOCAL_META"
grep -q '"checksum_status": "unknown"' "$LOCAL_META"
grep -Eq '"sha256": "[0-9a-f]{64}"' "$LOCAL_META"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/local-app" | grep -q "local appimage"
HOME="$TMP_HOME" "$ROOT/yai" remove 'local-*'

HOME="$TMP_HOME" "$ROOT/yai" install "$PARALLEL_ONE_SRC" "$PARALLEL_TWO_SRC" --jobs 2
test -x "$TMP_HOME/.local/share/yai/apps/parallel-one/current.AppImage"
test -x "$TMP_HOME/.local/share/yai/apps/parallel-two/current.AppImage"
test -x "$TMP_HOME/.local/bin/parallel-one"
test -x "$TMP_HOME/.local/bin/parallel-two"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/parallel-one" | grep -q "parallel one local appimage"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/parallel-two" | grep -q "parallel two local appimage"
HOME="$TMP_HOME" "$ROOT/yai" remove parallel-one
HOME="$TMP_HOME" "$ROOT/yai" remove parallel-two

(
  cd "$RELATIVE_APPIMAGE_DIR"
  HOME="$TMP_HOME" "$ROOT/yai" install "Bare.AppImage" --id bare-app --name "Bare Local App"
)
grep -q '"source_kind": "local_path"' "$TMP_HOME/.local/share/yai/apps/bare-app/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/bare-app" | grep -q "bare local appimage"
HOME="$TMP_HOME" "$ROOT/yai" remove bare-app

echo "stage1 smoke test passed"
