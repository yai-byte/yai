# Task 1 review — commands_lifecycle
  967 src/commands.cpp
  220 src/commands_lifecycle.cpp
 1187 总计
## Makefile diff
--- .superpowers/sdd/Makefile.pre-cmd-task1	2026-07-25 08:12:41.119791810 +0800
+++ Makefile	2026-07-25 08:13:56.513411688 +0800
@@ -9,6 +9,7 @@
 	src/cli_download.cpp \
 	src/commands.cpp \
 	src/commands_doctor.cpp \
+	src/commands_lifecycle.cpp \
 	src/core.cpp \
 	src/download_progress.cpp \
 	src/i18n.cpp \
## Symbol checks
src/commands.cpp:24:std::string resolve_installed_package_id(const std::string& pattern) {
src/commands.cpp:75:struct UpgradeCommandOptions {
src/commands_lifecycle.cpp:5:namespace {
src/commands_lifecycle.cpp:7:std::string download_output_name(const ResolvedSource& source) {
src/commands_lifecycle.cpp:18:} // namespace
src/commands_lifecycle.cpp:20:void download_app(int argc, char** argv) {
src/commands_lifecycle.cpp:212:void rollback_app(int argc, char** argv) {
## Must not be in commands.cpp
(none — good)
## NEW lifecycle file
#include "yai.hpp"

// Download, install, repair, and rollback command workflows.

namespace {

std::string download_output_name(const ResolvedSource& source) {
    std::string name = source.github_asset.empty()
        ? basename_from_url(source.source_url)
        : source.github_asset;
    name = fs::path(name).filename().string();
    if (name.empty() || name == "." || name == "..") {
        name = source.id.empty() ? "download.AppImage" : source.id + ".AppImage";
    }
    return name;
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

    ResolvedSource source = resolve_install_source(options);
    if (source.source_kind == "local_path") {
        throw std::runtime_error(tr("download does not accept local AppImage paths"));
    }

    const InstallOptions effective_options = apply_network_config_to_options(options, source);
    const fs::path target = fs::current_path() / download_output_name(source);
    if (fs::exists(target)) {
        throw std::runtime_error(tr("download target already exists: ") + target.string());
    }
    // The destination is always the caller's current directory plus the upstream
    // asset name. Refusing collisions preserves download-only behavior without
    // silently replacing user files.

    if (effective_options.download_strategy != "direct") {
        std::cout << tr("Using GitHub Release proxy strategy: ")
                  << effective_options.download_strategy << "\n";
        std::cout << tr("Upstream: ") << source.source_url << "\n";
    } else {
        std::cout << tr("Downloading ") << source.source_url << "\n";
    }
    source.download_url = stage_appimage_source(source, effective_options, target);
    std::cout << tr("Downloaded to: ") << target << "\n";
}

void install_app(int argc, char** argv) {
    // Install lifecycle: parse CLI -> resolve source -> stage current.AppImage
    // -> probe runtime mode -> generate wrapper/desktop/metadata. The resolved
    // source remains the metadata contract for later repair or upgrade.
    const InstallOptions options = parse_install_options(argc, argv);
    ResolvedSource source = resolve_install_source(options);
    const InstallOptions effective_options = apply_network_config_to_options(options, source);
    const InstallPaths paths = paths_for(source.id);

    ensure_directory(paths.app_dir);
    ensure_directory(paths.wrapper.parent_path());
    ensure_directory(paths.desktop.parent_path());

    if (source.source_kind == "local_path") {
        std::cout << tr("Installing local AppImage ")
                  << source.source_url << "\n";
    } else if (effective_options.download_strategy != "direct") {
        std::cout << tr("Using GitHub Release proxy strategy: ")
                  << effective_options.download_strategy << "\n";
        std::cout << tr("Upstream: ") << source.source_url << "\n";
    } else {
        std::cout << tr("Downloading ") << source.source_url << "\n";
    }
    source.download_url = stage_appimage_source(source, effective_options, paths.appimage);

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
    if (argc != 3) {
        throw std::runtime_error(tr("repair requires exactly one package id"));
    }

    const std::string id = resolve_installed_package_id(argv[2]);
    const RepairResult repair = repair_installed_package(id);

    std::cout << tr("Repaired ") << id << "\n";
    print_mode_line(repair.mode);
    if (repair.fuse_error_detected) {
        print_fuse_fallback_line();
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
    if (argc != 3) {
        throw std::runtime_error(tr("rollback requires exactly one package id"));
    }

    const std::string id = resolve_installed_package_id(argv[2]);
    restore_previous_version(id);
    std::cout << tr("Rolled back ") << id << "\n";
}
