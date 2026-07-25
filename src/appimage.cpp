#include "yai.hpp"

// AppImage-specific lifecycle helpers live here: runtime mode probing,
// extraction fallback, wrapper generation, desktop entry rewriting, and
// metadata emission. This module decides how an installed AppImage should be
// launched, but not which upstream source should be fetched.

std::optional<std::string> metadata_value(const fs::path& file, const std::string& key) {
    if (file.extension() == ".json") {
        if (!fs::exists(file)) {
            return std::nullopt;
        }
        return json_find_string(read_text_file(file), key);
    }

    std::ifstream in(file);
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
    }
    return std::nullopt;
}

fs::path readable_metadata_path(const InstallPaths& paths) {
    if (fs::exists(paths.metadata)) {
        return paths.metadata;
    }
    if (fs::exists(paths.legacy_metadata)) {
        return paths.legacy_metadata;
    }
    return paths.metadata;
}

bool metadata_exists(const InstallPaths& paths) {
    return fs::exists(paths.metadata) || fs::exists(paths.legacy_metadata);
}

std::string sha256_file(const fs::path& path) {
    const ProcessResult result = run_process_capture({"sha256sum", path.string()});
    if (result.exit_code != 0) {
        return "";
    }

    std::istringstream in(result.output);
    std::string digest;
    in >> digest;
    if (digest.size() != 64) {
        return "";
    }
    for (char ch : digest) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return "";
        }
    }
    return to_lower(digest);
}

bool has_image_extension(const fs::path& path) {
    const std::string ext = to_lower(path.extension().string());
    return ext == ".png" || ext == ".svg" || ext == ".xpm";
}

int path_depth(const fs::path& path) {
    return static_cast<int>(std::distance(path.begin(), path.end()));
}

std::optional<fs::path> find_upstream_desktop_file(const fs::path& root) {
    std::vector<fs::path> candidates;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(
             root,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file(ec) && to_lower(it->path().extension().string()) == ".desktop") {
            candidates.push_back(it->path());
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    std::sort(candidates.begin(), candidates.end(), [](const fs::path& left, const fs::path& right) {
        const int left_depth = path_depth(left);
        const int right_depth = path_depth(right);
        if (left_depth != right_depth) {
            return left_depth < right_depth;
        }
        return left.string() < right.string();
    });
    return candidates.front();
}

fs::path desktop_temp_dir(const InstallPaths& paths) {
    return paths.app_dir / "desktop-extract-tmp";
}

std::optional<DesktopSource> desktop_source_from_appimage(const InstallPaths& paths) {
    if (fs::exists(paths.extracted_dir / "AppRun")) {
        return DesktopSource{paths.extracted_dir, false};
    }

    const fs::path tmp_dir = desktop_temp_dir(paths);
    const fs::path squashfs_root = tmp_dir / "squashfs-root";
    remove_all_required(tmp_dir, tr("preparing desktop extract temp directory"));
    ensure_directory(tmp_dir);

    const ProcessResult result = run_process_capture({paths.appimage.string(), "--appimage-extract"}, tmp_dir);
    if (result.exit_code != 0 || !fs::exists(squashfs_root)) {
        const bool removed = remove_all_best_effort(tmp_dir);
        (void)removed; // Failed desktop extraction should not block default desktop generation.
        return std::nullopt;
    }
    return DesktopSource{squashfs_root, true};
}

void cleanup_desktop_source(const InstallPaths& paths, const DesktopSource& source) {
    if (!source.cleanup) {
        return;
    }
    const bool removed = remove_all_best_effort(desktop_temp_dir(paths));
    (void)removed; // Desktop extraction is disposable after wrapper/desktop rendering.
}

std::optional<fs::path> find_icon_by_name(const fs::path& root, const std::string& icon_name) {
    if (icon_name.empty()) {
        return std::nullopt;
    }

    const fs::path icon_path(icon_name);
    const std::string wanted_file = to_lower(icon_path.filename().string());
    const std::string wanted_stem = to_lower(icon_path.stem().string());

    std::error_code ec;
    for (fs::recursive_directory_iterator it(
             root,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec) || !has_image_extension(it->path())) {
            continue;
        }
        const std::string file = to_lower(it->path().filename().string());
        const std::string stem = to_lower(it->path().stem().string());
        if (file == wanted_file || stem == wanted_stem) {
            return it->path();
        }
    }
    return std::nullopt;
}

