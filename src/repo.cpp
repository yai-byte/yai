#include "yai.hpp"

#include <cstdio>

// Repo support owns schema-v1 index parsing, path helpers, and repo entry
// configuration. AppImage feed normalization lives in repo_feed.cpp.

std::string repo_index_json_from_package_objects(const std::vector<std::string>& packages);

fs::path repo_index_path() {
    const char* env = std::getenv("YAI_REPO_INDEX");
    if (env != nullptr && std::string(env).empty() == false && !has_url_scheme(env)) {
        return fs::path(env);
    }
    return expand_home_path(".local/share/yai/repos/index.json");
}

fs::path repos_dir_path() {
    return expand_home_path(".local/share/yai/repos");
}

fs::path repo_config_path() {
    return repos_dir_path() / "repos.conf";
}

fs::path named_repo_index_path(const std::string& name) {
    return repos_dir_path() / (name + ".json");
}

std::string current_utc_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm {};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string repo_index_json_from_package_objects(const std::vector<std::string>& packages) {
    std::string index =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"updated_at\": \"" + json_escape_string(current_utc_timestamp()) + "\",\n"
        "  \"packages\": [\n";
    for (std::size_t i = 0; i < packages.size(); ++i) {
        index += packages[i];
        if (i + 1 < packages.size()) {
            index += ",";
        }
        index += "\n";
    }
    index +=
        "  ]\n"
        "}\n";
    return index;
}

std::string load_repo_index_text() {
    const char* env = std::getenv("YAI_REPO_INDEX");
    if (env != nullptr && std::string(env).empty() == false) {
        const std::string location = env;
        if (has_url_scheme(location)) {
            return fetch_text(location);
        }
        return read_text_file(location);
    }
    return read_text_file(repo_index_path());
}

RepoPackage parse_repo_package(const std::string& object_text) {
    // Validate the fields that later command paths require. Optional descriptive
    // fields may be empty, but installable source types must carry the URL or
    // GitHub owner/repo needed by resolver.cpp.
    RepoPackage package;
    package.id = json_find_string(object_text, "id").value_or("");
    package.name = json_find_string(object_text, "name").value_or(package.id);
    package.summary = json_find_string(object_text, "summary").value_or("");
    package.homepage = json_find_string(object_text, "homepage").value_or("");
    package.license = json_find_string(object_text, "license").value_or("");

    const std::string source = json_find_object(object_text, "source").value_or("");
    package.source_type = json_find_string(source, "type").value_or("");
    package.source_owner = json_find_string(source, "owner").value_or("");
    package.source_repo = json_find_string(source, "repo").value_or("");
    package.source_url = json_find_string(source, "url").value_or("");
    package.source_reason = json_find_string(source, "reason").value_or("");
    package.asset_pattern = json_find_string(source, "asset_pattern").value_or("");
    package.download_url = json_find_string(object_text, "download_url").value_or("");
    package.resolved_at = json_find_string(object_text, "resolved_at").value_or("");
    package.arch = json_find_string(object_text, "arch").value_or("");
    package.version = json_find_string(object_text, "version").value_or("");
    package.source_origin = json_find_string(object_text, "source_origin").value_or("");
    for (const auto& [arch, url] : json_find_string_map(object_text, "download_urls")) {
        package.download_urls[normalize_arch(arch)] = url;
    }

    if (package.id.empty()) {
        throw std::runtime_error(tr("repo index package is missing id"));
    }
    if (package.source_type != "github_release" &&
        package.source_type != "direct_url" &&
        package.source_type != "website_page" &&
        package.source_type != "unavailable") {
        throw std::runtime_error(tr("unsupported package source type for ") + package.id + tr(": ") + package.source_type);
    }
    if (package.source_type == "github_release" &&
        (package.source_owner.empty() || package.source_repo.empty())) {
        throw std::runtime_error(tr("github_release package is missing owner or repo: ") + package.id);
    }
    if (package.source_type == "direct_url" && package.source_url.empty()) {
        throw std::runtime_error(tr("direct_url package is missing url: ") + package.id);
    }
    if (package.source_type == "website_page" && package.source_url.empty()) {
        throw std::runtime_error(tr("website_page package is missing url: ") + package.id);
    }
    return package;
}

