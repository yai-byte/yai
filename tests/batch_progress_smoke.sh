#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
export YAI_LANG=en

cat > "$TMP_DIR/batch_ui_test.cpp" <<'CPP'
#include "yai.hpp"
int main() {
    BatchTerminalUi ui(2);
    ui.log_line(0, "foo", "Downloading x");
    ui.log_line(1, "bar", "Downloading y");
    BatchProgressEvent ev;
    ev.kind = BatchProgressEvent::Kind::Progress;
    ev.done = 50; ev.total = 100; ev.rate_bps = 10;
    ui.apply_event(0, "foo", ev);
    ui.shutdown();
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$ROOT/src" \
  -o "$TMP_DIR/batch_ui_test" \
  "$TMP_DIR/batch_ui_test.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/process.cpp"

"$TMP_DIR/batch_ui_test" >"$TMP_DIR/out" 2>"$TMP_DIR/err"
grep -q '\[1/2 foo\] Downloading x' "$TMP_DIR/err"
grep -q '\[2/2 bar\] Downloading y' "$TMP_DIR/err"
if grep -q $'\r' "$TMP_DIR/err"; then
  echo "unexpected CR in non-TTY UI output" >&2
  exit 1
fi
echo "batch ui unit section passed"
