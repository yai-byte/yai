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
  "$ROOT/src/json.cpp" \
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
    BatchProgressEvent ev;
    ev.kind = BatchProgressEvent::Kind::Progress;
    ev.done = 50;
    ev.total = 100;
    ev.rate_bps = 10;
    ev.elapsed = 1.0;
    ev.total_seconds = 10.0;
    ev.left_seconds = 5.0;
    const std::string progress = format_batch_progress_event(ev) + "\n";
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
  "$ROOT/src/json.cpp" \
  "$ROOT/src/process.cpp"

cat > "$TMP_DIR/batch_stream_test.cpp" <<CPP
#include "yai.hpp"
int main() {
    BatchTerminalUi ui(1);
    const StreamingBatchResult result = run_batch_task_streaming(
        BatchTaskRequest{
            {"$TMP_DIR/fake_child"},
            std::nullopt,
            {},
            0,
            "pkg",
        },
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
  "$ROOT/src/json.cpp" \
  "$ROOT/src/process.cpp"

"$TMP_DIR/batch_stream_test" >"$TMP_DIR/stream_out" 2>"$TMP_DIR/stream_err"
grep -q '\[1/1 pkg\] hello-out' "$TMP_DIR/stream_err"
grep -q '\[1/1 pkg\] hello-err' "$TMP_DIR/stream_err"
echo "batch streaming section passed"

# --- live yai CLI section (non-TTY prefixes, no CR, artifacts) ---
make -C "$ROOT" yai >/dev/null

TMP_HOME="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR" "$TMP_HOME"' EXIT
API_ROOT="$TMP_HOME/api"
ORIGINAL_ROOT="$TMP_HOME/original"
PARALLEL_DOWNLOAD_DIR="$TMP_HOME/parallel-downloads"
PARALLEL_ONE_LATEST="$API_ROOT/repos/acme/parallel-one/releases/latest"
PARALLEL_TWO_LATEST="$API_ROOT/repos/acme/parallel-two/releases/latest"
PARALLEL_ONE_ASSET="ParallelOne-x86_64.AppImage"
PARALLEL_TWO_ASSET="ParallelTwo-x86_64.AppImage"

mkdir -p "$(dirname "$PARALLEL_ONE_LATEST")" "$(dirname "$PARALLEL_TWO_LATEST")" \
  "$ORIGINAL_ROOT" "$PARALLEL_DOWNLOAD_DIR"

cat > "$ORIGINAL_ROOT/$PARALLEL_ONE_ASSET" <<'APP'
#!/usr/bin/env bash
echo "parallel one app"
APP
chmod +x "$ORIGINAL_ROOT/$PARALLEL_ONE_ASSET"

cat > "$ORIGINAL_ROOT/$PARALLEL_TWO_ASSET" <<'APP'
#!/usr/bin/env bash
echo "parallel two app"
APP
chmod +x "$ORIGINAL_ROOT/$PARALLEL_TWO_ASSET"

cat > "$PARALLEL_ONE_LATEST" <<JSON
{
  "tag_name": "v1.0.0",
  "assets": [
    {
      "name": "$PARALLEL_ONE_ASSET",
      "browser_download_url": "file://$ORIGINAL_ROOT/$PARALLEL_ONE_ASSET"
    }
  ]
}
JSON

cat > "$PARALLEL_TWO_LATEST" <<JSON
{
  "tag_name": "v1.0.0",
  "assets": [
    {
      "name": "$PARALLEL_TWO_ASSET",
      "browser_download_url": "file://$ORIGINAL_ROOT/$PARALLEL_TWO_ASSET"
    }
  ]
}
JSON

(
  cd "$PARALLEL_DOWNLOAD_DIR"
  HOME="$TMP_HOME" YAI_GITHUB_API_BASE="file://$API_ROOT" \
    "$ROOT/yai" download acme/parallel-one acme/parallel-two --jobs 2 \
    >"$TMP_DIR/cli.out" 2>"$TMP_DIR/cli.err"
)
grep -E '\[1/2 .+\] ' "$TMP_DIR/cli.err"
grep -E '\[2/2 .+\] ' "$TMP_DIR/cli.err"
if grep -q $'\r' "$TMP_DIR/cli.err"; then
  echo "CR in non-TTY batch stderr" >&2
  exit 1
fi
test -f "$PARALLEL_DOWNLOAD_DIR/parallel-one.AppImage"
test -f "$PARALLEL_DOWNLOAD_DIR/parallel-two.AppImage"
echo "batch CLI parallel download section passed"

# Wildcard sequential: stop-on-first failure (pack-a succeeds, pack-b unavailable).
ASSET_DIR="$TMP_HOME/assets"
INDEX="$TMP_HOME/multi-index.json"
mkdir -p "$ASSET_DIR"
cat >"$ASSET_DIR/PackA.AppImage" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "PackA 1.0.0"
  exit 0
fi
echo "PackA app"
APP
chmod +x "$ASSET_DIR/PackA.AppImage"
cat >"$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-25T00:00:00Z",
  "packages": [
    {
      "id": "pack-a",
      "name": "Pack A",
      "summary": "Installable first package",
      "homepage": "https://example.com/pack-a",
      "license": "Unknown",
      "source": {"type": "direct_url", "url": "file://$ASSET_DIR/PackA.AppImage"}
    },
    {
      "id": "pack-b",
      "name": "Pack B",
      "summary": "Unavailable second package",
      "homepage": "https://example.com/pack-b",
      "license": "Unknown",
      "source": {"type": "unavailable", "reason": "mid-batch failure fixture"}
    }
  ]
}
JSON

if HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" \
  "$ROOT/yai" install 'pack-*' --yes >"$TMP_DIR/install-multi.out" 2>"$TMP_DIR/install-multi.err"; then
  echo "expected multi install to fail on unavailable pack-b" >&2
  exit 1
fi
grep -q "task failed: install pack-b" "$TMP_DIR/install-multi.err"
grep -E '\[1/2 pack-a\] ' "$TMP_DIR/install-multi.err"
test -x "$TMP_HOME/.local/share/yai/apps/pack-a/current.AppImage"
test ! -e "$TMP_HOME/.local/share/yai/apps/pack-b"
echo "batch CLI sequential failure section passed"

echo "batch progress smoke passed"