std::string serialize_repo_package(const RepoPackage& package) {
    // Match schema-v1 package object layout used by feed normalization. Optional
    // download URL fields are omitted when empty so existing indexes stay lean.
    std::string source =
        "      \"source\": {\n"
        "        \"type\": \"" + json_escape_string(package.source_type) + "\"";
    if (package.source_type == "github_release") {
        source +=
            ",\n"
            "        \"owner\": \"" + json_escape_string(package.source_owner) + "\",\n"
            "        \"repo\": \"" + json_escape_string(package.source_repo) + "\"";
        if (!package.asset_pattern.empty()) {
            source +=
                ",\n"
                "        \"asset_pattern\": \"" + json_escape_string(package.asset_pattern) + "\"";
        }
    } else if (package.source_type == "direct_url" || package.source_type == "website_page") {
        source +=
            ",\n"
            "        \"url\": \"" + json_escape_string(package.source_url) + "\"";
        if (!package.source_reason.empty()) {
            source +=
                ",\n"
                "        \"reason\": \"" + json_escape_string(package.source_reason) + "\"";
        }
    }
    source += "\n      }";

    std::string out =
        "    {\n"
        "      \"id\": \"" + json_escape_string(package.id) + "\",\n"
        "      \"name\": \"" + json_escape_string(package.name) + "\",\n"
        "      \"summary\": \"" + json_escape_string(package.summary) + "\",\n"
        "      \"homepage\": \"" + json_escape_string(package.homepage) + "\",\n"
        "      \"license\": \"" + json_escape_string(package.license) + "\",\n" +
        source;

    if (!package.download_url.empty()) {
        out +=
            ",\n"
            "      \"download_url\": \"" + json_escape_string(package.download_url) + "\"";
    }
    if (!package.download_urls.empty()) {
        out +=
            ",\n"
            "      \"download_urls\": {\n";
        bool first = true;
        for (const auto& [arch, url] : package.download_urls) {
            if (!first) {
                out += ",\n";
            }
            first = false;
            out +=
                "        \"" + json_escape_string(arch) + "\": \"" +
                json_escape_string(url) + "\"";
        }
        out +=
            "\n      }";
    }
    if (!package.resolved_at.empty()) {
        out +=
            ",\n"
            "      \"resolved_at\": \"" + json_escape_string(package.resolved_at) + "\"";
    }
    if (!package.arch.empty()) {
        out +=
            ",\n"
            "      \"arch\": \"" + json_escape_string(package.arch) + "\"";
    }
    if (!package.version.empty()) {
        out +=
            ",\n"
            "      \"version\": \"" + json_escape_string(package.version) + "\"";
    }
    if (!package.source_origin.empty()) {
        out +=
            ",\n"
            "      \"source_origin\": \"" + json_escape_string(package.source_origin) + "\"";
    }
    out +=
        "\n    }";
    return out;
}

std::vector<RepoPackage> load_repo_packages() {
    const std::string index = load_repo_index_text();
    const int schema_version = json_find_int(index, "schema_version").value_or(0);
    if (schema_version != 1) {
        throw std::runtime_error(tr("unsupported repo index schema_version: ") + std::to_string(schema_version));
    }

    const std::optional<std::string> packages_array = json_find_array(index, "packages");
    if (!packages_array.has_value()) {
        throw std::runtime_error(tr("repo index is missing packages array"));
    }

    std::vector<RepoPackage> packages;
    for (const std::string& object : json_top_level_objects(*packages_array)) {
        packages.push_back(parse_repo_package(object));
    }
    return packages;
}

