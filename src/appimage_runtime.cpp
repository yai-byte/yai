#include "yai.hpp"

// AppImage runtime: wrapper generation, mode probing, and extract fallback.

void write_wrapper(const InstallPaths& paths, const std::string& mode) {
    std::string command;
    if (mode == "extract_and_run") {
        command =
            "export APPIMAGE_EXTRACT_AND_RUN=1\n"
            "exec \"" + shell_escape_double_quoted(paths.appimage.string()) + "\" \"$@\"\n";
    } else if (mode == "extracted") {
        command =
            "exec \"" + shell_escape_double_quoted((paths.extracted_dir / "AppRun").string()) + "\" \"$@\"\n";
    } else {
        command =
            "exec \"" + shell_escape_double_quoted(paths.appimage.string()) + "\" \"$@\"\n";
    }

    const std::string wrapper =
        "#!/usr/bin/env bash\n"
        "set -e\n" +
        command;
    write_executable_file(paths.wrapper, wrapper);
}

void chmod_user_executable(const fs::path& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        throw std::runtime_error(tr("failed to stat ") + path.string() + tr(": ") + std::strerror(errno));
    }
    const mode_t mode = st.st_mode | S_IXUSR;
    if (chmod(path.c_str(), mode) != 0) {
        throw std::runtime_error(tr("failed to chmod ") + path.string() + tr(": ") + std::strerror(errno));
    }
}

RepairResult extract_appimage(const InstallPaths& paths) {
    // Extraction is the last fallback after direct and extract-and-run probes
    // fail. If extraction succeeds, the extracted AppRun becomes the runtime
    // mode that repair and update will record.
    const fs::path tmp_dir = paths.app_dir / "extract-tmp";
    const fs::path squashfs_root = tmp_dir / "squashfs-root";
    const fs::path app_run = paths.extracted_dir / "AppRun";

    remove_all_required(tmp_dir, tr("preparing extract temp directory"));
    ensure_directory(tmp_dir);

    const ProcessResult result = run_process_capture({paths.appimage.string(), "--appimage-extract"}, tmp_dir);
    if (result.exit_code != 0 || !fs::exists(squashfs_root / "AppRun")) {
        const bool tmp_removed = remove_all_best_effort(tmp_dir);
        (void)tmp_removed; // A failed extract has no runtime value; next repair removes it required.
        return RepairResult{"failed", output_has_fuse_error(result.output), result.output};
    }

    remove_all_required(paths.extracted_dir, tr("preparing extracted AppImage directory"));
    std::error_code ec;
    fs::rename(squashfs_root, paths.extracted_dir, ec);
    if (ec) {
        throw std::runtime_error(tr("failed to move extracted AppImage into place: ") + ec.message());
    }
    const bool tmp_removed = remove_all_best_effort(tmp_dir);
    (void)tmp_removed; // The extracted AppRun was already moved into place.
    chmod_user_executable(app_run);
    return RepairResult{"extracted", output_has_fuse_error(result.output), "extracted AppRun"};
}

RepairResult detect_run_mode(const InstallPaths& paths) {
    chmod_user_executable(paths.appimage);

    // Runtime mode detection is a non-interactive install/repair heuristic. It only
    // decides which wrapper yai can generate safely; it is not a general AppImage
    // health check and does not prove the GUI app will be usable after launch.
    std::cerr << tr("yai: probing direct AppImage mode\n");
    const ProcessResult direct = run_process_capture({paths.appimage.string(), "--appimage-version"});
    const ProcessResult direct_launch = run_process_capture_timeout({paths.appimage.string()}, 1500);
    const bool direct_fuse =
        output_has_fuse_error(direct.output) || output_has_fuse_error(direct_launch.output);
    if ((direct.exit_code == 0 || direct_launch.exit_code == 0 || direct_launch.timed_out) && !direct_fuse) {
        const bool removed = remove_all_best_effort(paths.extracted_dir);
        (void)removed; // Direct mode does not need stale extracted files.
        std::cerr << tr("yai: selected direct AppImage mode\n");
        return RepairResult{"direct", output_has_fuse_error(direct.output), direct.output};
    }
    if (direct_fuse) {
        std::cerr << tr("yai: direct mode reported a FUSE problem; trying extract-and-run mode\n");
    } else {
        std::cerr << tr("yai: direct mode did not pass runtime probe; trying extract-and-run mode\n");
    }

    std::cerr << tr("yai: probing extract-and-run AppImage mode\n");
    const ProcessResult extract_and_run = run_process_capture(
        {paths.appimage.string(), "--appimage-version"},
        std::nullopt,
        {{"APPIMAGE_EXTRACT_AND_RUN", "1"}});
    const ProcessResult extract_and_run_launch = run_process_capture_timeout(
        {paths.appimage.string()},
        1500,
        std::nullopt,
        {{"APPIMAGE_EXTRACT_AND_RUN", "1"}});
    const bool extract_and_run_fuse =
        output_has_fuse_error(extract_and_run.output) || output_has_fuse_error(extract_and_run_launch.output);
    if ((extract_and_run.exit_code == 0 ||
         extract_and_run_launch.exit_code == 0 ||
         extract_and_run_launch.timed_out) &&
        !extract_and_run_fuse) {
        const bool removed = remove_all_best_effort(paths.extracted_dir);
        (void)removed; // extract-and-run uses the AppImage file, not an extracted AppRun.
        std::cerr << tr("yai: selected extract-and-run AppImage mode\n");
        return RepairResult{
            "extract_and_run",
            direct_fuse || extract_and_run_fuse,
            extract_and_run.output};
    }
    std::cerr << tr("yai: extract-and-run mode did not pass runtime probe; extracting AppImage\n");

    RepairResult extracted = extract_appimage(paths);
    if (extracted.mode == "extracted") {
        const ProcessResult extracted_launch = run_process_capture_timeout({(paths.extracted_dir / "AppRun").string()}, 1500);
        if (output_has_fuse_error(extracted_launch.output)) {
            extracted.fuse_error_detected = true;
            return RepairResult{"failed", true, extracted_launch.output};
        }
        std::cerr << tr("yai: selected extracted AppRun mode\n");
        extracted.fuse_error_detected =
            extracted.fuse_error_detected ||
            direct_fuse ||
            extract_and_run_fuse;
        return extracted;
    }

    extracted.fuse_error_detected =
        extracted.fuse_error_detected ||
        direct_fuse ||
        extract_and_run_fuse;
    return extracted;
}
