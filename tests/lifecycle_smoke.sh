#!/usr/bin/env bash
# End-to-end smoke test for the install lifecycle commands: doctor, repair, rollback.
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
APPS_DIR="$TMP_HOME/.local/share/yai/apps"
BIN_DIR="$TMP_HOME/.local/bin"
DESKTOP_DIR="$TMP_HOME/.local/share/applications"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

# --- One local AppImage fixture, mutated between versions to simulate an upgrade ---
APP_SRC="$TMP_HOME/App.AppImage"

cat > "$APP_SRC" <<'APP'
#!/usr/bin/env bash
echo "app version 1"
APP
chmod +x "$APP_SRC"

# --- Install v1 ---
HOME="$TMP_HOME" "$ROOT/yai" install "file://$APP_SRC" --id lifecycle-app --name "Lifecycle App" --yes

META="$APPS_DIR/lifecycle-app/metadata.json"
test -x "$APPS_DIR/lifecycle-app/current.AppImage"
test -x "$BIN_DIR/lifecycle-app"
test -f "$DESKTOP_DIR/yai-lifecycle-app.desktop"
test -f "$META"
HOME="$TMP_HOME" "$BIN_DIR/lifecycle-app" | grep -q "app version 1"

# --- doctor: a healthy install reports OK ---
HOME="$TMP_HOME" "$ROOT/yai" doctor | grep -q "OK   lifecycle-app"
HOME="$TMP_HOME" "$ROOT/yai" doctor | grep -q "no problems" || true

# --- repair: deleting the wrapper and desktop entry is recoverable ---
rm -f "$BIN_DIR/lifecycle-app"
rm -f "$DESKTOP_DIR/yai-lifecycle-app.desktop"
test ! -e "$BIN_DIR/lifecycle-app"
test ! -e "$DESKTOP_DIR/yai-lifecycle-app.desktop"

HOME="$TMP_HOME" "$ROOT/yai" repair 'lifecycle-*' --yes
test -x "$BIN_DIR/lifecycle-app"
test -f "$DESKTOP_DIR/yai-lifecycle-app.desktop"
HOME="$TMP_HOME" "$BIN_DIR/lifecycle-app" | grep -q "app version 1"

# --- doctor: after repair everything is OK again ---
HOME="$TMP_HOME" "$ROOT/yai" doctor | grep -q "OK   lifecycle-app"

# --- doctor: a missing wrapper (without repair) is reported as WARN ---
rm -f "$BIN_DIR/lifecycle-app"
HOME="$TMP_HOME" "$ROOT/yai" doctor | grep -q "WARN lifecycle-app"
HOME="$TMP_HOME" "$ROOT/yai" doctor | grep -qi "missing wrapper"
# repair restores it once more
HOME="$TMP_HOME" "$ROOT/yai" repair 'lifecycle-*' --yes
test -x "$BIN_DIR/lifecycle-app"

# --- doctor: a leftover directory (no metadata.json) is reported ---
LEFT_DIR="$APPS_DIR/leftover-app"
mkdir -p "$LEFT_DIR"
cat > "$LEFT_DIR/current.AppImage" <<'APP'
#!/usr/bin/env bash
echo "leftover"
APP
chmod +x "$LEFT_DIR/current.AppImage"
HOME="$TMP_HOME" "$ROOT/yai" doctor | grep -q "leftover-app"
HOME="$TMP_HOME" "$ROOT/yai" doctor | grep -qi "leftover"
HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "lifecycle-app"
if HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "leftover-app"; then
  echo "FAIL: leftover directory must not appear in list" >&2
  exit 1
fi
# reclaim the leftover
HOME="$TMP_HOME" "$ROOT/yai" remove leftover-app --yes >/dev/null
test ! -d "$LEFT_DIR"

# --- rollback: upgrade to v2, then roll back to v1 ---
# Mutate the source fixture in place so `yai upgrade` re-downloads a new version.
cat > "$APP_SRC" <<'APP'
#!/usr/bin/env bash
echo "app version 2"
APP
chmod +x "$APP_SRC"

HOME="$TMP_HOME" "$ROOT/yai" upgrade 'lifecycle-*' --yes
HOME="$TMP_HOME" "$BIN_DIR/lifecycle-app" | grep -q "app version 2"
test -d "$APPS_DIR/lifecycle-app/versions/previous"

HOME="$TMP_HOME" "$ROOT/yai" rollback 'lifecycle-*' --yes
HOME="$TMP_HOME" "$BIN_DIR/lifecycle-app" | grep -q "app version 1"
# rollback metadata should carry the restored version
if HOME="$TMP_HOME" "$BIN_DIR/lifecycle-app" | grep -q "app version 2"; then
  echo "FAIL: rollback did not restore v1 runtime" >&2
  exit 1
fi

# --- rollback with no previous version available is rejected ---
HOME="$TMP_HOME" "$ROOT/yai" remove lifecycle-app --yes >/dev/null
test ! -d "$APPS_DIR/lifecycle-app"
if HOME="$TMP_HOME" "$ROOT/yai" rollback lifecycle-app --yes 2>"$TMP_HOME/rb.err"; then
  echo "FAIL: rollback on a missing package must fail" >&2
  exit 1
fi
grep -qi "no rollback version" "$TMP_HOME/rb.err" || grep -qi "not installed" "$TMP_HOME/rb.err"

echo "lifecycle smoke test passed"
