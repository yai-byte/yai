#include "yai.hpp"

// Download, install, repair, and rollback command workflows.

namespace {

std::string download_output_name(const ResolvedSource& source) {
    if (source.id.empty()) {
        return "download.AppImage";
    }
    return source.id + ".AppImage";
}

std::string install_arch_for_options(const InstallOptions& options) {
    return options.target_arch.empty() ? current_arch() : normalize_arch(options.target_arch);
}

struct RepoIndexUrlResolveState {
    bool is_repo_package = false;
    bool had_index_url = false;
    bool lacked_index_url = false;
    std::string arch;
};

RepoIndexUrlResolveState inspect_repo_index_url_state(const InstallOptions& options) {
    RepoIndexUrlResolveState state;
    if (!looks_like_repo_package_target(options.target)) {
        return state;
    }
    const std::optional<RepoPackage> package = find_repo_package(options.target);
    if (!package.has_value()) {
        return state;
    }
    state.is_repo_package = true;
    state.arch = install_arch_for_options(options);
    state.had_index_url = repo_package_has_download_url_for_arch(*package, state.arch);
    state.lacked_index_url = !state.had_index_url;
    return state;
}

void stage_resolved_source_with_index_fallback(
    const InstallOptions& options,
    const InstallOptions& effective_options,
    const RepoIndexUrlResolveState& index_state,
    ResolvedSource& source,
    const fs::path& target) {
    // Callers apply network config once for the banner + first staging attempt.
    // Re-apply only on recrawl retry, when the resolved source identity may change.
    try {
        source.download_url = stage_appimage_source(source, effective_options, target);
        return;
    } catch (const std::exception&) {
        if (options.recrawl || !index_state.is_repo_package || !index_state.had_index_url) {
            throw;
        }
        InstallOptions retry = options;
        retry.recrawl = true;
        source = resolve_install_source(retry);
        const InstallOptions retry_effective = apply_network_config_to_options(retry, source);
        source.download_url = stage_appimage_source(source, retry_effective, target);
    }
}

void maybe_write_back_index_download_url(
    const InstallOptions& options,
    const RepoIndexUrlResolveState& index_state,
    const ResolvedSource& source) {
    if (!index_state.is_repo_package || !index_state.lacked_index_url) {
        return;
    }
    if (!repo_index_is_locally_writable()) {
        return;
    }
    if (source.download_url.empty()) {
        return;
    }
    std::optional<RepoPackage> package = find_repo_package(options.target);
    if (!package.has_value()) {
        return;
    }
    const std::string arch = source.arch.empty() ? index_state.arch : source.arch;
    repo_package_set_download_url(*package, arch, source.download_url, false, true);
    upsert_repo_package_download_urls(*package);
}

void print_download_progress_banner(const InstallOptions& effective_options, const ResolvedSource& source) {
    if (effective_options.download_strategy != "direct") {
        std::cout << tr("Using GitHub Release proxy strategy: ")
                  << effective_options.download_strategy << "\n";
        std::cout << tr("Upstream: ") << source.source_url << "\n";
    } else {
        std::cout << tr("Downloading ") << source.source_url << "\n";
    }
}

} // namespace

void download_app(int argc, char** argv) {
    // download shares source resolution and staging with install, but its side
    // effect boundary is stricter: write one file in the caller's directory and
    // do not chmod, probe, install metadata, wrappers, or desktop entries.
    const InstallOptions options = parse_download_options(argc, argv);
    if (looks_like_local_appimage_target(options.target)) {
        throw std::runtime_error(tr("download does not accept local AppImage paths"));
    }

    const RepoIndexUrlResolveState index_state = inspect_repo_index_url_state(options);
    ResolvedSource source = resolve_install_source(options);
    if (source.source_kind == "local_path") {
        throw std::runtime_error(tr("download does not accept local AppImage paths"));
    }

    const InstallOptions effective_options = apply_network_config_to_options(options, source);
    const fs::path target = fs::current_path() / download_output_name(source);
    if (fs::exists(target)) {
        throw std::runtime_error(tr("download target already exists: ") + target.string());
    }
    // The destination is current_path() / {source.id}.AppImage (package id
    // basename). Refusing collisions preserves download-only behavior without
    // silently replacing user files.

    print_download_progress_banner(effective_options, source);
    stage_resolved_source_with_index_fallback(options, effective_options, index_state, source, target);
    maybe_write_back_index_download_url(options, index_state, source);
    std::cout << tr("Downloaded to: ") << target << "\n";
}