bool repo_index_is_locally_writable() {
    const char* env = std::getenv("YAI_REPO_INDEX");
    if (env == nullptr || std::string(env).empty()) {
        return true;
    }
    return !has_url_scheme(env);
}

void save_repo_packages_index(const std::vector<RepoPackage>& packages, const fs::path& path) {
    std::vector<std::string> objects;
    objects.reserve(packages.size());
    for (const RepoPackage& package : packages) {
        objects.push_back(serialize_repo_package(package));
    }
    if (path.has_parent_path()) {
        ensure_directory(path.parent_path());
    }
    write_text_file_atomic(path, repo_index_json_from_package_objects(objects));
}

RepoPackage merge_repo_package_download_url_fields(
    const RepoPackage& incoming,
    const RepoPackage& previous) {
    RepoPackage out = incoming;
    bool any_merged = false;

    for (const auto& [arch, url] : previous.download_urls) {
        const std::string key = normalize_arch(arch);
        const auto it = out.download_urls.find(key);
        if (it == out.download_urls.end() || it->second.empty()) {
            out.download_urls[key] = url;
            any_merged = true;
        }
    }

    if (!previous.download_url.empty() &&
        incoming.download_url.empty() &&
        incoming.download_urls.empty()) {
        out.download_url = previous.download_url;
        out.resolved_at = previous.resolved_at;
    } else if (any_merged && out.resolved_at.empty()) {
        out.resolved_at = previous.resolved_at;
    }

    return out;
}

std::string merge_named_repo_index_text(
    const RepoEntry& entry,
    const std::string& incoming_index_text) {
    const fs::path named = named_repo_index_path(entry.name);
    if (!fs::exists(named)) {
        return incoming_index_text;
    }

    std::map<std::string, RepoPackage> previous_by_id;
    for (const std::string& object : repo_package_objects_from_index(read_text_file(named))) {
        const RepoPackage package = parse_repo_package(object);
        previous_by_id[package.id] = package;
    }

    std::vector<std::string> objects;
    for (const std::string& object : repo_package_objects_from_index(incoming_index_text)) {
        RepoPackage package = parse_repo_package(object);
        const auto it = previous_by_id.find(package.id);
        if (it != previous_by_id.end()) {
            package = merge_repo_package_download_url_fields(package, it->second);
        }
        objects.push_back(serialize_repo_package(package));
    }
    return repo_index_json_from_package_objects(objects);
}

void upsert_repo_package_download_urls(const RepoPackage& updated) {
    std::vector<RepoPackage> packages = load_repo_packages();
    bool found = false;
    for (RepoPackage& package : packages) {
        if (package.id == updated.id) {
            package = updated;
            found = true;
            break;
        }
    }
    if (found) {
        save_repo_packages_index(packages, repo_index_path());
    }

    for (const RepoEntry& entry : load_repo_entries()) {
        const fs::path cached = named_repo_index_path(entry.name);
        if (!fs::exists(cached)) {
            continue;
        }
        const std::string index_text = read_text_file(cached);
        std::vector<RepoPackage> named_packages;
        bool named_found = false;
        for (const std::string& object : repo_package_objects_from_index(index_text)) {
            RepoPackage package = parse_repo_package(object);
            if (package.id == updated.id) {
                package = updated;
                named_found = true;
            }
            named_packages.push_back(package);
        }
        if (named_found) {
            save_repo_packages_index(named_packages, cached);
        }
    }
}

std::vector<std::string> repo_package_objects_from_index(const std::string& index_text) {
    const int schema_version = json_find_int(index_text, "schema_version").value_or(0);
    if (schema_version != 1) {
        throw std::runtime_error(tr("unsupported repo index schema_version: ") + std::to_string(schema_version));
    }

    const std::optional<std::string> packages_array = json_find_array(index_text, "packages");
    if (!packages_array.has_value()) {
        throw std::runtime_error(tr("repo index is missing packages array"));
    }

    std::vector<std::string> objects = json_top_level_objects(*packages_array);
    for (const std::string& object : objects) {
        parse_repo_package(object);
    }
    return objects;
}

