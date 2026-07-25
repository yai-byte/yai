# Task 4 review
   67 src/commands.cpp
  141 src/commands_doctor.cpp
  220 src/commands_lifecycle.cpp
  110 src/commands_query.cpp
  228 src/commands_repo.cpp
  580 src/commands_upgrade.cpp
 1346 总计
## Makefile SRC
SRC := \
	src/arch.cpp \
	src/appimage.cpp \
	src/cli_download.cpp \
	src/commands.cpp \
	src/commands_doctor.cpp \
	src/commands_lifecycle.cpp \
	src/commands_query.cpp \
	src/commands_repo.cpp \
	src/commands_upgrade.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/resolver.cpp

## thin commands.cpp
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
## repo symbols
5:void repo_list_app(int argc) {
16:void repo_add_app(int argc, char** argv) {
50:namespace {
52:std::optional<std::string> parse_repo_update_target(int argc, char** argv) {
62:void write_empty_repo_index() {
71:std::pair<RepoEntry, std::string> refresh_repo_index(const RepoEntry& entry) {
91:void store_repo_index_updates(const std::vector<std::pair<RepoEntry, std::string>>& fetched) {
98:void print_repo_update_result(std::size_t count) {
104:} // namespace
106:void repo_update_app(int argc, char** argv) {
123:void repo_app(int argc, char** argv) {
140:void mirror_list_app(int argc) {
157:void mirror_use_app(int argc, char** argv) {
180:void mirror_custom_app(int argc, char** argv) {
197:void mirror_off_app(int argc) {
211:void mirror_app(int argc, char** argv) {
## no repo left in commands
(none — good)