void install_app(int argc, char** argv) {
    // Install lifecycle: parse CLI -> resolve source -> stage current.AppImage
    // -> probe runtime mode -> generate wrapper/desktop/metadata. The resolved
    // source remains the metadata contract for later repair or upgrade.
    const InstallOptions options = parse_install_options(argc, argv);
    const RepoIndexUrlResolveState index_state = inspect_repo_index_url_state(options);
    ResolvedSource source = resolve_install_source(options);
    const InstallOptions effective_options = apply_network_config_to_options(options, source);
    const InstallPaths paths = paths_for(source.id);

    ensure_directory(paths.app_dir);
    ensure_directory(paths.wrapper.parent_path());
    ensure_directory(paths.desktop.parent_path());

    if (source.source_kind == "local_path") {
        std::cout << tr("Installing local AppImage ")
                  << source.source_url << "\n";
    } else {
        print_download_progress_banner(effective_options, source);
    }
    stage_resolved_source_with_index_fallback(options, effective_options, index_state, source, paths.appimage);
    maybe_write_back_index_download_url(options, index_state, source);

    const RepairResult repair = detect_run_mode(paths);
    if (repair.mode == "failed") {
        throw std::runtime_error(tr("failed to find a runnable AppImage mode"));
    }
    write_wrapper(paths, repair.mode);

    write_desktop_entry(paths, source.name);

    write_metadata(paths, source, repair.mode);

    run_process({"update-desktop-database", paths.desktop.parent_path().string()});

    std::cout << tr("Installed ") << source.id << "\n";
    if (!source.github_owner.empty() && !source.github_repo.empty()) {
        std::cout << tr("GitHub release: ")
                  << source.github_owner << "/" << source.github_repo << " " << source.version << "\n";
        std::cout << tr("Asset: ") << source.github_asset << "\n";
    }
    if (source.source_kind == "local_path") {
        std::cout << tr("Source: ") << source.download_url << "\n";
    } else {
        std::cout << tr("Downloaded from: ") << source.download_url << "\n";
    }
    print_mode_line(repair.mode);
    if (repair.fuse_error_detected) {
        print_fuse_fallback_line();
    }
    std::cout << tr("Wrapper: ") << paths.wrapper << "\n";
    std::cout << tr("Desktop entry: ") << paths.desktop << "\n";
}

RepairResult repair_installed_package(const std::string& id) {
    // Repair trusts installed metadata for identity, then re-probes the current
    // AppImage and regenerates only derived launch artifacts.
    const InstallPaths paths = paths_for(id);
    if (!metadata_exists(paths)) {
        throw std::runtime_error(tr("package is not installed: ") + id);
    }
    if (!fs::exists(paths.appimage)) {
        throw std::runtime_error(tr("AppImage file is missing: ") + paths.appimage.string());
    }

    ensure_directory(paths.wrapper.parent_path());
    ensure_directory(paths.desktop.parent_path());

    const fs::path metadata = readable_metadata_path(paths);
    ResolvedSource source;
    source.id = id;
    source.name = metadata_value(metadata, "name").value_or(id);
    source.source_kind = metadata_value(metadata, "source_kind").value_or("url");
    source.version = metadata_value(metadata, "version").value_or("");
    source.source_url = metadata_value(metadata, "source_url").value_or("");
    source.download_url = metadata_value(metadata, "download_url").value_or(source.source_url);
    source.http_etag = metadata_value(metadata, "http_etag").value_or("");
    source.http_last_modified = metadata_value(metadata, "http_last_modified").value_or("");
    source.http_content_length = metadata_value(metadata, "http_content_length").value_or("");
    source.github_owner = metadata_value(metadata, "github_owner").value_or("");
    source.github_repo = metadata_value(metadata, "github_repo").value_or("");
    source.github_asset = metadata_value(metadata, "github_asset").value_or("");
    source.arch = metadata_value(metadata, "arch").value_or(current_arch());

    const RepairResult repair = detect_run_mode(paths);
    if (repair.mode == "failed") {
        if (repair.fuse_error_detected) {
            throw std::runtime_error(
                tr("repair failed after detecting a FUSE-related problem; install a FUSE 2 compatible package or check whether this AppImage supports extraction"));
        }
        throw std::runtime_error(tr("repair failed: no runnable mode found"));
    }

    write_wrapper(paths, repair.mode);
    write_desktop_entry(paths, source.name);
    write_metadata(paths, source, repair.mode);
    run_process({"update-desktop-database", paths.desktop.parent_path().string()});
    return repair;
}

