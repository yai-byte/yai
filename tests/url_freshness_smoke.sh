#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

cat > "$TMP_DIR/headers.txt" <<'HEADERS'
HTTP/1.1 302 Found
ETag: "old"
Last-Modified: Sat, 01 Jan 2025 00:00:00 GMT
Content-Length: 10

HTTP/1.1 200 OK
ETag: "abc"
Last-Modified: Sat, 01 Jan 2026 00:00:00 GMT
Content-Length: 42
HEADERS

cat > "$TMP_DIR/url_freshness_test.cpp" <<'CPP'
#include "yai.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main() {
    const fs::path headers = "HEADERS_PATH";

    HttpValidators v = parse_http_validators_from_headers(headers);
    require(v.etag == "\"abc\"", "etag");
    require(v.last_modified == "Sat, 01 Jan 2026 00:00:00 GMT", "lm");
    require(v.content_length == "42", "len");

    HttpValidators stored{"\"abc\"", "Sat, 01 Jan 2026 00:00:00 GMT", "42"};
    require(compare_http_validators(stored, v) == UrlFreshness::Unchanged, "unchanged");

    HttpValidators remote_changed = v;
    remote_changed.etag = "\"xyz\"";
    require(compare_http_validators(stored, remote_changed) == UrlFreshness::Changed, "etag changed");

    HttpValidators empty{};
    require(compare_http_validators(empty, empty) == UrlFreshness::Unknown, "unknown");
    require(compare_http_validators(stored, empty) == UrlFreshness::Unknown, "remote empty");
    require(http_validators_empty(empty), "empty check");
    require(!http_validators_empty(stored), "non-empty check");

    const fs::path probe_file = "PROBE_FILE_PATH";
    {
        std::ofstream out(probe_file, std::ios::binary);
        out.write("abcd", 4);
    }

    HttpValidators stored_size_4;
    stored_size_4.content_length = "4";
    UrlFreshnessResult unchanged = probe_url_freshness("file://PROBE_FILE_PATH", stored_size_4);
    require(unchanged.status == UrlFreshness::Unchanged, "file unchanged");
    require(unchanged.remote.content_length == "4", "file remote size");

    HttpValidators stored_size_5;
    stored_size_5.content_length = "5";
    UrlFreshnessResult changed = probe_url_freshness("file://PROBE_FILE_PATH", stored_size_5);
    require(changed.status == UrlFreshness::Changed, "file changed");

    HttpValidators stored_empty_size;
    UrlFreshnessResult unknown = probe_url_freshness("file://PROBE_FILE_PATH", stored_empty_size);
    require(unknown.status == UrlFreshness::Unknown, "file unknown");

    std::cout << "url freshness smoke test passed\n";
    return 0;
}
CPP

sed -i "s|HEADERS_PATH|$TMP_DIR/headers.txt|g" "$TMP_DIR/url_freshness_test.cpp"
sed -i "s|PROBE_FILE_PATH|$TMP_DIR/probe.bin|g" "$TMP_DIR/url_freshness_test.cpp"

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/url_freshness_test" \
  "$TMP_DIR/url_freshness_test.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/download_progress.cpp"

"$TMP_DIR/url_freshness_test"
