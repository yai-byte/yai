#include "yai.hpp"

// Upgrade / update preview and apply workflows.

namespace {
struct UpgradeCommandOptions {
    InstallOptions options;
    bool all = false;
    bool yes = false;
};

UpgradeCommandOptions parse_upgrade_command_options(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(tr("upgrade requires a package id or --all"));
    }

    UpgradeCommandOptions command;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") {
            command.all = true;
        } else if (arg == "--yes" || arg == "-y") {
            command.yes = true;
        } else if (arg == "--download") {
            parse_download_strategy_option(command.options, read_option_value(argc, argv, i, arg));
        } else if (arg == "--mirror-template") {
            parse_mirror_template_option(command.options, read_option_value(argc, argv, i, arg));
        } else if (arg == "--downloader") {
            parse_downloader_option(command.options, read_option_value(argc, argv, i, arg));
        } else if (arg.rfind("--", 0) == 0) {
            throw std::runtime_error(tr("unknown upgrade option: ") + arg);
        } else if (command.options.target.empty()) {
            command.options.target = arg;
        } else {
            throw std::runtime_error(tr("upgrade accepts one package id or --all"));
        }
    }

    if (command.all && !command.options.target.empty()) {
        throw std::runtime_error(tr("upgrade --all does not accept a package id"));
    }
    if (!command.all && command.options.target.empty()) {
        throw std::runtime_error(tr("upgrade requires a package id or --all"));
    }
    if (contains_line_break(command.options.target) ||
        contains_line_break(command.options.mirror_template) ||
        contains_line_break(command.options.downloader)) {
        throw std::runtime_error(tr("upgrade arguments must not contain line breaks"));
    }
    validate_mirror_options(command.options);
    return command;
}

struct UpdateContext {
    InstallOptions options;
    std::string id;
    InstallPaths paths;
    std::string source_kind;
    std::string current_version;
    std::string name;
    std::string source_url;
    std::string github_owner;
    std::string github_repo;
    std::string installed_arch;
};

std::string unsupported_update_reason(const std::string& source_kind) {
    if (source_kind == "local_path") {
        return tr("local AppImage path has no queryable update source");
    }
    if (source_kind == "url") {
        return tr("plain URL install has no stable update source");
    }
    if (source_kind == "unavailable") {
        return tr("package source is unavailable");
    }
    return tr("source kind is not upgradable: ") + source_kind;
}

UpdateContext load_update_context(const InstallOptions& options) {
    const std::string id = resolve_installed_package_id(options.target);
    const InstallPaths paths = paths_for(id);
    if (!metadata_exists(paths)) {
        throw std::runtime_error(tr("package is not installed: ") + id);
    }

    const fs::path metadata = readable_metadata_path(paths);
    const std::string source_kind = metadata_value(metadata, "source_kind").value_or("url");
    const std::string current_version = metadata_value(metadata, "version").value_or("");
    const std::string name = metadata_value(metadata, "name").value_or(id);
    const std::string source_url = metadata_value(metadata, "source_url").value_or("");
    const std::string github_owner = metadata_value(metadata, "github_owner").value_or("");
    const std::string github_repo = metadata_value(metadata, "github_repo").value_or("");
    const std::string installed_arch = metadata_value(metadata, "arch").value_or(current_arch());
    if (source_kind != "github_release" &&
        source_kind != "repo_github_release" &&
        source_kind != "repo_direct_url" &&
        source_kind != "repo_website_page") {
        throw std::runtime_error(tr("package is not upgradable: ") + unsupported_update_reason(source_kind));
    }
    if ((source_kind == "github_release" || source_kind == "repo_github_release") &&
        (github_owner.empty() || github_repo.empty())) {
        throw std::runtime_error(tr("package is not upgradable: GitHub metadata is missing for ") + id);
    }

    return UpdateContext{
        options,
        id,
        paths,
        source_kind,
        current_version,
        name,
        source_url,
        github_owner,
        github_repo,
        installed_arch};
}

void print_already_up_to_date(const UpdateContext& context) {
    const std::string current = context.current_version.empty() ? "-" : context.current_version;
    std::cout << tr_format(
        "{id} is already up to date ({version})\n",
        {{"{id}", context.id}, {"{version}", current}});
}

ResolvedSource build_update_source(const UpdateContext& context, const GitHubRelease& release) {
    ResolvedSource source;
    source.source_kind = context.source_kind;
    source.id = context.id;
    source.name = context.name;
    source.version = release.tag;
    source.source_url = release.asset.url;
    source.download_url = release.asset.url;
    source.github_owner = release.owner;
    source.github_repo = release.repo;
    source.github_asset = release.asset.name;
    source.arch = context.installed_arch;
    return source;
}

