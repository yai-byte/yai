#include "yai.hpp"

// Shared path/string helpers, filesystem utilities, mirror/network config, and
// install path derivation. Process execution, download progress UI, and i18n
// live in process.cpp, download_progress.cpp, and i18n.cpp.

const char* APPIMAGE_FEED_URL = "https://appimage.github.io/feed.json";

namespace {

bool token_looks_like_version(const std::string& token) {
    if (token.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (token[0] == 'v' || token[0] == 'V') {
        if (token.size() == 1) {
            return false;
        }
        i = 1;
    }
    bool saw_digit = false;
    for (; i < token.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(token[i]);
        if (ch >= '0' && ch <= '9') {
            saw_digit = true;
            continue;
        }
        if (ch == '.') {
            continue;
        }
        return false;
    }
    return saw_digit;
}

}  // namespace

std::optional<std::string> env_string(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return std::nullopt;
    }
    return std::string(value);
}

std::string ascii_lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}


std::string home_dir() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || std::string(home).empty()) {
        throw std::runtime_error(tr("HOME is not set"));
    }
    return home;
}

fs::path expand_home_path(const std::string& path) {
    return fs::path(home_dir()) / path;
}

fs::path config_dir_path() {
    return expand_home_path(".config/yai");
}

fs::path network_config_path() {
    return config_dir_path() / "network.conf";
}

fs::path github_blocklist_path() {
    return config_dir_path() / "github_blocklist.conf";
}

bool contains_line_break(const std::string& value) {
    return value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string basename_from_url(const std::string& url) {
    std::string clean = url;
    const std::size_t fragment = clean.find('#');
    if (fragment != std::string::npos) {
        clean.erase(fragment);
    }
    const std::size_t query = clean.find('?');
    if (query != std::string::npos) {
        clean.erase(query);
    }
    while (!clean.empty() && clean.back() == '/') {
        clean.pop_back();
    }
    const std::size_t slash = clean.find_last_of('/');
    if (slash == std::string::npos) {
        return clean.empty() ? "app.AppImage" : clean;
    }
    std::string name = clean.substr(slash + 1);
    return name.empty() ? "app.AppImage" : name;
}

std::string strip_appimage_suffix(std::string value) {
    const std::string suffix = ".AppImage";
    if (value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
        value.resize(value.size() - suffix.size());
    }
    return value;
}

std::string base_name_from_appimage_filename(const std::string& filename) {
    const std::string stem = strip_appimage_suffix(fs::path(filename).filename().string());
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : stem) {
        if (ch == '-' || ch == '_') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }

    while (!tokens.empty() &&
           (token_looks_like_arch(tokens.back()) || token_looks_like_version(tokens.back()))) {
        tokens.pop_back();
    }

    if (tokens.empty()) {
        return stem;
    }

    std::string out = tokens.front();
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        out.push_back('-');
        out += tokens[i];
    }
    return out;
}

std::string sanitize_id(const std::string& value) {
    std::string out;
    bool previous_dash = false;
    for (unsigned char ch : value) {
        char lower = static_cast<char>(std::tolower(ch));
        const bool allowed = (lower >= 'a' && lower <= 'z') ||
                             (lower >= '0' && lower <= '9') ||
                             lower == '_' || lower == '-' || lower == '.';
        if (allowed) {
            out.push_back(lower);
            previous_dash = false;
        } else if (!previous_dash) {
            out.push_back('-');
            previous_dash = true;
        }
    }
    while (!out.empty() && (out.front() == '-' || out.front() == '.')) {
        out.erase(out.begin());
    }
    while (!out.empty() && (out.back() == '-' || out.back() == '.')) {
        out.pop_back();
    }
    return out.empty() ? "appimage-app" : out;
}

std::string shell_escape_double_quoted(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

std::string desktop_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == ';') {
            out.push_back('\\');
        }
        if (ch != '\n' && ch != '\r') {
            out.push_back(ch);
        }
    }
    return out;
}

std::string to_lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool has_url_scheme(const std::string& value) {
    return value.find("://") != std::string::npos;
}

bool looks_like_github_repo(const std::string& value) {
    if (has_url_scheme(value)) {
        return false;
    }
    const std::size_t slash = value.find('/');
    return slash != std::string::npos &&
           slash > 0 &&
           slash + 1 < value.size() &&
           value.find('/', slash + 1) == std::string::npos;
}

bool looks_like_repo_package_target(const std::string& value) {
    if (value.empty() || has_url_scheme(value) || value.find('/') != std::string::npos) {
        return false;
    }
    return to_lower(value).find(".appimage") == std::string::npos;
}

bool looks_like_local_appimage_target(const std::string& value) {
    if (value.empty() || has_url_scheme(value)) {
        return false;
    }
    const std::string lower = to_lower(value);
    return value.front() == '/' ||
           value.rfind("./", 0) == 0 ||
           value.rfind("../", 0) == 0 ||
           lower.find(".appimage") != std::string::npos ||
           fs::exists(value);
}

