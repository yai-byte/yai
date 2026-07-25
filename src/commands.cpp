#include "yai.hpp"

// Shared helpers used by multiple command-family translation units.

namespace {
std::string command_match_list(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += values[i];
    }
    return out;
}
} // namespace

std::string resolve_installed_package_id(const std::string& pattern) {
    if (!has_glob_wildcards(pattern)) {
        const std::string id = sanitize_id(pattern);
        if (!metadata_exists(paths_for(id))) {
            throw std::runtime_error(tr("package is not installed: ") + id);
        }
        return id;
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
    if (matches.size() > 1) {
        throw std::runtime_error(tr_format(
            "package pattern is ambiguous: {pattern} (matches: {matches})",
            {{"{pattern}", pattern}, {"{matches}", command_match_list(matches)}}));
    }
    return matches.front();
}

void print_mode_line(const std::string& mode) {
    std::cout << tr("Mode: ") << mode << "\n";
}

void print_fuse_fallback_line() {
    std::cout << tr("FUSE problem detected; yai selected a fallback mode.\n");
}