std::string load_repo_source_text(const std::string& location) {
    if (has_url_scheme(location)) {
        return fetch_text(location, kFetchTextFeedTimeoutMs);
    }
    return read_text_file(location);
}

std::vector<RepoEntry> load_repo_entries() {
    std::vector<RepoEntry> entries;
    const fs::path config = repo_config_path();
    if (!fs::exists(config)) {
        return entries;
    }

    std::ifstream in(config);
    if (!in) {
        throw std::runtime_error(tr("failed to read ") + config.string());
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) {
            throw std::runtime_error(tr("invalid repo config line: ") + line);
        }
        entries.push_back(RepoEntry{line.substr(0, tab), line.substr(tab + 1)});
    }
    return entries;
}

void write_repo_entries(const std::vector<RepoEntry>& entries) {
    ensure_directory(repos_dir_path());
    std::string content;
    for (const RepoEntry& entry : entries) {
        content += entry.name + "\t" + entry.location + "\n";
    }
    write_text_file_atomic(repo_config_path(), content);
}

std::string validate_repo_name(const std::string& value) {
    if (contains_line_break(value) || value.find('\t') != std::string::npos) {
        throw std::runtime_error(tr("repo name must not contain whitespace control characters"));
    }
    const std::string name = sanitize_id(value);
    if (name.empty() || name != value) {
        throw std::runtime_error(tr("repo name must already be a safe id: ") + value);
    }
    return name;
}

std::string validate_repo_location(const std::string& value) {
    if (value.empty() || contains_line_break(value) || value.find('\t') != std::string::npos) {
        throw std::runtime_error(tr("repo location must not be empty or contain line breaks"));
    }
    if (has_url_scheme(value)) {
        return value;
    }
    return fs::absolute(value).lexically_normal().string();
}

std::vector<std::string> resolve_configured_repo_names(const std::string& pattern) {
    const std::vector<RepoEntry> entries = load_repo_entries();
    if (!has_glob_wildcards(pattern)) {
        const std::string name = validate_repo_name(pattern);
        for (const RepoEntry& entry : entries) {
            if (entry.name == name) {
                return {name};
            }
        }
        throw std::runtime_error(tr("repo not configured: ") + name);
    }

    std::vector<std::string> matches;
    for (const RepoEntry& entry : entries) {
        if (glob_match_case_insensitive(pattern, entry.name)) {
            matches.push_back(entry.name);
        }
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());

    if (matches.empty()) {
        throw std::runtime_error(tr("repo pattern matched no configured repos: ") + pattern);
    }
    return matches;
}

void rebuild_repo_index_from_cached_files(const std::vector<RepoEntry>& entries) {
    // The combined default index is rebuilt from cached named indexes. A missing
    // cache means that repo has not been fetched yet, so fail instead of silently
    // dropping packages from search/info/install.
    std::vector<std::string> packages;
    for (const RepoEntry& entry : entries) {
        const fs::path cached = named_repo_index_path(entry.name);
        if (!fs::exists(cached)) {
            throw std::runtime_error(tr("repo has not been updated yet: ") + entry.name);
        }
        const std::string index_text = read_text_file(cached);
        const std::vector<std::string> repo_packages = repo_package_objects_from_index(index_text);
        packages.insert(packages.end(), repo_packages.begin(), repo_packages.end());
    }

    ensure_directory(repos_dir_path());
    write_text_file_atomic(repos_dir_path() / "index.json", repo_index_json_from_package_objects(packages));
}

