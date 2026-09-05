#include "yai.hpp"

#include <unordered_set>

// Remove, list, search, and info command workflows.

namespace {
std::unordered_set<std::string> installed_package_id_set() {
    std::unordered_set<std::string> ids;
    const fs::path apps_dir = expand_home_path(".local/share/yai/apps");
    if (!fs::exists(apps_dir)) {
        return ids;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(apps_dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        const InstallPaths paths = paths_for(entry.path().filename().string());
        const fs::path metadata = readable_metadata_path(paths);
        if (!fs::exists(metadata)) {
            continue;
        }
        const std::string id = metadata_json_value(metadata, "id").value_or(entry.path().filename().string());
        ids.insert(id);
    }
    return ids;
}

std::string search_summary(const std::string& summary) {
    constexpr std::size_t max_summary_width = 80;
    return truncate_display_width(summary, max_summary_width);
}

void print_package_source_reason(const RepoPackage& package) {
    if (!package.source_reason.empty()) {
        std::cout << tr("Reason: ") << package.source_reason << "\n";
    }
}

void print_package_source_info(const RepoPackage& package) {
    // Show source origin for packages from AppImage GitHub apps/
    if (package.source_origin == "appimage_apps") {
        std::cout << tr("Source origin: AppImage GitHub (apps/)\n");
    }

    if (package.source_type == "github_release") {
        std::cout << tr_format(
            "Source: {type} {owner}/{repo}\n",
            {{"{type}", package.source_type},
             {"{owner}", package.source_owner},
             {"{repo}", package.source_repo}});
    } else if (package.source_type == "direct_url") {
        std::cout << tr("Source: direct_url ") << package.source_url << "\n";
    } else if (package.source_type == "website_page") {
        std::cout << tr("Source: website_page ") << package.source_url << "\n";
        if (is_appimage_catalog_url(package.source_url)) {
            std::cout << tr("  (AppImageHub catalog page — yai will attempt to find the real download source from the catalog during install)\n");
        }
        print_package_source_reason(package);
    } else if (package.source_type == "unavailable") {
        std::cout << tr("Source: unavailable — cannot be installed automatically\n");
        print_package_source_reason(package);
        if (!package.homepage.empty()) {
            std::cout << tr("Homepage: ") << package.homepage << "\n";
        }
        std::cout << tr_format(
            "Visit the homepage or AppImageHub page to manually download: {homepage}\n",
            {{"{homepage}", package.homepage}});
    } else {
        std::cout << tr("Source: unavailable\n");
        print_package_source_reason(package);
    }
}

void print_package_info(const RepoPackage& package) {
    std::cout << tr("Id: ") << package.id << "\n";
    std::cout << tr("Name: ") << package.name << "\n";
    std::cout << tr("Summary: ") << package.summary << "\n";
    std::cout << tr("Homepage: ") << package.homepage << "\n";
    std::cout << tr("License: ") << package.license << "\n";
    if (!package.arch.empty()) {
        std::cout << tr("Architecture: ") << package.arch << "\n";
    }
    if (!package.version.empty()) {
        std::cout << tr("AppVersion: ") << package.version << "\n";
    }
    print_package_source_info(package);
    if (!package.asset_pattern.empty()) {
        std::cout << tr("Asset pattern: ") << package.asset_pattern << "\n";
    }
}

bool looks_like_shell_expanded_remove_target(const std::string& value) {
    if (value.empty() || has_glob_wildcards(value)) {
        return false;
    }
    const std::string lower = to_lower(value);
    return value.front() == '/' ||
           value.rfind("./", 0) == 0 ||
           value.rfind("../", 0) == 0 ||
           lower.find(".appimage") != std::string::npos;
}
} // namespace

void remove_if_exists(const fs::path& path) {
    remove_required(path, tr("removing installed package file"));
}

void remove_installed_id(const std::string& id) {
    const InstallPaths paths = paths_for(id);
    // A directory without metadata.json is a leftover rather than an installed
    // package, but deleting it is the only way to reclaim its disk space, so
    // it is accepted as long as the app directory itself exists.
    if (!metadata_exists(paths) && !fs::exists(paths.app_dir)) {
        throw std::runtime_error(tr("package is not installed: ") + id);
    }

    remove_if_exists(paths.wrapper);
    remove_if_exists(paths.desktop);

    remove_all_required(paths.app_dir, tr("removing installed package directory"));

    run_process({"update-desktop-database", paths.desktop.parent_path().string()});
    std::cout << tr("Removed ") << id << "\n";
}

void remove_app(int argc, char** argv) {
    bool yes = false;
    std::string pattern;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--yes" || arg == "-y") {
            yes = true;
        } else if (arg.rfind("--", 0) == 0) {
            throw std::runtime_error(tr("unknown remove option: ") + arg);
        } else if (pattern.empty()) {
            pattern = arg;
        } else {
            throw std::runtime_error(tr(
                "remove accepts one package id or quoted pattern; quote wildcards like 'name*' so the shell does not expand them"));
        }
    }
    if (pattern.empty()) {
        throw std::runtime_error(tr("remove requires exactly one package id"));
    }
    if (looks_like_shell_expanded_remove_target(pattern)) {
        const std::string id = sanitize_id(pattern);
        const InstallPaths paths = paths_for(id);
        if (!metadata_exists(paths)) {
            throw std::runtime_error(tr(
                "remove matches installed package ids only; quote wildcards like 'name*' so the shell does not expand them"));
        }
    }

    // A directory without metadata.json is a leftover, not an installed package.
    // remove_installed_id already knows how to reclaim it, so short-circuit
    // before resolve_installed_package_ids would reject it as "not installed".
    if (!has_glob_wildcards(pattern)) {
        const std::string id = sanitize_id(pattern);
        const InstallPaths paths = paths_for(id);
        if (!metadata_exists(paths) && fs::exists(paths.app_dir)) {
            remove_installed_id(id);
            return;
        }
    }

    const std::vector<std::string> ids = resolve_installed_package_ids(pattern);
    if (ids.size() > 1) {
        const std::string prompt = tr_format(
            "Remove {count} package(s)? [y/N] ",
            {{"{count}", std::to_string(ids.size())}});
        if (!confirm_multi_match(prompt, ids, yes)) {
            std::cout << tr("Remove cancelled\n");
            return;
        }
    }

    for (const std::string& id : ids) {
        remove_installed_id(id);
    }
}