InstallOptions update_resolution_options(const UpdateContext& context) {
    InstallOptions options = context.options;
    options.target = context.id;
    options.id = context.id;
    options.name = context.name;
    options.target_arch = context.installed_arch;
    options.id_explicit = true;
    options.name_explicit = true;
    options.arch_explicit = true;
    return options;
}

bool update_source_identity_changed(const UpdateContext& context, const ResolvedSource& source) {
    if (!source.version.empty() && !context.current_version.empty()) {
        return source.version != context.current_version;
    }
    return source.source_url != context.source_url;
}

ResolvedSource resolve_repo_update_source(const UpdateContext& context) {
    ResolvedSource source = resolve_install_source(update_resolution_options(context));
    source.id = context.id;
    source.name = context.name;
    source.arch = context.installed_arch;
    return source;
}

InstallPaths update_candidate_paths(const InstallPaths& paths) {
    InstallPaths candidate_paths = paths;
    candidate_paths.appimage = paths.app_dir / "update.AppImage";
    candidate_paths.extracted_dir = paths.app_dir / "update-extracted";
    return candidate_paths;
}

void print_update_start(
    const UpdateContext& context,
    const ResolvedSource& source,
    const InstallOptions& effective_options) {
    const std::string current = context.current_version.empty() ? "-" : context.current_version;
    const std::string latest = source.version.empty() ? "-" : source.version;
    std::cout << tr_format(
        "Upgrading {id} from {current} to {latest}\n",
        {{"{id}", context.id}, {"{current}", current}, {"{latest}", latest}});
    if (effective_options.download_strategy != "direct") {
        std::cout << tr("Using GitHub Release proxy strategy: ")
                  << effective_options.download_strategy << "\n";
        std::cout << tr("Upstream: ") << source.source_url << "\n";
    } else {
        std::cout << tr("Downloading ") << source.source_url << "\n";
    }
}

RepairResult download_and_probe_update_candidate(
    ResolvedSource& source,
    const InstallOptions& effective_options,
    const InstallPaths& candidate_paths) {
    source.download_url = stage_appimage_source(source, effective_options, candidate_paths.appimage);
    const RepairResult repair = detect_run_mode(candidate_paths);
    if (repair.mode == "failed") {
        cleanup_update_candidate(candidate_paths);
        throw std::runtime_error(tr("updated AppImage is not runnable; current installation was not changed"));
    }
    return repair;
}

void activate_update_appimage(const InstallPaths& paths, const InstallPaths& candidate_paths) {
    remove_required(paths.appimage, tr("activating updated AppImage"));
    std::error_code ec;
    fs::rename(candidate_paths.appimage, paths.appimage, ec);
    if (ec) {
        throw std::runtime_error(tr("failed to activate updated AppImage: ") + ec.message());
    }
}

void activate_update_extracted_dir(
    const InstallPaths& paths,
    const InstallPaths& candidate_paths,
    const RepairResult& repair) {
    remove_all_required(paths.extracted_dir, tr("activating updated extracted directory"));

    if (repair.mode == "extracted") {
        std::error_code ec;
        fs::rename(candidate_paths.extracted_dir, paths.extracted_dir, ec);
        if (ec) {
            throw std::runtime_error(tr("failed to activate updated extracted directory: ") + ec.message());
        }
    } else {
        const bool removed = remove_all_best_effort(candidate_paths.extracted_dir);
        (void)removed; // No extracted runtime is needed for direct or extract-and-run updates.
    }
}

void write_activated_update(
    const InstallPaths& paths,
    const ResolvedSource& source,
    const RepairResult& repair) {
    write_wrapper(paths, repair.mode);
    write_desktop_entry(paths, source.name);
    write_metadata(paths, source, repair.mode);
    run_process({"update-desktop-database", paths.desktop.parent_path().string()});
}

void activate_update_candidate(
    const InstallPaths& paths,
    const InstallPaths& candidate_paths,
    const ResolvedSource& source,
    const RepairResult& repair) {
    activate_update_appimage(paths, candidate_paths);
    activate_update_extracted_dir(paths, candidate_paths, repair);
    write_activated_update(paths, source, repair);
}

void commit_update_transaction(
    const UpdateContext& context,
    const InstallPaths& candidate_paths,
    const ResolvedSource& source,
    const RepairResult& repair) {
    // Update has already downloaded and probed the candidate. Commit starts by
    // saving the known-good current version, then activates the candidate; if
    // activation fails after that point, restore_previous_version repairs the
    // install back to a runnable state.
    bool previous_saved = false;
    try {
        save_previous_version(context.paths);
        previous_saved = true;
        activate_update_candidate(context.paths, candidate_paths, source, repair);
    } catch (const std::exception& ex) {
        cleanup_update_candidate(candidate_paths);
        if (previous_saved) {
            try {
                restore_previous_version(context.id);
            } catch (const std::exception& restore_ex) {
                throw std::runtime_error(
                    std::string(tr("upgrade failed and rollback failed: ")) +
                    ex.what() + tr("; rollback error: ") + restore_ex.what());
            }
        }
        throw std::runtime_error(std::string(tr("upgrade failed; restored previous version: ")) + ex.what());
    }
}

