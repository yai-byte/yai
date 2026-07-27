#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
export HOME="$TMP_DIR/home"
mkdir -p "$HOME"

cat > "$TMP_DIR/url_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

int main() {
    RepoPackage p;
    p.id = "pkg";
    p.name = "Pkg";
    p.source_type = "direct_url";
    p.source_url = "https://example/a.AppImage";

    require(!repo_package_has_download_url_for_arch(p, "x86_64"), "empty");
    repo_package_set_download_url(p, "x86_64", "https://example/x.AppImage", false, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/x.AppImage", "single");
    require(p.download_url == "https://example/x.AppImage", "mirrored");
    require(!p.resolved_at.empty(), "timestamp");

    // no overwrite
    repo_package_set_download_url(p, "x86_64", "https://example/other.AppImage", false, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/x.AppImage", "kept");

    // overwrite
    repo_package_set_download_url(p, "x86_64", "https://example/other.AppImage", true, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/other.AppImage", "overwritten");

    // multi-arch: missing arch must not fall back to download_url when map exists
    repo_package_set_download_url(p, "aarch64", "https://example/a.AppImage", true, false);
    require(!repo_package_download_url_for_arch(p, "armv7").has_value(), "no wrong-arch fallback");

    // serialize round-trip via parse_repo_package
    const std::string obj = serialize_repo_package(p);
    require(obj.find("download_urls") != std::string::npos, "has map");
    const RepoPackage again = parse_repo_package(obj);
    require(repo_package_download_url_for_arch(again, "aarch64").value_or("").find("a.AppImage") != std::string::npos, "roundtrip");

    // single-field compatibility: only download_url
    RepoPackage single;
    single.id = "s";
    single.name = "s";
    single.source_type = "direct_url";
    single.source_url = "https://example/s.AppImage";
    single.download_url = "https://example/only.AppImage";
    require(repo_package_download_url_for_arch(single, "x86_64").value_or("") == "https://example/only.AppImage", "compat");

    // no-overwrite must respect single-field download_url (read-priority semantics)
    repo_package_set_download_url(single, "x86_64", "https://example/new.AppImage", false, true);
    require(single.download_url == "https://example/only.AppImage", "compat no-overwrite download_url");
    require(single.download_urls.empty(), "compat no-overwrite map stays empty");
    require(repo_package_download_url_for_arch(single, "x86_64").value_or("") == "https://example/only.AppImage",
            "compat no-overwrite read");

    std::cout << "repo index url unit smoke passed\n";
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/url_unit" \
  "$TMP_DIR/url_unit.cpp" \
  "$ROOT/src/repo_index_urls.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/terminal_color.cpp" \
  "$ROOT/src/website_resolve_cache.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  -pthread

"$TMP_DIR/url_unit"

# --- Task 2: persist + merge across repo update ---

cat > "$TMP_DIR/persist_merge_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

static std::string minimal_index_json(const RepoPackage& package) {
    return std::string(
               "{\n"
               "  \"schema_version\": 1,\n"
               "  \"updated_at\": \"2026-01-01T00:00:00Z\",\n"
               "  \"packages\": [\n") +
           serialize_repo_package(package) +
           "\n  ]\n"
           "}\n";
}

int main() {
    require(repo_index_is_locally_writable(), "writable default");

    RepoPackage base;
    base.id = "demo";
    base.name = "Demo";
    base.source_type = "direct_url";
    base.source_url = "https://example/demo.AppImage";

    ensure_directory(repos_dir_path());
    write_text_file(named_repo_index_path("local"), minimal_index_json(base));
    write_text_file(repos_dir_path() / "index.json", minimal_index_json(base));
    write_repo_entries({RepoEntry{"local", "/tmp/local-feed.json"}});

    RepoPackage enriched = base;
    repo_package_set_download_url(
        enriched, "x86_64", "https://example/demo-x86_64.AppImage", false, true);
    upsert_repo_package_download_urls(enriched);

    const std::vector<RepoPackage> loaded = load_repo_packages();
    require(loaded.size() == 1, "one package");
    require(
        repo_package_download_url_for_arch(loaded[0], "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "upsert persisted url");
    require(!loaded[0].resolved_at.empty(), "upsert persisted resolved_at");

    const std::string named_after = read_text_file(named_repo_index_path("local"));
    require(named_after.find("download_urls") != std::string::npos, "named cache enriched");

    RepoPackage incoming = base;
    const RepoPackage previous = loaded[0];
    const RepoPackage merged = merge_repo_package_download_url_fields(incoming, previous);
    require(
        repo_package_download_url_for_arch(merged, "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "merge keeps url");
    require(merged.resolved_at == previous.resolved_at, "merge keeps resolved_at");

    write_text_file(named_repo_index_path("local"), minimal_index_json(merged));
    rebuild_repo_index_from_cached_files({RepoEntry{"local", "/tmp/local-feed.json"}});

    const std::vector<RepoPackage> after_rebuild = load_repo_packages();
    require(after_rebuild.size() == 1, "rebuild one package");
    require(
        repo_package_download_url_for_arch(after_rebuild[0], "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "rebuild keeps url");

    // Simulate store_repo_index_updates merge: upstream text has no URL fields
    write_text_file(named_repo_index_path("local"), minimal_index_json(previous));
    const std::string upstream = minimal_index_json(base);
    const fs::path named = named_repo_index_path("local");
    require(fs::exists(named), "named exists before update");
    {
        std::map<std::string, RepoPackage> previous_by_id;
        for (const std::string& object : repo_package_objects_from_index(read_text_file(named))) {
            const RepoPackage pkg = parse_repo_package(object);
            previous_by_id[pkg.id] = pkg;
        }
        std::vector<RepoPackage> incoming_pkgs;
        for (const std::string& object : repo_package_objects_from_index(upstream)) {
            RepoPackage pkg = parse_repo_package(object);
            const auto it = previous_by_id.find(pkg.id);
            if (it != previous_by_id.end()) {
                pkg = merge_repo_package_download_url_fields(pkg, it->second);
            }
            incoming_pkgs.push_back(pkg);
        }
        save_repo_packages_index(incoming_pkgs, named);
    }
    rebuild_repo_index_from_cached_files({RepoEntry{"local", "/tmp/local-feed.json"}});
    const std::vector<RepoPackage> after_update = load_repo_packages();
    require(
        repo_package_download_url_for_arch(after_update[0], "x86_64").value_or("") ==
            "https://example/demo-x86_64.AppImage",
        "update merge keeps url");

    std::cout << "repo index persist merge smoke passed\n";
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/persist_merge_unit" \
  "$TMP_DIR/persist_merge_unit.cpp" \
  "$ROOT/src/repo_index_urls.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/terminal_color.cpp" \
  "$ROOT/src/website_resolve_cache.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  -pthread

"$TMP_DIR/persist_merge_unit"