std::optional<fs::path> find_first_icon(const fs::path& root) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(
             root,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file(ec) && has_image_extension(it->path())) {
            return it->path();
        }
    }
    return std::nullopt;
}

std::optional<fs::path> find_relative_icon_path(const fs::path& root, const std::string& icon_name) {
    const fs::path icon_path(icon_name);
    if (icon_name.empty() || !icon_path.is_relative() || icon_name.find('/') == std::string::npos) {
        return std::nullopt;
    }

    const fs::path relative_icon = root / icon_path;
    if (fs::is_regular_file(relative_icon)) {
        return relative_icon;
    }
    return std::nullopt;
}

std::optional<fs::path> find_desktop_icon_source(const fs::path& root, const std::string& icon_name) {
    const fs::path dir_icon = root / ".DirIcon";
    if (fs::is_regular_file(dir_icon)) {
        return dir_icon;
    }

    std::optional<fs::path> source = find_relative_icon_path(root, icon_name);
    if (source.has_value()) {
        return source;
    }
    source = find_icon_by_name(root, icon_name);
    if (source.has_value()) {
        return source;
    }
    return find_first_icon(root);
}

std::optional<fs::path> install_desktop_icon_file(const InstallPaths& paths, const fs::path& source) {
    std::string ext = source.extension().string();
    if (source.filename() == ".DirIcon") {
        ext.clear();
    }
    const fs::path installed = paths.app_dir / ("desktop-icon" + ext);
    copy_file_overwrite(source, installed);
    return installed;
}

std::optional<fs::path> install_desktop_icon(
    const InstallPaths& paths,
    const fs::path& root,
    const std::string& icon_name) {
    const std::optional<fs::path> source = find_desktop_icon_source(root, icon_name);
    if (!source.has_value()) {
        return std::nullopt;
    }
    return install_desktop_icon_file(paths, *source);
}

std::string desktop_key_base(const std::string& key) {
    const std::size_t locale_start = key.find('[');
    return locale_start == std::string::npos ? key : key.substr(0, locale_start);
}

bool is_safe_upstream_desktop_key(const std::string& key) {
    const std::string base = desktop_key_base(key);
    if (key.rfind("X-", 0) == 0) {
        return true;
    }
    return base == "Version" ||
           base == "Name" ||
           base == "GenericName" ||
           base == "NoDisplay" ||
           base == "Comment" ||
           base == "Icon" ||
           base == "Hidden" ||
           base == "OnlyShowIn" ||
           base == "NotShowIn" ||
           base == "Terminal" ||
           base == "MimeType" ||
           base == "Categories" ||
           base == "StartupNotify" ||
           base == "StartupWMClass" ||
           base == "Keywords" ||
           base == "PrefersNonDefaultGPU" ||
           base == "SingleMainWindow";
}

std::optional<std::string> read_desktop_icon_name(const fs::path& desktop_file) {
    std::ifstream in(desktop_file);
    std::string line;
    bool in_entry = false;
    while (std::getline(in, line)) {
        const std::string stripped = trim(line);
        if (stripped == "[Desktop Entry]") {
            in_entry = true;
            continue;
        }
        if (in_entry && !stripped.empty() && stripped.front() == '[') {
            break;
        }
        if (!in_entry) {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        if (desktop_key_base(line.substr(0, equals)) == "Icon") {
            return line.substr(equals + 1);
        }
    }
    return std::nullopt;
}

struct DesktopKeyValue {
    std::string key;
    std::string value;
    std::string base;
};

struct DesktopEntryWriteState {
    bool type = false;
    bool name = false;
    bool exec = false;
    bool icon = false;
    bool terminal = false;
    bool categories = false;
};

void remember_desktop_key(const DesktopKeyValue& entry, DesktopEntryWriteState& state);

std::optional<DesktopKeyValue> desktop_line_key_value(const std::string& line) {
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
        return std::nullopt;
    }

    const std::string key = line.substr(0, equals);
    return DesktopKeyValue{key, line.substr(equals + 1), desktop_key_base(key)};
}

bool should_skip_upstream_desktop_key(const std::string& base) {
    return base == "TryExec" ||
           base == "Path" ||
           base == "Actions" ||
           base == "DBusActivatable";
}

