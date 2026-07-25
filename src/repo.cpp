#include "yai.hpp"

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
        return fetch_text(location);
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
    write_text_file(repo_config_path(), content);
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

std::string resolve_configured_repo_name(const std::string& pattern) {
    const std::vector<std::string> matches = resolve_configured_repo_names(pattern);
    if (matches.size() > 1) {
        throw std::runtime_error(tr("multi-match pattern requires list resolver"));
    }
    return matches.front();
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
    write_text_file(repos_dir_path() / "index.json", repo_index_json_from_package_objects(packages));
}

std::vector<RepoPackage> find_repo_packages(const std::string& id) {
    const std::vector<RepoPackage> packages = load_repo_packages();
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
