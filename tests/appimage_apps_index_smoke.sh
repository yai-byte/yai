#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
TEST_BIN="$TMP_HOME/test_apps_integration"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

# Compile a C++ test that exercises the apps/ integration logic
cat > "$TMP_HOME/test_apps_integration.cpp" <<'CPP'
#include "yai.hpp"
#include <iostream>
#include <stdexcept>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool condition, const std::string& message) {
    if (condition) {
        tests_passed++;
        std::cout << "  PASS: " << message << "\n";
    } else {
        tests_failed++;
        std::cerr << "  FAIL: " << message << "\n";
    }
}

void test_structs() {
    std::cout << "=== Test: Struct definitions ===";

    // Test AppImageAppsEntry struct
    {
        AppImageAppsEntry entry;
        entry.name = "TestApp";
        entry.github_repo = "owner/repo";
        entry.direct_url = "https://example.com/app.AppImage";
        entry.homepage = "https://example.com";
        entry.description = "A test app";
        entry.license = "MIT";
        entry.arch = "x86_64";
        entry.version = "1.0";

        check(entry.name == "TestApp", "AppImageAppsEntry: name");
        check(entry.github_repo == "owner/repo", "AppImageAppsEntry: github_repo");
        check(entry.direct_url == "https://example.com/app.AppImage", "AppImageAppsEntry: direct_url");
        check(entry.homepage == "https://example.com", "AppImageAppsEntry: homepage");
        check(entry.description == "A test app", "AppImageAppsEntry: description");
        check(entry.license == "MIT", "AppImageAppsEntry: license");
        check(entry.arch == "x86_64", "AppImageAppsEntry: arch");
        check(entry.version == "1.0", "AppImageAppsEntry: version");
    }

    // Test AppImageDataEntry struct
    {
        AppImageDataEntry entry;
        entry.name = "DataApp";
        entry.github_repo = "user/datarepo";
        entry.direct_url = "https://download.com/app.AppImage";

        check(entry.name == "DataApp", "AppImageDataEntry: name");
        check(entry.github_repo == "user/datarepo", "AppImageDataEntry: github_repo");
        check(entry.direct_url == "https://download.com/app.AppImage", "AppImageDataEntry: direct_url");
    }
}


void test_serialization() {
    std::cout << "=== Test: New field serialization ===";

    // Test RepoPackage with new fields
    {
        RepoPackage pkg;
        pkg.id = "test";
        pkg.name = "Test";
        pkg.source_type = "github_release";
        pkg.source_owner = "owner";
        pkg.source_repo = "repo";
        pkg.arch = "x86_64";
        pkg.version = "3.0";
        pkg.source_origin = "appimage_apps";

        std::string serialized = serialize_repo_package(pkg);
        check(!serialized.empty(), "serialization: produces non-empty output");

        RepoPackage parsed = parse_repo_package(serialized);
        check(parsed.id == "test", "serialization round-trip: id preserved");
        check(parsed.arch == "x86_64", "serialization round-trip: arch preserved");
        check(parsed.version == "3.0", "serialization round-trip: version preserved");
        check(parsed.source_origin == "appimage_apps", "serialization round-trip: source_origin preserved");
    }

    // Test backward compatibility (no new fields)
    {
        RepoPackage pkg;
        pkg.id = "old-style";
        pkg.name = "Old Style";
        pkg.source_type = "direct_url";
        pkg.source_url = "https://example.com/old.AppImage";

        std::string serialized = serialize_repo_package(pkg);
        RepoPackage parsed = parse_repo_package(serialized);

        check(parsed.id == "old-style", "compat: id preserved");
        check(parsed.source_type == "direct_url", "compat: source_type preserved");
        check(parsed.source_url == "https://example.com/old.AppImage", "compat: source_url preserved");
        check(parsed.arch.empty(), "compat: arch empty for old-style package");
        check(parsed.version.empty(), "compat: version empty for old-style package");
        check(parsed.source_origin.empty(), "compat: source_origin empty for old-style package");
    }
}

void test_github_repo_detection() {
    std::cout << "=== Test: GitHub repo detection ===";

    // Test looks_like_github_repo (owner/repo format only, not full URLs)
    check(looks_like_github_repo("user/repo"),
          "github_repo: owner/repo format");
    check(looks_like_github_repo("owner-name/repo-name"),
          "github_repo: hyphenated owner/repo");
    check(!looks_like_github_repo("https://github.com/user/repo"),
          "github_repo: URL rejected (no scheme allowed)");
    check(!looks_like_github_repo("singleword"),
          "github_repo: single word rejected");
    check(!looks_like_github_repo("a/b/c"),
          "github_repo: multi-slash rejected");
    check(!looks_like_github_repo(""),
          "github_repo: empty string rejected");
}

int main() {
    try {
        test_structs();
        test_serialization();
        test_github_repo_detection();

        std::cout << "\n=== Results: " << tests_passed << " passed, "
                  << tests_failed << " failed ===\n";

        return tests_failed > 0 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
CPP

# Compile the test
make -C "$ROOT" libyai.a >/dev/null 2>&1

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$ROOT/src" \
  -o "$TEST_BIN" \
  "$TMP_HOME/test_apps_integration.cpp" "$ROOT/libyai.a" 2>&1 || {
    echo "Compilation failed!"
    exit 1
  }

"$TEST_BIN"
echo "EXIT=$?"
