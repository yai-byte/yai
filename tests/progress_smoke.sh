#!/usr/bin/env bash
set -euo pipefail

export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

cat > "$TMP_DIR/progress_test.cpp" <<'CPP'
#include "yai.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        throw std::runtime_error("usage: progress_test <tmp-dir>");
    }

    const fs::path part = fs::path(argv[1]) / "demo.AppImage.part";
    {
        std::ofstream out(part, std::ios::binary);
        out.seekp((1024 * 1024) - 1);
        out.put('\0');
    }
    {
        const std::string body =
            "{\"id\":\"yai\",\"jsonrpc\":\"2.0\",\"result\":[{"
            "\"gid\":\"abc\","
            "\"completedLength\":\"1048576\","
            "\"totalLength\":\"10485760\","
            "\"downloadSpeed\":\"204800\""
            "}]}";
        const auto parsed = parse_aria2_tell_active_response(body);
        require(parsed.has_value(), "parse tellActive");
        require(parsed->completed == 1048576, "completedLength");
        require(parsed->total.has_value() && *parsed->total == 10485760, "totalLength");
        require(parsed->speed_bps.has_value() && std::fabs(*parsed->speed_bps - 204800.0) < 0.001, "downloadSpeed");

        require(!parse_aria2_tell_active_response(
                    "{\"id\":\"yai\",\"jsonrpc\":\"2.0\",\"result\":[]}").has_value(),
                "empty tellActive");
        require(!parse_aria2_tell_active_response("not-json").has_value(), "reject junk");

        // tellStopped uses the same download-list shape once the transfer leaves
        // tellActive; completed downloads must still yield final byte counts.
        const std::string stopped =
            "{\"id\":\"yai\",\"jsonrpc\":\"2.0\",\"result\":[{"
            "\"gid\":\"done\","
            "\"status\":\"complete\","
            "\"completedLength\":\"213744120\","
            "\"totalLength\":\"213744120\","
            "\"downloadSpeed\":\"0\""
            "}]}";
        const auto done = parse_aria2_tell_active_response(stopped);
        require(done.has_value() && done->completed == 213744120, "tellStopped completedLength");
        require(done->total.has_value() && *done->total == 213744120, "tellStopped totalLength");
    }

    {
        const auto idle = parse_aria2_global_stat(
            "{\"id\":\"1\",\"jsonrpc\":\"2.0\",\"result\":{"
            "\"downloadSpeed\":\"0\",\"numActive\":\"0\",\"numStopped\":\"1\","
            "\"numWaiting\":\"0\",\"uploadSpeed\":\"0\"}}");
        require(idle.has_value(), "parse globalStat");
        require(idle->num_active == 0 && idle->num_waiting == 0 && idle->num_stopped == 1,
                "globalStat fields");
        require(aria2_rpc_session_finished(*idle), "idle with stopped => finished");

        const auto active = parse_aria2_global_stat(
            "{\"id\":\"1\",\"jsonrpc\":\"2.0\",\"result\":{"
            "\"numActive\":\"1\",\"numStopped\":\"0\",\"numWaiting\":\"0\"}}");
        require(active.has_value() && !aria2_rpc_session_finished(*active),
                "active download not finished");
        const auto empty = parse_aria2_global_stat(
            "{\"id\":\"1\",\"jsonrpc\":\"2.0\",\"result\":{"
            "\"numActive\":\"0\",\"numStopped\":\"0\",\"numWaiting\":\"0\"}}");
        require(empty.has_value() && !aria2_rpc_session_finished(*empty),
                "pre-start empty session not finished");
        require(!parse_aria2_global_stat("not-json").has_value(), "reject junk globalStat");
    }

    {
        Aria2RpcProgress prev;
        prev.completed = 1000;
        prev.total = 5000;
        prev.speed_bps = 10.0;
        const auto held = merge_aria2_rpc_progress(prev, std::nullopt);
        require(held.has_value() && held->completed == 1000, "hold last rpc progress");
        Aria2RpcProgress next;
        next.completed = 2000;
        const auto advanced = merge_aria2_rpc_progress(prev, next);
        require(advanced.has_value() && advanced->completed == 2000, "accept newer rpc progress");
        require(!merge_aria2_rpc_progress(std::nullopt, std::nullopt).has_value(), "no rpc yet");
    }

    DownloadProgressState state;
    const auto start = std::chrono::steady_clock::now();
    require(download_progress_recent_speed(state, start, 0) == 0.0, "initial speed should be zero");
    const double first = download_progress_recent_speed(state, start + std::chrono::seconds(1), 1000);
    require(std::fabs(first - 1000.0) < 0.001, "first one-second speed failed");
    const double unchanged = download_progress_recent_speed(
        state,
        start + std::chrono::milliseconds(1200),
        1000);
    require(std::fabs(unchanged - 1000.0) < 0.001, "speed dropped to zero between source updates");
    const double second = download_progress_recent_speed(state, start + std::chrono::seconds(2), 1100);
    require(std::fabs(second - 100.0) < 0.001, "speed used cumulative average instead of recent window");
    const double stalled = download_progress_recent_speed(
        state,
        start + std::chrono::milliseconds(3700),
        1100);
    require(stalled == 0.0, "speed did not clear after a real stall");

    {
        BatchProgressEvent sample;
        sample.done = 12345;
        sample.total = 67890;
        sample.rate_bps = 1024.0;
        sample.elapsed = 1.5;
        sample.total_seconds = 60.0;
        sample.left_seconds = 45.0;
        const std::string line = format_batch_progress_event(sample);
        require(
            line == "PROGRESS done=12345 total=67890 rate=1024 elapsed=1.5 total_time=60 left=45",
            "format progress");
        const auto ev = parse_batch_progress_event(line);
        require(ev.has_value(), "parse progress");
        require(ev->kind == BatchProgressEvent::Kind::Progress, "kind");
        require(ev->done == 12345, "done");
        require(ev->total.has_value() && *ev->total == 67890, "total");
        require(std::fabs(ev->rate_bps - 1024.0) < 0.001, "rate");
        require(std::fabs(ev->elapsed - 1.5) < 0.001, "elapsed");
        require(std::fabs(ev->total_seconds - 60.0) < 0.001, "total_time");
        require(std::fabs(ev->left_seconds - 45.0) < 0.001, "left");

        BatchProgressEvent unknown_sample;
        unknown_sample.done = 10;
        unknown_sample.total = std::nullopt;
        unknown_sample.rate_bps = 0.0;
        unknown_sample.elapsed = 0.5;
        unknown_sample.total_seconds = -1.0;
        unknown_sample.left_seconds = -1.0;
        const std::string unknown = format_batch_progress_event(unknown_sample);
        require(
            unknown == "PROGRESS done=10 total=- rate=0 elapsed=0.5 total_time=- left=-",
            "format unknown total");
        const auto ev2 = parse_batch_progress_event(unknown);
        require(ev2.has_value() && !ev2->total.has_value(), "parse unknown total");
        require(ev2->total_seconds < 0.0 && ev2->left_seconds < 0.0, "unknown times");

        require(format_batch_progress_clear_event() == "PROGRESS_CLEAR", "format clear");
        const auto clear = parse_batch_progress_event("PROGRESS_CLEAR");
        require(clear.has_value() && clear->kind == BatchProgressEvent::Kind::Clear, "parse clear");

        require(!parse_batch_progress_event("NOPE").has_value(), "reject junk");
        require(
            !parse_batch_progress_event("PROGRESS done=x total=1 rate=1 elapsed=1 total_time=1 left=1")
                 .has_value(),
            "reject bad ints");
        require(batch_event_fd() < 0, "unset event fd");

        const std::string single_style = format_download_progress_line(
            12345,
            67890,
            1024.0,
            1.5,
            60.0,
            45.0,
            80,
            0);
        require(single_style.find("Downloaded:") != std::string::npos, "progress line has Downloaded");
        require(single_style.find('[') != std::string::npos, "progress line has bar");
    }

    {
        int fds[2];
        require(pipe(fds) == 0, "pipe");
        require(setenv("YAI_BATCH_EVENT_FD", std::to_string(fds[1]).c_str(), 1) == 0, "setenv");
        require(batch_event_fd() == fds[1], "event fd visible");

        const fs::path headers = fs::path(argv[1]) / "headers.txt";
        {
            std::ofstream out(headers);
            out << "Content-Length: 1048576\r\n";
        }
        std::size_t last_width = 0;
        DownloadProgressState prog_state;
        const auto start = std::chrono::steady_clock::now();
        render_download_progress(part, headers, start, 0, last_width, prog_state);
        clear_download_progress(last_width);

        require(close(fds[1]) == 0, "close write");
        unsetenv("YAI_BATCH_EVENT_FD");

        char buf[4096];
        const ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
        require(n > 0, "read events");
        buf[n] = '\0';
        close(fds[0]);
        const std::string text(buf);
        require(text.find("PROGRESS done=") != std::string::npos, "progress event missing");
        require(text.find("PROGRESS_CLEAR") != std::string::npos, "clear event missing");
        require(text.find('\r') == std::string::npos, "no carriage return on event path");
    }

    std::cout << "progress smoke test passed\n";
    return 0;
}
CPP

