#include "yai.hpp"

// Shared helpers used by multiple command-family translation units.

std::vector<std::string> resolve_installed_package_ids(const std::string& pattern) {
    if (!has_glob_wildcards(pattern)) {
        const std::string id = sanitize_id(pattern);
        const InstallPaths paths = paths_for(id);
        if (!metadata_exists(paths)) {
            // A directory without metadata.json is a leftover rather than an
            // installed package. Every caller here reads metadata, so none can
            // proceed; point at the one command that can reclaim the space.
            if (fs::exists(paths.app_dir)) {
                throw std::runtime_error(tr_format(
                    "package is not installed: {id} (app directory exists without metadata.json; reclaim the space with: yai remove {id})",
                    {{"{id}", id}}));
            }
            throw std::runtime_error(tr("package is not installed: ") + id);
        }
        return {id};
    }

    const fs::path apps_dir = expand_home_path(".local/share/yai/apps");
    std::vector<std::string> matches;
    if (fs::exists(apps_dir)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(apps_dir)) {
            if (!entry.is_directory()) {
                continue;
            }
            const std::string dir_id = entry.path().filename().string();
            const InstallPaths paths = paths_for(dir_id);
            const fs::path metadata = readable_metadata_path(paths);
            if (!fs::exists(metadata)) {
                continue;
            }
            const std::string id = metadata_value(metadata, "id").value_or(dir_id);
            if ((glob_match_case_insensitive(pattern, dir_id) ||
                 glob_match_case_insensitive(pattern, id)) &&
                std::find(matches.begin(), matches.end(), id) == matches.end()) {
                matches.push_back(id);
            }
        }
    }
    std::sort(matches.begin(), matches.end());

    if (matches.empty()) {
        throw std::runtime_error(tr("package pattern matched no installed packages: ") + pattern);
    }
    return matches;
}

std::string resolve_installed_package_id(const std::string& pattern) {
    const std::vector<std::string> matches = resolve_installed_package_ids(pattern);
    if (matches.size() > 1) {
        throw std::runtime_error(tr("multi-match pattern requires list resolver"));
    }
    return matches.front();
}

// App directories that hold no metadata.json. Under the JSON-only metadata
// standard these are leftovers -- an interrupted install, or a directory left
// by a yai version that predates JSON metadata -- and not installed packages.
// They are reported by `doctor` and reclaimed by `remove`, but never counted
// as installed.
std::vector<std::string> stale_app_dir_ids() {
    const fs::path apps_dir = expand_home_path(".local/share/yai/apps");
    std::vector<std::string> ids;
    if (!fs::exists(apps_dir)) {
        return ids;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(apps_dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string dir_id = entry.path().filename().string();
        if (fs::exists(readable_metadata_path(paths_for(dir_id)))) {
            continue;
        }
        ids.push_back(dir_id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Like resolve_installed_package_ids, but stale app directories match too, so
// `remove` can reclaim the disk they occupy. Commands that read metadata
// (repair, rollback, update) must keep using the stricter resolver, which
// requires metadata.json to exist.
std::vector<std::string> resolve_removable_package_ids(const std::string& pattern) {
    if (!has_glob_wildcards(pattern)) {
        const std::string id = sanitize_id(pattern);
        const InstallPaths paths = paths_for(id);
        if (!metadata_exists(paths) && !fs::exists(paths.app_dir)) {
            throw std::runtime_error(tr("package is not installed: ") + id);
        }
        return {id};
    }

    const fs::path apps_dir = expand_home_path(".local/share/yai/apps");
    std::vector<std::string> matches;
    if (fs::exists(apps_dir)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(apps_dir)) {
            if (!entry.is_directory()) {
                continue;
            }
            const std::string dir_id = entry.path().filename().string();
            const InstallPaths paths = paths_for(dir_id);
            // A stale directory has no id field to read, so it can only ever
            // be matched by its directory name.
            const std::string id =
                fs::exists(readable_metadata_path(paths))
                    ? metadata_value(readable_metadata_path(paths), "id").value_or(dir_id)
                    : dir_id;
            if ((glob_match_case_insensitive(pattern, dir_id) ||
                 glob_match_case_insensitive(pattern, id)) &&
                std::find(matches.begin(), matches.end(), id) == matches.end()) {
                matches.push_back(id);
            }
        }
    }
    std::sort(matches.begin(), matches.end());

    if (matches.empty()) {
        throw std::runtime_error(tr("package pattern matched no installed packages: ") + pattern);
    }
    return matches;
}

bool confirm_multi_match(
    const std::string& prompt,
    const std::vector<std::string>& matches,
    bool yes) {
    for (const std::string& match : matches) {
        std::cerr << match << "\n";
    }
    if (yes) {
        return true;
    }
    std::cerr << prompt;
    std::string answer;
    if (!std::getline(std::cin, answer)) {
        std::cerr << "\n";
        return false;
    }
    answer = to_lower(trim(answer));
    return answer == "y" || answer == "yes";
}

void print_mode_line(const std::string& mode) {
    std::cout << tr("Mode: ") << mode << "\n";
}

void print_fuse_fallback_line() {
    std::cout << tr("FUSE problem detected; yai selected a fallback mode.\n");
}