bool render_forced_desktop_key(
    std::ostringstream& out,
    const InstallPaths& paths,
    const std::optional<fs::path>& installed_icon,
    const DesktopKeyValue& entry,
    DesktopEntryWriteState& state) {
    // yai always owns the launch entry: Type must stay Application, Exec must
    // point at the wrapper, and Icon is rewritten when yai installs a better
    // local asset. Other safe keys may still be inherited from upstream.
    if (entry.base == "Type") {
        if (!state.type) {
            out << "Type=Application\n";
            state.type = true;
        }
        return true;
    }
    if (entry.base == "Exec") {
        if (!state.exec) {
            out << "Exec=" << desktop_escape(paths.wrapper.string()) << " %U\n";
            state.exec = true;
        }
        return true;
    }
    if (entry.base == "Icon" && installed_icon.has_value()) {
        state.icon = true;
        out << entry.key << "=" << desktop_escape(installed_icon->string()) << "\n";
        return true;
    }
    return false;
}

void render_allowed_upstream_desktop_key(
    std::ostringstream& out,
    const DesktopKeyValue& entry,
    DesktopEntryWriteState& state) {
    // Safe upstream keys preserve the app's identity and categorization, but
    // only after yai strips launch-affecting fields such as TryExec, Path, and
    // DBusActivatable.
    if (should_skip_upstream_desktop_key(entry.base) ||
        !is_safe_upstream_desktop_key(entry.key)) {
        return;
    }
    remember_desktop_key(entry, state);
    out << entry.key << "=" << entry.value << "\n";
}

void render_desktop_entry_key(
    std::ostringstream& out,
    const InstallPaths& paths,
    const std::optional<fs::path>& installed_icon,
    const DesktopKeyValue& entry,
    DesktopEntryWriteState& state) {
    if (render_forced_desktop_key(out, paths, installed_icon, entry, state)) {
        return;
    }
    render_allowed_upstream_desktop_key(out, entry, state);
}

void remember_desktop_key(const DesktopKeyValue& entry, DesktopEntryWriteState& state) {
    if (entry.key == "Name") {
        state.name = true;
    } else if (entry.base == "Icon") {
        state.icon = true;
    } else if (entry.base == "Terminal") {
        state.terminal = true;
    } else if (entry.base == "Categories") {
        state.categories = true;
    }
}

enum class DesktopLineReadAction {
    Skip,
    Use,
    Stop,
};

DesktopLineReadAction desktop_line_read_action(const std::string& line, bool& in_entry) {
    const std::string stripped = trim(line);
    if (stripped.empty() || stripped.front() == '#') {
        return DesktopLineReadAction::Skip;
    }
    if (stripped == "[Desktop Entry]") {
        in_entry = true;
        return DesktopLineReadAction::Skip;
    }
    if (in_entry && stripped.front() == '[') {
        return DesktopLineReadAction::Stop;
    }
    return in_entry ? DesktopLineReadAction::Use : DesktopLineReadAction::Skip;
}

void append_missing_desktop_defaults(
    std::ostringstream& out,
    const InstallPaths& paths,
    const std::string& name,
    const std::optional<fs::path>& installed_icon,
    const DesktopEntryWriteState& state) {
    if (!state.type) {
        out << "Type=Application\n";
    }
    if (!state.name) {
        out << "Name=" << desktop_escape(name) << "\n";
    }
    if (!state.exec) {
        out << "Exec=" << desktop_escape(paths.wrapper.string()) << " %U\n";
    }
    if (!state.icon && installed_icon.has_value()) {
        out << "Icon=" << desktop_escape(installed_icon->string()) << "\n";
    }
    if (!state.terminal) {
        out << "Terminal=false\n";
    }
    if (!state.categories) {
        out << "Categories=Utility;\n";
    }
}

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

std::optional<std::string> render_upstream_desktop_entry(
    const InstallPaths& paths,
    const std::string& name,
    const fs::path& root,
    const fs::path& desktop_file) {
    // Upstream desktop files are treated as hints. yai keeps safe keys that
    // help the entry look native, but forces its own Exec path and default
    // application metadata so uninstall and repair stay under yai control.
    const std::string icon_name = read_desktop_icon_name(desktop_file).value_or("");
    const std::optional<fs::path> installed_icon = install_desktop_icon(paths, root, icon_name);

    std::ifstream in(desktop_file);
    if (!in) {
        return std::nullopt;
    }

    std::ostringstream out;
    out << "[Desktop Entry]\n";
    std::string line;
    bool in_entry = false;
    DesktopEntryWriteState state;
    while (std::getline(in, line)) {
        const DesktopLineReadAction action = desktop_line_read_action(line, in_entry);
        if (action == DesktopLineReadAction::Stop) {
            break;
        }
        if (action == DesktopLineReadAction::Skip) {
            continue;
        }

        const std::optional<DesktopKeyValue> entry = desktop_line_key_value(line);
        if (!entry.has_value()) {
            continue;
        }
        render_desktop_entry_key(out, paths, installed_icon, *entry, state);
    }

    append_missing_desktop_defaults(out, paths, name, installed_icon, state);
    return out.str();
}