bool has_glob_wildcards(const std::string& value) {
    return value.find('*') != std::string::npos ||
           value.find('?') != std::string::npos;
}

bool glob_match_case_insensitive(const std::string& pattern, const std::string& value) {
    const std::string pat = to_lower(pattern);
    const std::string text = to_lower(value);
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string::npos;
    std::size_t retry = 0;

    while (t < text.size()) {
        if (p < pat.size() && (pat[p] == '?' || pat[p] == text[t])) {
            ++p;
            ++t;
            continue;
        }
        if (p < pat.size() && pat[p] == '*') {
            star = p++;
            retry = t;
            continue;
        }
        if (star != std::string::npos) {
            p = star + 1;
            t = ++retry;
            continue;
        }
        return false;
    }

    while (p < pat.size() && pat[p] == '*') {
        ++p;
    }
    return p == pat.size();
}

std::string url_encode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : value) {
        const bool safe = (ch >= 'a' && ch <= 'z') ||
                          (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') ||
                          ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 15]);
        }
    }
    return out;
}

std::string replace_all(std::string value, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return value;
    }
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

bool output_has_fuse_error(const std::string& output) {
    const std::string lower = to_lower(output);
    return lower.find("libfuse") != std::string::npos ||
           lower.find("/dev/fuse") != std::string::npos ||
           lower.find("squashfuse") != std::string::npos ||
           lower.find("appimages require fuse") != std::string::npos ||
           lower.find("cannot mount appimage") != std::string::npos ||
           lower.find("fuse setup") != std::string::npos ||
           lower.find("fuse kernel module") != std::string::npos;
}

void ensure_directory(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error(tr("failed to create directory ") + path.string() + tr(": ") + ec.message());
    }
}

void remove_required(const fs::path& path, const std::string& context) {
    std::error_code ec;
    const bool removed = fs::remove(path, ec);
    if (ec) {
        throw std::runtime_error(context + tr(": failed to remove ") + path.string() + tr(": ") + ec.message());
    }
    (void)removed; // Absence is acceptable; filesystem errors are not.
}

void remove_all_required(const fs::path& path, const std::string& context) {
    std::error_code ec;
    const std::uintmax_t removed = fs::remove_all(path, ec);
    if (ec) {
        throw std::runtime_error(context + tr(": failed to remove ") + path.string() + tr(": ") + ec.message());
    }
    (void)removed; // Absence is acceptable; filesystem errors are not.
}

bool remove_best_effort(const fs::path& path) {
    std::error_code ec;
    const bool removed = fs::remove(path, ec);
    (void)removed;
    return !ec;
}

bool remove_all_best_effort(const fs::path& path) {
    std::error_code ec;
    const std::uintmax_t removed = fs::remove_all(path, ec);
    (void)removed;
    return !ec;
}

void write_executable_file(const fs::path& path, const std::string& content) {
    {
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error(tr("failed to write ") + path.string());
        }
        out << content;
    }
    if (chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
        throw std::runtime_error(tr("failed to chmod ") + path.string() + tr(": ") + std::strerror(errno));
    }
}

void write_text_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error(tr("failed to write ") + path.string());
    }
    out << content;
}

void write_text_file_atomic(const fs::path& path, const std::string& content) {
    const fs::path temp_path = path.string() + ".tmp";
    std::ofstream out(temp_path);
    if (!out) {
        throw std::runtime_error(tr("failed to write ") + temp_path.string());
    }
    out << content;
    out.flush();
    std::error_code ec;
    fs::rename(temp_path, path, ec);
    if (ec) {
        // Clean up the temp file on failure
        std::error_code remove_ec;
        fs::remove(temp_path, remove_ec);
        throw std::runtime_error(tr("failed to move file into place: ") + ec.message());
    }
}

std::string read_text_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(tr("failed to read ") + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void copy_file_overwrite(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    ensure_directory(to.parent_path());
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        throw std::runtime_error(tr("failed to copy ") + from.string() + tr(" to ") + to.string() + tr(": ") + ec.message());
    }
}

std::vector<MirrorProvider> built_in_mirror_providers() {
    return {
        MirrorProvider{"ghfast", "https://ghfast.top/{raw_url}", "GHFast prefix proxy"},
        MirrorProvider{"chengc", "https://github.chenc.dev/{raw_url_noscheme}", "ChengC-style GitHub proxy"},
        MirrorProvider{"fastgit", "https://download.fastgit.org/{owner}/{repo}/releases/download/{tag}/{asset}", "FastGit release download mirror"},
        MirrorProvider{"yylx", "https://git.yylx.win/{raw_url}", "YYLX prefix proxy"},
        MirrorProvider{"llkk", "https://gh.llkk.cc/{raw_url}", "LLKK prefix proxy"},
    };
}