std::vector<RepoPackage> find_repo_packages(const std::string& id) {
    const std::vector<RepoPackage> packages = load_repo_packages_with_overlay();
    if (!has_glob_wildcards(id)) {
        for (const RepoPackage& package : packages) {
            if (package.id == id) {
                return {package};
            }
        }
        return {};
    }

    std::vector<RepoPackage> matches;
    for (const RepoPackage& package : packages) {
        if (!glob_match_case_insensitive(id, package.id)) {
            continue;
        }
        const bool already_matched = std::any_of(
            matches.begin(),
            matches.end(),
            [&package](const RepoPackage& match) { return match.id == package.id; });
        if (!already_matched) {
            matches.push_back(package);
        }
    }
    std::sort(
        matches.begin(),
        matches.end(),
        [](const RepoPackage& a, const RepoPackage& b) { return a.id < b.id; });
    if (matches.empty()) {
        throw std::runtime_error(tr("package pattern matched no repo packages: ") + id);
    }
    return matches;
}

std::optional<RepoPackage> find_repo_package(const std::string& id) {
    const std::vector<RepoPackage> matches = find_repo_packages(id);
    if (matches.empty()) {
        return std::nullopt;
    }
    if (matches.size() > 1) {
        throw std::runtime_error(tr("internal error: use find_repo_packages for multi-match"));
    }
    return matches.front();
}

// --- Index strategy and remote index support ---

std::string detect_index_region() {
    // 1. YAI_INDEX_REGION env var (explicit override)
    const char* env = std::getenv("YAI_INDEX_REGION");
    if (env != nullptr && std::string(env) != "") {
        const std::string value = to_lower(trim(env));
        if (value == "cn" || value == "global") {
            return value;
        }
    }
    // 2. Cached region from region.conf
    const fs::path cache = repos_dir_path() / "region.conf";
    if (fs::exists(cache)) {
        const std::string cached = trim(read_text_file(cache));
        if (cached == "cn" || cached == "global") {
            return cached;
        }
    }
    // 3. Detect from timezone + locale
    bool is_cn = false;
    // Check TZ env var
    const char* tz = std::getenv("TZ");
    if (tz != nullptr) {
        const std::string tz_str(tz);
        if (tz_str.find("Asia/Shanghai") != std::string::npos ||
            tz_str.find("Asia/Urumqi") != std::string::npos ||
            tz_str.find("Asia/Chongqing") != std::string::npos ||
            tz_str.find("Asia/Harbin") != std::string::npos ||
            tz_str.find("Asia/Chungking") != std::string::npos ||
            tz_str.find("PRC") != std::string::npos) {
            is_cn = true;
        }
    }
    // Check /etc/localtime symlink target
    if (!is_cn) {
        std::error_code ec;
        const fs::path localtime = "/etc/localtime";
        if (fs::is_symlink(localtime, ec)) {
            const fs::path target = fs::read_symlink(localtime, ec);
            const std::string target_str = target.string();
            if (target_str.find("Asia/Shanghai") != std::string::npos ||
                target_str.find("Asia/Urumqi") != std::string::npos ||
                target_str.find("Asia/Chongqing") != std::string::npos ||
                target_str.find("Asia/Harbin") != std::string::npos) {
                is_cn = true;
            }
        }
    }
    // Check locale (LANG / LC_ALL)
    if (!is_cn) {
        const char* lang = std::getenv("LANG");
        if (lang != nullptr && std::string(lang).find("zh_CN") != std::string::npos) {
            is_cn = true;
        }
        const char* lc_all = std::getenv("LC_ALL");
        if (lc_all != nullptr && std::string(lc_all).find("zh_CN") != std::string::npos) {
            is_cn = true;
        }
    }
    const std::string region = is_cn ? "cn" : "global";
    // Cache result
    try {
        ensure_directory(repos_dir_path());
        write_text_file_atomic(cache, region);
    } catch (const std::exception&) {
        // Best-effort cache write; must not fail detection.
    }
    return region;
}

std::string default_repo_index_url() {
    return detect_index_region() == "cn" ? kRepoIndexUrlGitee : kRepoIndexUrlGithub;
}