make -C "$ROOT" libyai.a >/dev/null 2>&1

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$ROOT/src" \
  -o "$TMP_DIR/progress_test" \
  "$TMP_DIR/progress_test.cpp" "$ROOT/libyai.a"

"$TMP_DIR/progress_test" "$TMP_DIR"

# Regression: aria2 with --enable-rpc finishes the file but stays in RPC mode.
# yai must shut it down so waitpid returns and the .part can be finalized.
if command -v aria2c >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
  make -C "$ROOT" -j"$(nproc)" >/tmp/yai-progress-make.out 2>/tmp/yai-progress-make.err
  dd if=/dev/urandom of="$TMP_DIR/payload.bin" bs=64k count=8 status=none
  export YAI_HTTP_ROOT="$TMP_DIR"
  export YAI_HTTP_PORT_FILE="$TMP_DIR/http.port"
  python3 - <<'PY' &
import http.server, os, socketserver
os.chdir(os.environ["YAI_HTTP_ROOT"])
httpd = socketserver.TCPServer(("127.0.0.1", 0), http.server.SimpleHTTPRequestHandler)
with open(os.environ["YAI_HTTP_PORT_FILE"], "w", encoding="utf-8") as fh:
    fh.write(str(httpd.server_address[1]))
httpd.serve_forever()
PY
  HTTP_PID=$!
  for _ in $(seq 1 100); do
    if [[ -s "$TMP_DIR/http.port" ]]; then
      break
    fi
    sleep 0.05
  done
  HTTP_PORT="$(cat "$TMP_DIR/http.port")"
  WORK="$TMP_DIR/dl"
  mkdir -p "$WORK"
  # Bound the hang: without the fix, aria2 stays in RPC mode forever.
  if ! (
    cd "$WORK"
    timeout 25 "$ROOT/yai" download \
      "http://127.0.0.1:${HTTP_PORT}/payload.bin" \
      --downloader aria2c
  ); then
    kill "$HTTP_PID" 2>/dev/null || true
    wait "$HTTP_PID" 2>/dev/null || true
    echo "aria2 rpc exit regression failed (download hung or errored)" >&2
    exit 1
  fi
  if [[ ! -s "$WORK/payload.bin.AppImage" ]]; then
    find "$WORK" -type f -printf '%p %s\n' >&2 || true
    kill "$HTTP_PID" 2>/dev/null || true
    wait "$HTTP_PID" 2>/dev/null || true
    echo "aria2 rpc exit regression failed (output file missing)" >&2
    exit 1
  fi
  kill "$HTTP_PID" 2>/dev/null || true
  wait "$HTTP_PID" 2>/dev/null || true
  echo "aria2 rpc exit regression passed"
fi