std::optional<MirrorProvider> mirror_provider_by_name(const std::string& name) {
    for (const MirrorProvider& provider : built_in_mirror_providers()) {
        if (provider.name == name) {
            return provider;
        }
    }
    return std::nullopt;
}

bool template_has_placeholder(const std::string& value) {
    return value.find("{raw_url}") != std::string::npos ||
           value.find("{raw_url_noscheme}") != std::string::npos ||
           value.find("{url}") != std::string::npos ||
           value.find("{owner}") != std::string::npos ||
           value.find("{repo}") != std::string::npos ||
           value.find("{tag}") != std::string::npos ||
           value.find("{asset}") != std::string::npos;
}

std::string normalize_custom_mirror_template(std::string value) {
    value = trim(value);
    if (value.empty() || contains_line_break(value)) {
        throw std::runtime_error(tr("custom mirror template must not be empty or contain line breaks"));
    }
    if (template_has_placeholder(value)) {
        return value;
    }
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    if (!has_url_scheme(value)) {
        value = "https://" + value;
    }
    return value + "/{raw_url}";
}

std::optional<std::string> key_value_file_value(const fs::path& file, const std::string& key) {
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

NetworkConfig load_network_config() {
    NetworkConfig config;
    const fs::path path = network_config_path();
    config.exists = fs::exists(path);
    if (!config.exists) {
        return config;
    }

    config.prompted = key_value_file_value(path, "china_network_prompted").value_or("0") == "1";
    config.provider = key_value_file_value(path, "provider").value_or("direct");
    config.download_strategy = key_value_file_value(path, "download_strategy").value_or("direct");
    config.mirror_template = key_value_file_value(path, "mirror_template").value_or("");
    if (const std::optional<MirrorProvider> provider = mirror_provider_by_name(config.provider)) {
        config.mirror_template = provider->mirror_template;
    }

    if (config.download_strategy != "direct" &&
        config.download_strategy != "mirror_first" &&
        config.download_strategy != "direct_first") {
        config.download_strategy = "direct";
        config.mirror_template.clear();
    }
    if (config.download_strategy != "direct" && config.mirror_template.empty()) {
        config.download_strategy = "direct";
    }
    return config;
}

void write_network_config(const NetworkConfig& config) {
    ensure_directory(config_dir_path());
    const std::string content =
        "china_network_prompted=" + std::string(config.prompted ? "1" : "0") + "\n"
        "provider=" + config.provider + "\n"
        "download_strategy=" + config.download_strategy + "\n"
        "mirror_template=" + config.mirror_template + "\n";
    write_text_file_atomic(network_config_path(), content);
}

std::string china_network_disclaimer() {
    return tr("This tool only accelerates public GitHub Release assets for lawful open source downloads;\n"
        "proxy services are third-party providers, and you are responsible for following local laws;\n"
        "yai does not host or rewrite binaries, and indexes point to upstream URLs.");
}

InstallPaths paths_for(const std::string& id) {
    const fs::path app_dir = expand_home_path(".local/share/yai/apps") / id;
    return InstallPaths{
        app_dir,
        app_dir / "current.AppImage",
        app_dir / "extracted",
        expand_home_path(".local/bin") / id,
        expand_home_path(".local/share/applications") / ("yai-" + id + ".desktop"),
        app_dir / "metadata.json",
        app_dir / "metadata.conf",
    };
}

// --- Signal / interrupt handling ---

namespace {

std::atomic<bool> g_interrupted{false};
std::atomic<int> g_interrupt_count{0};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        const int count = g_interrupt_count.fetch_add(1) + 1;
        g_interrupted.store(true);
        if (count >= 2) {
            std::_Exit(1);
        }
    }
}

} // namespace

void install_signal_handler() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

bool was_interrupted() {
    return g_interrupted.load();
}

void check_interrupt() {
    if (g_interrupted.load()) {
        throw std::runtime_error(tr("Operation interrupted by user"));
    }
}

void cleanup_orphan_downloads() {
    // Remove stale .part and .aria2 files left by previous interrupted runs.
    // Also remove .tmp files created by atomic writes.
    const fs::path data_dir = expand_home_path(".local/share/yai");
    if (!fs::exists(data_dir)) {
        return;
    }
    try {
        for (const auto& entry : fs::recursive_directory_iterator(data_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string filename = entry.path().filename().string();
            if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".part") {
                std::error_code ec;
                fs::remove(entry.path(), ec);
            } else if (filename.size() >= 6 && filename.substr(filename.size() - 6) == ".aria2") {
                std::error_code ec;
                fs::remove(entry.path(), ec);
            } else if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".tmp") {
                std::error_code ec;
                fs::remove(entry.path(), ec);
            }
        }
    } catch (const std::exception&) {
        // Best-effort cleanup; never fail startup.
    }
}
