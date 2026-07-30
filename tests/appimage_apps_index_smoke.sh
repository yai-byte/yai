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

void test_merge_apps_entry_into_package() {
    std::cout << "=== Test: merge_apps_entry_into_package ===";

    // Test 1: Enrich existing package with apps/ metadata
    {
        RepoPackage existing;
        existing.id = "test-app";
        existing.name = "Test App";
        existing.summary = "From feed";
        existing.source_owner = "user";
        existing.source_repo = "repo";
        existing.source_type = "github_release";

        AppImageAppsEntry entry;
        entry.name = "test-app";
        entry.description = "From apps/ with more details";
        entry.license = "MIT";
        entry.homepage = "https://example.com";
        entry.github_repo = "user/repo";
        entry.arch = "x86_64";
        entry.version = "2.0";

        RepoPackage merged = merge_apps_entry_into_package(entry, existing);

        check(merged.source_origin == "appimage_apps", "merge: source_origin set to appimage_apps");
        check(merged.arch == "x86_64", "merge: arch copied from apps/");
        check(merged.version == "2.0", "merge: version copied from apps/");
        check(merged.summary == "From feed", "merge: feed summary preserved");
        check(merged.license == "MIT", "merge: license enriched from apps/");
        check(merged.homepage == "https://example.com", "merge: homepage enriched from apps/");
        check(merged.source_owner == "user", "merge: source_owner preserved");
        check(merged.source_repo == "repo", "merge: source_repo preserved");
    }

    // Test 2: Enrich package with no GitHub info from apps/
    {
        RepoPackage existing;
        existing.id = "new-app";
        existing.name = "New App";
        existing.source_type = "website_page";

        AppImageAppsEntry entry;
        entry.name = "new-app";
        entry.description = "New application";
        entry.github_repo = "newuser/newrepo";
        entry.arch = "aarch64";

        RepoPackage merged = merge_apps_entry_into_package(entry, existing);

        check(merged.source_type == "github_release", "merge: website_page upgraded to github_release");
        check(merged.source_owner == "newuser", "merge: source_owner extracted from apps/");
        check(merged.source_repo == "newrepo", "merge: source_repo extracted from apps/");
        check(merged.arch == "aarch64", "merge: arch set for new package");
    }

    // Test 3: Don't overwrite non-empty fields from feed
    {
        RepoPackage existing;
        existing.id = "existing-app";
        existing.name = "Existing App";
        existing.summary = "Feed summary";
        existing.license = "GPL-3.0";
        existing.homepage = "https://feed.example.com";
        existing.source_type = "github_release";
        existing.source_owner = "originalowner";
        existing.source_repo = "originalrepo";

        AppImageAppsEntry entry;
        entry.name = "existing-app";
        entry.description = "Apps/ summary";
        entry.license = "MIT";
        entry.homepage = "https://apps.example.com";
        entry.github_repo = "newowner/newrepo";
        entry.arch = "x86_64";
        entry.version = "1.0";

        RepoPackage merged = merge_apps_entry_into_package(entry, existing);

        check(merged.summary == "Feed summary", "merge: feed summary not overwritten");
        check(merged.license == "GPL-3.0", "merge: feed license not overwritten");
        check(merged.homepage == "https://feed.example.com", "merge: feed homepage not overwritten");
        check(merged.source_owner == "originalowner", "merge: feed source_owner not overwritten");
        check(merged.source_repo == "originalrepo", "merge: feed source_repo not overwritten");
        check(merged.arch == "x86_64", "merge: arch added from apps/");
        check(merged.version == "1.0", "merge: version added from apps/");
    }

    // Test 4: New package from apps/ entry
    {
        RepoPackage existing;
        existing.id = "brand-new";
        existing.name = "Brand New App";

        AppImageAppsEntry entry;
        entry.name = "brand-new";
        entry.description = "A brand new application";
        entry.license = "Apache-2.0";
        entry.homepage = "https://brandnew.example.com";
        entry.github_repo = "brand/newrepo";
        entry.arch = "aarch64";
        entry.version = "0.1.0";

        RepoPackage merged = merge_apps_entry_into_package(entry, existing);

        check(merged.source_origin == "appimage_apps", "merge new: source_origin set");
        check(merged.source_type == "github_release", "merge new: github_release set");
        check(merged.source_owner == "brand", "merge new: source_owner extracted");
        check(merged.source_repo == "newrepo", "merge new: source_repo extracted");
        check(merged.summary == "A brand new application", "merge new: summary set");
        check(merged.license == "Apache-2.0", "merge new: license set");
        check(merged.homepage == "https://brandnew.example.com", "merge new: homepage set");
        check(merged.arch == "aarch64", "merge new: arch set");
        check(merged.version == "0.1.0", "merge new: version set");
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
        test_merge_apps_entry_into_package();
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
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TEST_BIN" \
  "$TMP_HOME/test_apps_integration.cpp" \
  "$ROOT/src/repo_appimage_github.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/repo_index_urls.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/appimage_desktop.cpp" \
  "$ROOT/src/appimage_runtime.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/terminal_color.cpp" \
  "$ROOT/src/website_resolve_cache.cpp" \
  "$ROOT/src/resolver.cpp" \
  "$ROOT/src/resolver_github.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  "$ROOT/src/resolver_website.cpp" \
  -pthread 2>&1 || {
    echo "Compilation failed!"
    exit 1
  }

"$TEST_BIN"
echo "EXIT=$?"