std::string fetch_remote_repo_index_text() {
    const char* env = std::getenv("YAI_REPO_INDEX");
    if (env != nullptr && std::string(env) != "") {
        if (has_url_scheme(env)) {
            return fetch_text(env, kFetchRepoIndexTimeoutMs);
        }
        return read_text_file(env);
    }
    return fetch_text(default_repo_index_url(), kFetchRepoIndexTimeoutMs);
}

std::string repo_index_updated_at(const std::string& index_text) {
    return json_find_string(index_text, "updated_at").value_or("");
}

bool repo_index_is_fresh(const std::string& index_text, int threshold_days) {
    const std::string updated_at = repo_index_updated_at(index_text);
    if (updated_at.empty()) {
        return false;  // No timestamp → treat as stale
    }
    // Parse ISO-8601 UTC: "2026-07-30T12:00:00Z"
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (std::sscanf(updated_at.c_str(), "%d-%d-%dT%d:%d:%dZ",
                    &year, &month, &day, &hour, &min, &sec) != 6) {
        return false;  // Unparseable → treat as stale
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = 0;
    std::time_t updated = timegm(&tm);
    if (updated == static_cast<std::time_t>(-1)) {
        return false;
    }
    const std::time_t now = std::time(nullptr);
    const double age_seconds = std::difftime(now, updated);
    const double threshold_seconds = static_cast<double>(threshold_days) * 86400.0;
    return age_seconds <= threshold_seconds;
}

fs::path resolved_overlay_path() {
    return repos_dir_path() / "resolved-cache.json";
}

std::vector<RepoPackage> load_repo_packages_with_overlay() {
    std::vector<RepoPackage> packages = load_repo_packages();
    const fs::path overlay = resolved_overlay_path();
    if (!fs::exists(overlay)) {
        return packages;
    }
    std::map<std::string, RepoPackage> overlay_by_id;
    try {
        const std::string overlay_text = read_text_file(overlay);
        const std::optional<std::string> packages_array = json_find_array(overlay_text, "packages");
        if (packages_array.has_value()) {
            for (const std::string& object : json_top_level_objects(*packages_array)) {
                const RepoPackage pkg = parse_repo_package(object);
                overlay_by_id[pkg.id] = pkg;
            }
        }
    } catch (const std::exception&) {
        // Corrupt overlay should not break normal operation.
        return packages;
    }
    for (RepoPackage& pkg : packages) {
        const auto it = overlay_by_id.find(pkg.id);
        if (it == overlay_by_id.end()) {
            continue;
        }
        // Merge download URLs: overlay takes precedence for populated arches
        for (const auto& [arch, url] : it->second.download_urls) {
            if (!url.empty()) {
                pkg.download_urls[arch] = url;
            }
        }
        if (!it->second.download_url.empty()) {
            pkg.download_url = it->second.download_url;
        }
        if (!it->second.resolved_at.empty()) {
            pkg.resolved_at = it->second.resolved_at;
        }
        overlay_by_id.erase(it);
    }
    // Append packages that exist only in overlay
    for (const auto& [id, pkg] : overlay_by_id) {
        packages.push_back(pkg);
    }
    return packages;
}

void upsert_overlay_package_download_url(const RepoPackage& updated) {
    const fs::path overlay = resolved_overlay_path();
    std::vector<RepoPackage> packages;
    if (fs::exists(overlay)) {
        try {
            const std::string text = read_text_file(overlay);
            const std::optional<std::string> arr = json_find_array(text, "packages");
            if (arr.has_value()) {
                for (const std::string& obj : json_top_level_objects(*arr)) {
                    packages.push_back(parse_repo_package(obj));
                }
            }
        } catch (const std::exception&) {
            // Corrupt overlay → start fresh
            packages.clear();
        }
    }
    bool found = false;
    for (RepoPackage& pkg : packages) {
        if (pkg.id == updated.id) {
            pkg = updated;
            found = true;
            break;
        }
    }
    if (!found) {
        packages.push_back(updated);
    }
    ensure_directory(overlay.parent_path());
    save_repo_packages_index(packages, overlay);
}
