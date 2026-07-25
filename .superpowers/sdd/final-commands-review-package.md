# Final review — split commands.cpp
# SDD Progress — split commands.cpp

Workspace: /home/fsx/yai (in-place; no usable git)
Plan: docs/superpowers/plans/2026-07-25-split-commands-cpp.md
Spec: docs/superpowers/specs/2026-07-25-split-commands-cpp-design.md

Global constraints: no behavior/signature changes; no yai.hpp split; no commits; mechanical moves; commands_doctor untouched

## Tasks


Task 1: complete (review clean)

Task 2: complete (review clean)

Task 3: complete (review clean)

Task 4: complete (review clean)

Task 5: complete (all smokes pass; review pending)

Task 5: complete (review Approved)

   67 src/commands.cpp
  141 src/commands_doctor.cpp
  220 src/commands_lifecycle.cpp
  110 src/commands_query.cpp
  228 src/commands_repo.cpp
  580 src/commands_upgrade.cpp
 1346 总计

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
## Symbol ownership
### src/commands.cpp
5:namespace {
6:std::string command_match_list(const std::vector<std::string>& values) {
16:} // namespace
18:std::string resolve_installed_package_id(const std::string& pattern) {
61:void print_mode_line(const std::string& mode) {
65:void print_fuse_fallback_line() {
### src/commands_lifecycle.cpp
5:namespace {
7:std::string download_output_name(const ResolvedSource& source) {
18:} // namespace
20:void download_app(int argc, char** argv) {
54:void install_app(int argc, char** argv) {
110:RepairResult repair_installed_package(const std::string& id) {
153:void repair_app(int argc, char** argv) {
168:fs::path previous_version_dir(const InstallPaths& paths) {
172:void save_previous_version(const InstallPaths& paths) {
188:void restore_previous_version(const std::string& id) {
212:void rollback_app(int argc, char** argv) {
### src/commands_upgrade.cpp
5:namespace {
6:struct UpgradeCommandOptions {
54:struct UpdateContext {
67:std::string unsupported_update_reason(const std::string& source_kind) {
119:void print_already_up_to_date(const UpdateContext& context) {
153:bool update_source_identity_changed(const UpdateContext& context, const ResolvedSource& source) {
175:void print_update_start(
193:RepairResult download_and_probe_update_candidate(
206:void activate_update_appimage(const InstallPaths& paths, const InstallPaths& candidate_paths) {
215:void activate_update_extracted_dir(
233:void write_activated_update(
243:void activate_update_candidate(
253:void commit_update_transaction(
282:void print_update_result(
297:void upgrade_installed_target(const InstallOptions& options) {
334:std::string yes_no_prompt_text(std::size_t count) {
340:bool confirm_batch_upgrade(std::size_t count, bool yes) {
355:std::vector<std::string> installed_package_ids() {
380:std::string preview_value_or_dash(const std::string& value) {
384:struct UpdatePreviewResult {
392:bool preview_result_is_upgradable(const UpdatePreviewResult& result) {
396:void print_update_preview_row(const UpdatePreviewResult& result) {
487:std::vector<UpdatePreviewResult> build_update_previews(const std::vector<std::string>& ids) {
496:void print_update_previews(const std::vector<UpdatePreviewResult>& results) {
502:void upgrade_all_app(const UpgradeCommandOptions& command) {
541:} // namespace
543:void cleanup_update_candidate(const InstallPaths& candidate_paths) {
558:void upgrade_app(int argc, char** argv) {
568:void update_app(int argc, char** argv) {
### src/commands_query.cpp
5:namespace {
6:std::string search_summary(const std::string& summary) {
11:void print_package_source_reason(const RepoPackage& package) {
17:void print_package_source_info(const RepoPackage& package) {
31:} // namespace
33:void remove_if_exists(const fs::path& path) {
37:void remove_app(int argc, char** argv) {
56:void list_apps() {
78:void search_packages(int argc, char** argv) {
91:void info_package(int argc, char** argv) {
### src/commands_repo.cpp
5:void repo_list_app(int argc) {
16:void repo_add_app(int argc, char** argv) {
50:namespace {
52:std::optional<std::string> parse_repo_update_target(int argc, char** argv) {
62:void write_empty_repo_index() {
71:std::pair<RepoEntry, std::string> refresh_repo_index(const RepoEntry& entry) {
75:std::vector<std::pair<RepoEntry, std::string>> refresh_selected_repo_indexes(
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
### src/commands_doctor.cpp
108:void print_doctor_summary(int warnings) {
118:void doctor_app(int argc) {
139:void doctor_app_files(int& warnings) {