void list_apps() {
    const fs::path apps_dir = expand_home_path(".local/share/yai/apps");
    if (!fs::exists(apps_dir)) {
        return;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(apps_dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        const InstallPaths paths = paths_for(entry.path().filename().string());
        const fs::path metadata = readable_metadata_path(paths);
        if (!fs::exists(metadata)) {
            continue;
        }
        const std::string id = metadata_json_value(metadata, "id").value_or(entry.path().filename().string());
        const std::string name = metadata_json_value(metadata, "name").value_or(id);
        const std::string mode = metadata_json_value(metadata, "install_mode").value_or("unknown");
        std::cout << id << "\t" << name << "\t" << mode << "\n";
    }
}

void search_packages(int argc, char** argv) {
    if (argc != 3) {
        throw std::runtime_error(tr("search requires exactly one keyword"));
    }
    const std::string keyword = argv[2];
    const std::unordered_set<std::string> installed_ids = installed_package_id_set();
    for (const RepoPackage& package : load_repo_packages()) {
        if (!package_matches_keyword(package, keyword)) {
            continue;
        }
        std::string line = package.id + "\t" + package.name + "\t" + search_summary(package.summary);
        if (installed_ids.count(package.id) != 0) {
            line += " ";
            line += tr("[installed]");
            line = color_green(line);
        }
        std::cout << line << "\n";
    }
}

void info_package(int argc, char** argv) {
    if (argc != 3) {
        throw std::runtime_error(tr("info requires exactly one package id"));
    }
    const std::string id = argv[2];
    const std::vector<RepoPackage> packages = find_repo_packages(id);
    if (packages.empty()) {
        throw std::runtime_error(tr("package not found in repo index: ") + id);
    }

    for (std::size_t i = 0; i < packages.size(); ++i) {
        if (i > 0) {
            std::cout << "\n";
        }
        print_package_info(packages[i]);
    }
}
