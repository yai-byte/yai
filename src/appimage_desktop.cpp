#include "yai.hpp"

// AppImage desktop entry and icon helpers: upstream .desktop rewriting and icon install.

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

namespace {

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
} // namespace


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

namespace {

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
} // namespace


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

namespace {

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
} // namespace


void write_desktop_entry(const InstallPaths& paths, const std::string& name) {
    const std::optional<std::string> upstream_desktop = upstream_desktop_entry_content(paths, name);
    if (upstream_desktop.has_value()) {
        write_text_file(paths.desktop, *upstream_desktop);
        return;
    }

    const std::string desktop = default_desktop_entry(paths, name);
    write_text_file(paths.desktop, desktop);
}

