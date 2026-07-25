#include "yai.hpp"

// Remove, list, search, and info command workflows.

namespace {
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
    if (package.source_type == "github_release") {
        std::cout << tr("Source: ") << package.source_type << " "
                  << package.source_owner << "/" << package.source_repo << "\n";
    } else if (package.source_type == "direct_url") {
        std::cout << tr("Source: direct_url ") << package.source_url << "\n";
    } else if (package.source_type == "website_page") {
        std::cout << tr("Source: website_page ") << package.source_url << "\n";
        print_package_source_reason(package);
    } else {
        std::cout << tr("Source: unavailable\n");
        print_package_source_reason(package);
    }
}
} // namespace

void remove_if_exists(const fs::path& path) {
    remove_required(path, tr("removing installed package file"));
}

void remove_app(int argc, char** argv) {
    if (argc != 3) {
        throw std::runtime_error(tr("remove requires exactly one package id"));
    }
    const std::string id = resolve_installed_package_id(argv[2]);
    const InstallPaths paths = paths_for(id);
    if (!metadata_exists(paths)) {
        throw std::runtime_error(tr("package is not installed: ") + id);
    }

    remove_if_exists(paths.wrapper);
    remove_if_exists(paths.desktop);

    remove_all_required(paths.app_dir, tr("removing installed package directory"));

    run_process({"update-desktop-database", paths.desktop.parent_path().string()});
    std::cout << tr("Removed ") << id << "\n";
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
        const std::string id = metadata_value(metadata, "id").value_or(entry.path().filename().string());
        const std::string name = metadata_value(metadata, "name").value_or(id);
        const std::string mode = metadata_value(metadata, "install_mode").value_or("unknown");
        std::cout << id << "\t" << name << "\t" << mode << "\n";
    }
}

void search_packages(int argc, char** argv) {
    if (argc != 3) {
        throw std::runtime_error(tr("search requires exactly one keyword"));
    }
    const std::string keyword = argv[2];
    for (const RepoPackage& package : load_repo_packages()) {
        if (!package_matches_keyword(package, keyword)) {
            continue;
        }
        std::string line = package.id + "\t" + package.name + "\t" + search_summary(package.summary);
        if (metadata_exists(paths_for(package.id))) {
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
    const std::optional<RepoPackage> package = find_repo_package(id);
    if (!package.has_value()) {
        throw std::runtime_error(tr("package not found in repo index: ") + id);
    }

    std::cout << tr("Id: ") << package->id << "\n";
    std::cout << tr("Name: ") << package->name << "\n";
    std::cout << tr("Summary: ") << package->summary << "\n";
    std::cout << tr("Homepage: ") << package->homepage << "\n";
    std::cout << tr("License: ") << package->license << "\n";
    print_package_source_info(*package);
    if (!package->asset_pattern.empty()) {
        std::cout << tr("Asset pattern: ") << package->asset_pattern << "\n";
    }
}
