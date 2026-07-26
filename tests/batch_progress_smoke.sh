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

# --- streaming section ---
cat > "$TMP_DIR/fake_child.cpp" <<'CPP'
#include "yai.hpp"
#include <unistd.h>
int main() {
    std::cout << "hello-out\n" << std::flush;
    std::cerr << "hello-err\n" << std::flush;
    const int fd = batch_event_fd();
    if (fd < 0) {
        return 2;
    }
    const std::string progress = format_batch_progress_event(50, 100, 10) + "\n";
    const std::string clear = format_batch_progress_clear_event() + "\n";
    if (write(fd, progress.data(), progress.size()) != static_cast<ssize_t>(progress.size())) {
        return 3;
    }
    if (write(fd, clear.data(), clear.size()) != static_cast<ssize_t>(clear.size())) {
        return 4;
    }
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$ROOT/src" \
  -o "$TMP_DIR/fake_child" \
  "$TMP_DIR/fake_child.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/process.cpp"

cat > "$TMP_DIR/batch_stream_test.cpp" <<CPP
#include "yai.hpp"
int main() {
    BatchTerminalUi ui(1);
    const StreamingBatchResult result = run_batch_task_streaming(
        {"$TMP_DIR/fake_child"},
        std::nullopt,
        {},
        0,
        1,
        "pkg",
        ui);
    ui.shutdown();
    if (result.exit_code != 0) {
        return 10 + result.exit_code;
    }
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$ROOT/src" \
  -o "$TMP_DIR/batch_stream_test" \
  "$TMP_DIR/batch_stream_test.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/process.cpp"

"$TMP_DIR/batch_stream_test" >"$TMP_DIR/stream_out" 2>"$TMP_DIR/stream_err"
grep -q '\[1/1 pkg\] hello-out' "$TMP_DIR/stream_err"
grep -q '\[1/1 pkg\] hello-err' "$TMP_DIR/stream_err"
echo "batch streaming section passed"
