#!/usr/bin/env bash
set -euo pipefail

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
        const std::string line = format_batch_progress_event(12345, 67890, 1024.0);
        require(line == "PROGRESS done=12345 total=67890 rate=1024", "format progress");
        const auto ev = parse_batch_progress_event(line);
        require(ev.has_value(), "parse progress");
        require(ev->kind == BatchProgressEvent::Kind::Progress, "kind");
        require(ev->done == 12345, "done");
        require(ev->total.has_value() && *ev->total == 67890, "total");
        require(std::fabs(ev->rate_bps - 1024.0) < 0.001, "rate");

        const std::string unknown = format_batch_progress_event(10, std::nullopt, 0.0);
        require(unknown == "PROGRESS done=10 total=- rate=0", "format unknown total");
        const auto ev2 = parse_batch_progress_event(unknown);
        require(ev2.has_value() && !ev2->total.has_value(), "parse unknown total");

        require(format_batch_progress_clear_event() == "PROGRESS_CLEAR", "format clear");
        const auto clear = parse_batch_progress_event("PROGRESS_CLEAR");
        require(clear.has_value() && clear->kind == BatchProgressEvent::Kind::Clear, "parse clear");

        require(!parse_batch_progress_event("NOPE").has_value(), "reject junk");
        require(!parse_batch_progress_event("PROGRESS done=x total=1 rate=1").has_value(), "reject bad ints");
        require(batch_event_fd() < 0, "unset event fd");
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

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$ROOT/src" \
  -o "$TMP_DIR/progress_test" \
  "$TMP_DIR/progress_test.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/process.cpp"

"$TMP_DIR/progress_test" "$TMP_DIR"