std::optional<std::string> upstream_desktop_entry_content(
    const InstallPaths& paths,
    const std::string& name) {
    const std::optional<DesktopSource> source = desktop_source_from_appimage(paths);
    if (!source.has_value()) {
        return std::nullopt;
    }

    const std::optional<fs::path> upstream_desktop = find_upstream_desktop_file(source->root);
    const std::optional<std::string> desktop = upstream_desktop.has_value()
        ? render_upstream_desktop_entry(paths, name, source->root, *upstream_desktop)
        : std::nullopt;
    cleanup_desktop_source(paths, *source);
    return desktop;
}

std::string default_desktop_entry(const InstallPaths& paths, const std::string& name) {
    const std::string desktop =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=" + desktop_escape(name) + "\n"
        "Comment=Installed by yai\n"
        "Exec=" + desktop_escape(paths.wrapper.string()) + " %U\n"
        "Icon=application-x-executable\n"
        "Terminal=false\n"
        "Categories=Utility;\n"
        "StartupNotify=true\n";
    return desktop;
}

void write_desktop_entry(const InstallPaths& paths, const std::string& name) {
    const std::optional<std::string> upstream_desktop = upstream_desktop_entry_content(paths, name);
    if (upstream_desktop.has_value()) {
        write_text_file(paths.desktop, *upstream_desktop);
        return;
    }

    const std::string desktop = default_desktop_entry(paths, name);
    write_text_file(paths.desktop, desktop);
}

void write_metadata(
    const InstallPaths& paths,
    const ResolvedSource& source,
    const std::string& mode) {
    // Metadata is the contract between install, repair, upgrade, and rollback.
    // Keep upstream identity, selected arch, and install_mode together so later
    // flows can reconstruct the same launch behavior.
    const std::string sha256 = sha256_file(paths.appimage);
    const std::string metadata_arch = source.arch.empty() ? current_arch() : normalize_arch(source.arch);
    const std::string metadata =
        "{\n"
        "  \"id\": \"" + json_escape_string(source.id) + "\",\n"
        "  \"name\": \"" + json_escape_string(source.name) + "\",\n"
        "  \"version\": \"" + json_escape_string(source.version) + "\",\n"
        "  \"arch\": \"" + json_escape_string(metadata_arch) + "\",\n"
        "  \"install_mode\": \"" + json_escape_string(mode) + "\",\n"
        "  \"installed_at\": \"" + json_escape_string(current_utc_timestamp()) + "\",\n"
        "  \"source_kind\": \"" + json_escape_string(source.source_kind) + "\",\n"
        "  \"source_url\": \"" + json_escape_string(source.source_url) + "\",\n"
        "  \"download_url\": \"" + json_escape_string(source.download_url) + "\",\n"
        "  \"github_owner\": \"" + json_escape_string(source.github_owner) + "\",\n"
        "  \"github_repo\": \"" + json_escape_string(source.github_repo) + "\",\n"
        "  \"github_asset\": \"" + json_escape_string(source.github_asset) + "\",\n"
        "  \"sha256\": \"" + json_escape_string(sha256) + "\",\n"
        "  \"checksum_status\": \"unknown\",\n"
        "  \"files\": {\n"
        "    \"appimage\": \"" + json_escape_string(paths.appimage.string()) + "\",\n"
        "    \"wrapper\": \"" + json_escape_string(paths.wrapper.string()) + "\",\n"
        "    \"desktop\": \"" + json_escape_string(paths.desktop.string()) + "\",\n"
        "    \"extracted_dir\": \"" + json_escape_string(paths.extracted_dir.string()) + "\"\n"
        "  }\n"
        "}\n";
    write_text_file(paths.metadata, metadata);

    const bool legacy_removed = remove_best_effort(paths.legacy_metadata);
    (void)legacy_removed; // Legacy metadata is a compatibility cleanup only.
}
