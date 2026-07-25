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
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

static void write_u16(std::ofstream& out, std::uint16_t value) {
    out.put(static_cast<char>((value >> 8) & 0xff));
    out.put(static_cast<char>(value & 0xff));
}

static void write_u32(std::ofstream& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.put(static_cast<char>((value >> shift) & 0xff));
    }
}

static void write_u64(std::ofstream& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.put(static_cast<char>((value >> shift) & 0xff));
    }
}

static void write_aria2_control(const fs::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to create aria2 control fixture");
    }

    const std::uint32_t piece_length = 256 * 1024;
    const std::uint64_t total_length = 1024 * 1024;

    write_u16(out, 1);                  // version
    write_u32(out, 0);                  // extension length
    write_u32(out, 0);                  // info hash length
    write_u32(out, piece_length);
    write_u64(out, total_length);
    write_u64(out, 0);                  // upload length
    write_u32(out, 1);                  // completed-piece bitfield length
    out.put(static_cast<char>(0x80));   // first 256 KiB piece is complete
    write_u32(out, 1);                  // in-flight piece count
    write_u32(out, 2);                  // third piece is partially complete
    write_u32(out, piece_length);
    write_u32(out, 2);                  // 16 chunks need 2 bytes of bitfield
    out.put(static_cast<char>(0x80));   // one 16 KiB chunk is complete
    out.put(static_cast<char>(0x00));
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
    write_aria2_control(part.string() + ".aria2");

    const std::uintmax_t downloaded = download_progress_downloaded_bytes(part);
    require(downloaded == (256 * 1024) + (16 * 1024), "aria2 progress used apparent file size");

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

    std::cout << "progress smoke test passed\n";
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/progress_test" \
  "$TMP_DIR/progress_test.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/process.cpp"

"$TMP_DIR/progress_test" "$TMP_DIR"
