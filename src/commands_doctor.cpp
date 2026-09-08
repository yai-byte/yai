#include "yai.hpp"

int check_installed_app_files(const std::string& id, const InstallPaths& paths, const std::string& mode) {
    int warnings = 0;
    if (!fs::exists(paths.appimage)) {
        std::cout << tr_format(
            "WARN {id}: missing AppImage {path}\n",
            {{"{id}", id}, {"{path}", paths.appimage.string()}});
        ++warnings;
    }
    if (!fs::exists(paths.wrapper)) {
        std::cout << tr_format(
            "WARN {id}: missing wrapper {path}\n",
            {{"{id}", id}, {"{path}", paths.wrapper.string()}});
        ++warnings;
    }
    if (!fs::exists(paths.desktop)) {
        std::cout << tr_format(
            "WARN {id}: missing desktop entry {path}\n",
            {{"{id}", id}, {"{path}", paths.desktop.string()}});
        ++warnings;
    }
    if (mode == "extracted" && !fs::exists(paths.extracted_dir / "AppRun")) {
        std::cout << tr_format(
            "WARN {id}: missing extracted AppRun {path}\n",
            {{"{id}", id}, {"{path}", (paths.extracted_dir / "AppRun").string()}});
        ++warnings;
    }

    if (warnings == 0) {
        std::cout << tr_format(
            "OK   {id}: files present, mode={mode}\n",
            {{"{id}", id}, {"{mode}", mode}});
    }
    return warnings;
}

int check_installed_apps() {
    const std::pair<fs::path, InstallScope> roots[] = {
        {expand_home_path(".local/share/yai/apps"), InstallScope::User},
        {fs::path("/usr/local/share/yai/apps"), InstallScope::System},
    };

    int warnings = 0;
    for (const auto& [root, scope] : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            const fs::path app_dir = entry.path();
            const fs::path metadata = app_dir / "metadata.json";
            if (!fs::exists(metadata)) {
                // A directory without metadata.json is a leftover, not a broken
                // install. Surface it so the user can reclaim the disk space.
                const std::string leftover_id = app_dir.filename().string();
                std::cout << tr_format(
                    "WARN {id}: leftover directory without metadata.json; reclaim the space with: yai remove {id}\n",
                    {{"{id}", leftover_id}});
                ++warnings;
                continue;
            }

            const std::string id = metadata_json_value(metadata, "id").value_or(app_dir.filename().string());
            const fs::path wrapper =
                (scope == InstallScope::System ? fs::path("/usr/local/bin") : bin_dir()) / id;
            const fs::path desktop =
                (scope == InstallScope::System ? fs::path("/usr/local/share/applications") : applications_dir()) /
                ("yai-" + id + ".desktop");
            const InstallPaths paths{
                app_dir,
                app_dir / "current.AppImage",
                app_dir / "extracted",
                wrapper,
                desktop,
                metadata,
            };
            const std::string mode = metadata_json_value(metadata, "install_mode").value_or("unknown");
            warnings += check_installed_app_files(id, paths, mode) > 0 ? 1 : 0;
        }
    }
    return warnings;
}

int check_path_setup() {
    const fs::path local_bin = bin_dir();
    const char* path_env = std::getenv("PATH");
    const std::string path = path_env == nullptr ? "" : path_env;
    if (path.find(local_bin.string()) != std::string::npos) {
        std::cout << tr("OK   PATH contains ") << local_bin << "\n";
        return 0;
    }

    std::cout << tr("WARN PATH does not contain ") << local_bin << "\n";
    return 1;
}

int check_tool_available(
    const std::vector<std::string>& args,
    const std::string& ok_message,
    const std::string& warn_message,
    const std::vector<int>& accepted_exit_codes = {0}) {
    const ProcessResult result = run_process_capture(args);
    if (std::find(accepted_exit_codes.begin(), accepted_exit_codes.end(), result.exit_code) !=
        accepted_exit_codes.end()) {
        std::cout << tr(ok_message);
        return 0;
    }

    std::cout << tr(warn_message);
    return 1;
}

int check_fuse_access() {
    const fs::path fuse_dev = "/dev/fuse";
    if (!fs::exists(fuse_dev)) {
        std::cout << tr("WARN /dev/fuse is missing; some AppImages may require extracted mode\n");
        return 1;
    }
    if (access(fuse_dev.c_str(), R_OK | W_OK) == 0) {
        std::cout << tr("OK   /dev/fuse is accessible\n");
        return 0;
    }

    std::cout << tr("WARN /dev/fuse exists but is not accessible by this user\n");
    return 1;
}

void print_doctor_summary(int warnings) {
    if (warnings == 0) {
        std::cout << tr("Doctor finished with no warnings\n");
    } else {
        std::cout << tr_format(
            "Doctor finished with {count} warning(s)\n",
            {{"{count}", std::to_string(warnings)}});
    }
}

int check_idle_inhibitor() {
    if (executable_available("systemd-inhibit")) {
        std::cout << tr("OK   idle inhibitor (systemd-inhibit) is available; downloads will not be suspended\n");
        return 0;
    }
    std::cout << tr("WARN idle inhibitor (systemd-inhibit) is not available; long downloads may be suspended by power management\n");
    return 1;
}

void doctor_app(int argc) {
    if (argc != 2) {
        throw std::runtime_error(tr("doctor does not accept arguments"));
    }

    int warnings = 0;
    warnings += check_path_setup();
    warnings += check_tool_available(
        {"curl", "--version"},
        "OK   curl is available\n",
        "WARN curl is not available; URL installs will fail\n");
    warnings += check_tool_available(
        {"update-desktop-database", "--help"},
        "OK   update-desktop-database is available\n",
        "WARN update-desktop-database is not available; desktop cache refresh will be skipped\n",
        {0, 1});
    warnings += check_fuse_access();
    warnings += check_idle_inhibitor();
    warnings += check_installed_apps();
    print_doctor_summary(warnings);
}