void print_update_result(
    const UpdateContext& context,
    const ResolvedSource& source,
    const RepairResult& repair) {
    std::cout << tr_format(
        "Upgraded {id} to {version}\n",
        {{"{id}", context.id}, {"{version}", source.version}});
    std::cout << tr("Downloaded from: ") << source.download_url << "\n";
    print_mode_line(repair.mode);
    if (repair.fuse_error_detected) {
        print_fuse_fallback_line();
    }
    std::cout << tr("Rollback version: ") << previous_version_dir(context.paths) << "\n";
}

void upgrade_installed_target(const InstallOptions& options) {
    // Upgrade reuses the installed source metadata. GitHub sources query the
    // release API; repo direct/website sources resolve the current repo package
    // again and compare the resulting AppImage identity.
    // The transaction order is: parse original install metadata -> resolve next
    // source -> download/probe candidate -> save previous -> activate candidate
    // -> write updated metadata and launch files -> clean temporary candidate
    // files.
    const UpdateContext context = load_update_context(options);

    ResolvedSource source;
    if (context.source_kind == "github_release" || context.source_kind == "repo_github_release") {
        const GitHubRelease release = resolve_github_latest(
            context.github_owner + "/" + context.github_repo,
            "",
            context.installed_arch);
        source = build_update_source(context, release);
    } else {
        source = resolve_repo_update_source(context);
    }
    if (!update_source_identity_changed(context, source)) {
        print_already_up_to_date(context);
        return;
    }
    const InstallOptions effective_options = apply_network_config_to_options(options, source);

    const InstallPaths candidate_paths = update_candidate_paths(context.paths);
    cleanup_update_candidate(candidate_paths);

    print_update_start(context, source, effective_options);
    const RepairResult repair = download_and_probe_update_candidate(source, effective_options, candidate_paths);

    commit_update_transaction(context, candidate_paths, source, repair);
    cleanup_update_candidate(candidate_paths);
    print_update_result(context, source, repair);
}

std::string yes_no_prompt_text(std::size_t count) {
    return tr_format(
        "Upgrade {count} package(s)? [y/N] ",
        {{"{count}", std::to_string(count)}});
}

bool confirm_batch_upgrade(std::size_t count, bool yes) {
    if (yes) {
        return true;
    }

    std::cerr << yes_no_prompt_text(count);
    std::string answer;
    if (!std::getline(std::cin, answer)) {
        std::cerr << "\n";
        return false;
    }
    answer = to_lower(trim(answer));
    return answer == "y" || answer == "yes";
}