void repair_app(int argc, char** argv) {
    bool yes = false;
    std::string pattern;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--yes" || arg == "-y") {
            yes = true;
        } else if (pattern.empty() && arg.rfind("--", 0) != 0) {
            pattern = arg;
        } else {
            throw std::runtime_error(tr("unknown repair option: ") + arg);
        }
    }
    if (pattern.empty()) {
        throw std::runtime_error(tr("repair requires exactly one package id"));
    }

    const std::vector<std::string> ids = resolve_installed_package_ids(pattern);
    if (ids.size() > 1) {
        const std::string prompt = tr_format(
            "Repair {count} package(s)? [y/N] ",
            {{"{count}", std::to_string(ids.size())}});
        if (!confirm_multi_match(prompt, ids, yes)) {
            std::cout << tr("Repair cancelled\n");
            return;
        }
    }

    for (const std::string& id : ids) {
        const RepairResult repair = repair_installed_package(id);
        std::cout << tr("Repaired ") << id << "\n";
        print_mode_line(repair.mode);
        if (repair.fuse_error_detected) {
            print_fuse_fallback_line();
        }
    }
}

fs::path previous_version_dir(const InstallPaths& paths) {
    return paths.app_dir / "versions" / "previous";
}

void save_previous_version(const InstallPaths& paths) {
    if (!fs::exists(paths.appimage)) {
        throw std::runtime_error(tr("current AppImage is missing: ") + paths.appimage.string());
    }
    const fs::path metadata = readable_metadata_path(paths);
    if (!fs::exists(metadata)) {
        throw std::runtime_error(tr("metadata is missing: ") + paths.metadata.string());
    }

    const fs::path previous = previous_version_dir(paths);
    remove_all_required(previous, tr("saving previous version"));
    ensure_directory(previous);
    copy_file_overwrite(paths.appimage, previous / "current.AppImage");
    copy_file_overwrite(metadata, previous / metadata.filename());
}

void restore_previous_version(const std::string& id) {
    // Rollback restores the previous AppImage and metadata pair first, then
    // reuses repair so wrapper and desktop files match the restored runtime mode.
    const InstallPaths paths = paths_for(id);
    const fs::path previous = previous_version_dir(paths);
    const fs::path previous_appimage = previous / "current.AppImage";
    const fs::path previous_metadata = previous / "metadata.json";
    const fs::path previous_legacy_metadata = previous / "metadata.conf";
    if (!fs::exists(previous_appimage) ||
        (!fs::exists(previous_metadata) && !fs::exists(previous_legacy_metadata))) {
        throw std::runtime_error(tr("no rollback version is available for ") + id);
    }

    copy_file_overwrite(previous_appimage, paths.appimage);
    remove_required(paths.metadata, tr("restoring rollback metadata"));
    remove_required(paths.legacy_metadata, tr("restoring rollback metadata"));
    if (fs::exists(previous_metadata)) {
        copy_file_overwrite(previous_metadata, paths.metadata);
    } else {
        copy_file_overwrite(previous_legacy_metadata, paths.legacy_metadata);
    }
    repair_installed_package(id);
}

void rollback_app(int argc, char** argv) {
    bool yes = false;
    std::string pattern;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--yes" || arg == "-y") {
            yes = true;
        } else if (pattern.empty() && arg.rfind("--", 0) != 0) {
            pattern = arg;
        } else {
            throw std::runtime_error(tr("unknown rollback option: ") + arg);
        }
    }
    if (pattern.empty()) {
        throw std::runtime_error(tr("rollback requires exactly one package id"));
    }

    const std::vector<std::string> ids = resolve_installed_package_ids(pattern);
    if (ids.size() > 1) {
        const std::string prompt = tr_format(
            "Roll back {count} package(s)? [y/N] ",
            {{"{count}", std::to_string(ids.size())}});
        if (!confirm_multi_match(prompt, ids, yes)) {
            std::cout << tr("Rollback cancelled\n");
            return;
        }
    }

    for (const std::string& id : ids) {
        restore_previous_version(id);
        std::cout << tr("Rolled back ") << id << "\n";
    }
}