std::vector<std::string> installed_package_ids() {
    const fs::path apps_dir = expand_home_path(".local/share/yai/apps");
    std::vector<std::string> ids;
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
        const std::string id = metadata_value(metadata, "id").value_or(entry.path().filename().string());
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string preview_value_or_dash(const std::string& value) {
    return value.empty() ? "-" : value;
}

struct UpdatePreviewResult {
    std::string id;
    std::string current_version;
    std::string latest_version;
    std::string status;
    std::string reason;
};

bool preview_result_is_upgradable(const UpdatePreviewResult& result) {
    return result.status == "upgradable";
}

void print_update_preview_row(const UpdatePreviewResult& result) {
    std::cout << result.id << "\t"
              << preview_value_or_dash(result.current_version) << "\t"
              << preview_value_or_dash(result.latest_version) << "\t"
              << result.status << "\t"
              << result.reason << "\n";
}

UpdatePreviewResult build_update_preview(const std::string& id) {
    const InstallPaths paths = paths_for(id);
    if (!metadata_exists(paths)) {
        throw std::runtime_error(tr("package is not installed: ") + id);
    }

    const fs::path metadata = readable_metadata_path(paths);
    const std::string source_kind = metadata_value(metadata, "source_kind").value_or("url");
    const std::string current_version = metadata_value(metadata, "version").value_or("");
    const std::string name = metadata_value(metadata, "name").value_or(id);
    const std::string source_url = metadata_value(metadata, "source_url").value_or("");
    const std::string github_owner = metadata_value(metadata, "github_owner").value_or("");
    const std::string github_repo = metadata_value(metadata, "github_repo").value_or("");
    const std::string installed_arch = metadata_value(metadata, "arch").value_or(current_arch());

    if (source_kind == "repo_direct_url" || source_kind == "repo_website_page") {
        try {
            UpdateContext context{
                InstallOptions{},
                id,
                paths,
                source_kind,
                current_version,
                name,
                source_url,
                github_owner,
                github_repo,
                installed_arch};
            ResolvedSource source = resolve_repo_update_source(context);
            if (!update_source_identity_changed(context, source)) {
                return UpdatePreviewResult{
                    id,
                    current_version,
                    source.version,
                    "current",
                    tr("already up to date")};
            }
            return UpdatePreviewResult{
                id,
                current_version,
                source.version,
                "upgradable",
                tr("source: ") + source.source_url};
        } catch (const std::exception& ex) {
            return UpdatePreviewResult{id, current_version, "", "error", ex.what()};
        }
    }

    if ((source_kind != "github_release" && source_kind != "repo_github_release") ||
        github_owner.empty() ||
        github_repo.empty()) {
        return UpdatePreviewResult{
            id,
            current_version,
            "",
            "unsupported",
            unsupported_update_reason(source_kind)};
    }

    try {
        const GitHubRelease release = resolve_github_latest(
            github_owner + "/" + github_repo,
            "",
            installed_arch);
        if (release.tag == current_version) {
            return UpdatePreviewResult{
                id,
                current_version,
                release.tag,
                "current",
                tr("already up to date")};
        }
        return UpdatePreviewResult{
            id,
            current_version,
            release.tag,
            "upgradable",
            tr("asset: ") + release.asset.name};
    } catch (const std::exception& ex) {
        return UpdatePreviewResult{id, current_version, "", "error", ex.what()};
    }
}

std::vector<UpdatePreviewResult> build_update_previews(const std::vector<std::string>& ids) {
    std::vector<UpdatePreviewResult> results;
    results.reserve(ids.size());
    for (const std::string& id : ids) {
        results.push_back(build_update_preview(id));
    }
    return results;
}

void print_update_previews(const std::vector<UpdatePreviewResult>& results) {
    for (const UpdatePreviewResult& result : results) {
        print_update_preview_row(result);
    }
}

void upgrade_all_app(const UpgradeCommandOptions& command) {
    const std::vector<UpdatePreviewResult> previews = build_update_previews(installed_package_ids());
    print_update_previews(previews);

    std::vector<std::string> upgrade_ids;
    for (const UpdatePreviewResult& result : previews) {
        if (preview_result_is_upgradable(result)) {
            upgrade_ids.push_back(result.id);
        }
    }

    if (upgrade_ids.empty()) {
        std::cout << tr("No upgradable packages\n");
        return;
    }
    if (!confirm_batch_upgrade(upgrade_ids.size(), command.yes)) {
        std::cout << tr("Upgrade cancelled\n");
        return;
    }

    std::size_t failed = 0;
    for (const std::string& id : upgrade_ids) {
        InstallOptions options = command.options;
        options.target = id;
        try {
            upgrade_installed_target(options);
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << tr("yai: upgrade failed for ") << id << tr(": ") << ex.what() << "\n";
        }
    }
    if (failed > 0) {
        throw std::runtime_error(tr_format(
            "{failed} of {total} upgrade task(s) failed",
            {{"{failed}", std::to_string(failed)},
             {"{total}", std::to_string(upgrade_ids.size())}}));
    }
}

} // namespace

void cleanup_update_candidate(const InstallPaths& candidate_paths) {
    // Candidate files are disposable. Cleanup failures should not hide the
    // download/probe/transaction error that caused this path.
    const bool appimage_removed = remove_best_effort(candidate_paths.appimage);
    const bool part_removed = remove_best_effort(candidate_paths.appimage.string() + ".part");
    const bool headers_removed = remove_best_effort(candidate_paths.appimage.string() + ".headers");
    const bool aria_removed = remove_best_effort(candidate_paths.appimage.string() + ".part.aria2");
    const bool extracted_removed = remove_all_best_effort(candidate_paths.extracted_dir);
    (void)appimage_removed;
    (void)part_removed;
    (void)headers_removed;
    (void)aria_removed;
    (void)extracted_removed;
}

void upgrade_app(int argc, char** argv) {
    const UpgradeCommandOptions command = parse_upgrade_command_options(argc, argv);
    if (command.all) {
        upgrade_all_app(command);
        return;
    }

    upgrade_installed_target(command.options);
}

void update_app(int argc, char** argv) {
    if (argc > 3) {
        throw std::runtime_error(tr("update accepts zero or one package id"));
    }

    if (argc == 3) {
        print_update_preview_row(build_update_preview(resolve_installed_package_id(argv[2])));
        return;
    }

    print_update_previews(build_update_previews(installed_package_ids()));
}

