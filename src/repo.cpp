#include "yai.hpp"

// Repo support owns schema-v1 index parsing plus normalization of the upstream
// AppImage feed into that schema. It keeps search/info/install inputs stable by
// converting feed items into RepoPackage objects with explicit source types.
// The minimal schema-v1 package contract is id/name/summary/homepage/license
// plus source.type and the fields required by that source type.

void append_collapsed_whitespace_char(std::string& out, bool& previous_space, unsigned char ch);
void append_html_plain_text_char(std::string& out, char ch, bool& in_tag);
std::string unique_package_id(const std::string& base_id, const std::vector<std::string>& ids);
std::string repo_index_json_from_package_objects(const std::vector<std::string>& packages);
std::string package_match_list(const std::vector<RepoPackage>& packages);

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

std::string collapse_whitespace(const std::string& value) {
    // Feed descriptions often contain HTML and layout whitespace. Repo search
    // stores a compact plain summary so tabular output remains readable.
    std::string out;
    bool previous_space = false;
    for (unsigned char ch : value) {
        append_collapsed_whitespace_char(out, previous_space, ch);
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string html_to_plain_text(const std::string& value) {
    std::string out;
    bool in_tag = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        append_html_plain_text_char(out, value[i], in_tag);
    }
    out = replace_all(out, "&amp;", "&");
    out = replace_all(out, "&lt;", "<");
    out = replace_all(out, "&gt;", ">");
    out = replace_all(out, "&quot;", "\"");
    out = replace_all(out, "&#39;", "'");
    return collapse_whitespace(out);
}

std::string feed_summary_from_description(const std::string& description) {
    const std::string plain = html_to_plain_text(description);
    std::size_t end = std::string::npos;
    for (const char* marker : {". ", "! ", "? "}) {
        const std::size_t pos = plain.find(marker);
        if (pos != std::string::npos && (end == std::string::npos || pos < end)) {
            end = pos;
        }
    }

    const std::string summary = end == std::string::npos ? plain : plain.substr(0, end + 1);
    return truncate_display_width(trim(summary), 140);
}

std::string current_utc_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm {};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

void append_collapsed_whitespace_char(std::string& out, bool& previous_space, unsigned char ch) {
    if (std::isspace(ch)) {
        if (!previous_space && !out.empty()) {
            out.push_back(' ');
        }
        previous_space = true;
        return;
    }

    out.push_back(static_cast<char>(ch));
    previous_space = false;
}

void append_html_plain_text_char(std::string& out, char ch, bool& in_tag) {
    if (ch == '<') {
        in_tag = true;
        out.push_back(' ');
        return;
    }
    if (ch == '>') {
        in_tag = false;
        return;
    }
    if (!in_tag) {
        out.push_back(ch);
    }
}

std::string unique_package_id(const std::string& base_id, const std::vector<std::string>& ids) {
    // AppImage feed display names are not guaranteed unique after sanitizing.
    // Stable numeric suffixes keep every normalized package addressable.
    std::string id = base_id;
    int suffix = 2;
    while (std::find(ids.begin(), ids.end(), id) != ids.end()) {
        id = base_id + "-" + std::to_string(suffix++);
    }
    return id;
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

std::optional<std::string> github_repo_from_link(std::string value) {
    value = trim(value);
    const std::string github_https = "https://github.com/";
    const std::string github_http = "http://github.com/";
    if (value.rfind(github_https, 0) == 0) {
        value = value.substr(github_https.size());
    } else if (value.rfind(github_http, 0) == 0) {
        value = value.substr(github_http.size());
    }

    const std::size_t query = value.find('?');
    if (query != std::string::npos) {
        value.erase(query);
    }
    const std::size_t fragment = value.find('#');
    if (fragment != std::string::npos) {
        value.erase(fragment);
    }
    const std::string releases_suffix = "/releases";
    const std::size_t releases = value.find(releases_suffix);
    if (releases != std::string::npos) {
        value.erase(releases);
    }
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    const std::string git_suffix = ".git";
    if (value.size() > git_suffix.size() &&
        value.compare(value.size() - git_suffix.size(), git_suffix.size(), git_suffix) == 0) {
        value.resize(value.size() - git_suffix.size());
    }

    return looks_like_github_repo(value) ? std::optional<std::string>{value} : std::nullopt;
}

std::optional<std::string> appimage_feed_github_repo(const std::string& item_text) {
    const std::optional<std::string> links_array = json_find_array(item_text, "links");
    if (!links_array.has_value()) {
        return std::nullopt;
    }

    std::optional<std::string> fallback;
    for (const std::string& link : json_top_level_objects(*links_array)) {
        const std::string type = to_lower(json_find_string(link, "type").value_or(""));
        const std::string url = json_find_string(link, "url").value_or("");
        const std::optional<std::string> repo = github_repo_from_link(url);
        if (!repo.has_value()) {
            continue;
        }
        if (type == "github") {
            return repo;
        }
        if (!fallback.has_value()) {
            fallback = repo;
        }
    }
    return fallback;
}

std::optional<std::string> appimage_feed_homepage(const std::string& item_text) {
    const std::optional<std::string> links_array = json_find_array(item_text, "links");
    if (!links_array.has_value()) {
        return std::nullopt;
    }

    for (const std::string& link : json_top_level_objects(*links_array)) {
        const std::string type = to_lower(json_find_string(link, "type").value_or(""));
        if (type == "homepage" || type == "website") {
            const std::string url = json_find_string(link, "url").value_or("");
            if (!url.empty()) {
                return url;
            }
        }
    }
    return std::nullopt;
}

std::string appimage_catalog_page_url(const std::string& name) {
    return "https://appimage.github.io/" + url_encode(name) + "/";
}

std::string strip_unexpanded_url_placeholder(std::string url) {
    const std::size_t placeholder = url.find("{url}");
    if (placeholder != std::string::npos) {
        url.erase(placeholder);
    }
    return url;
}

std::optional<std::string> appimage_feed_direct_download_url(const std::string& item_text) {
    const std::optional<std::string> links_array = json_find_array(item_text, "links");
    if (!links_array.has_value()) {
        return std::nullopt;
    }

    std::optional<std::string> fallback;
    for (const std::string& link : json_top_level_objects(*links_array)) {
        const std::string type = to_lower(json_find_string(link, "type").value_or(""));
        const std::string url = json_find_string(link, "url").value_or("");
        if (to_lower(url).find(".appimage") == std::string::npos) {
            continue;
        }
        if (type == "download") {
            return url;
        }
        if (!fallback.has_value()) {
            fallback = url;
        }
    }
    return fallback;
}

std::string yai_package_object_from_appimage_feed_item(
    const std::string& item_text,
    const std::string& id) {
    // Feed entries vary widely. Prefer a GitHub release source when the feed
    // exposes one, fall back to a direct AppImage URL, and otherwise record a
    // website_page source so resolver.cpp can do bounded discovery later.
    // If no useful source exists, website_page still points at the catalog page
    // so info/search can explain why installation needs discovery.
    const std::string name = json_find_string(item_text, "name").value_or(id);
    const std::string summary = feed_summary_from_description(
        json_find_string(item_text, "description").value_or(""));
    const std::string license = json_find_string(item_text, "license").value_or("Unknown");
    const std::string homepage = strip_unexpanded_url_placeholder(
        appimage_feed_homepage(item_text).value_or(appimage_catalog_page_url(name)));
    const std::optional<std::string> github_repo = appimage_feed_github_repo(item_text);
    const std::optional<std::string> direct_url = appimage_feed_direct_download_url(item_text);

    std::string source =
        "      \"source\": {\n";
    if (github_repo.has_value()) {
        const std::size_t slash = github_repo->find('/');
        const std::string owner = github_repo->substr(0, slash);
        const std::string repo = github_repo->substr(slash + 1);
        source +=
            "        \"type\": \"github_release\",\n"
            "        \"owner\": \"" + json_escape_string(owner) + "\",\n"
            "        \"repo\": \"" + json_escape_string(repo) + "\",\n"
            "        \"asset_pattern\": \".*\\\\.AppImage$\"\n";
    } else if (direct_url.has_value()) {
        source +=
            "        \"type\": \"direct_url\",\n"
            "        \"url\": \"" + json_escape_string(*direct_url) + "\"\n";
    } else {
        source +=
            "        \"type\": \"website_page\",\n"
            "        \"url\": \"" + json_escape_string(homepage) + "\",\n"
            "        \"reason\": \"AppImage feed entry has no GitHub or direct AppImage download link; yai will search the website for an AppImage\"\n";
    }
    source += "      }\n";

    return
        "    {\n"
        "      \"id\": \"" + json_escape_string(id) + "\",\n"
        "      \"name\": \"" + json_escape_string(name) + "\",\n"
        "      \"summary\": \"" + json_escape_string(summary) + "\",\n"
        "      \"homepage\": \"" + json_escape_string(homepage) + "\",\n"
        "      \"license\": \"" + json_escape_string(license.empty() ? "Unknown" : license) + "\",\n" +
        source +
        "    }";
}

std::string normalize_appimage_feed_index(const std::string& feed_text) {
    // The public AppImage feed is not yai's repo schema. Normalize it once at
    // repo-add/update time so command code can operate on a single package
    // format and duplicate names get stable package ids.
    const std::optional<std::string> items_array = json_find_array(feed_text, "items");
    if (!items_array.has_value()) {
        throw std::runtime_error(tr("AppImage feed is missing items array"));
    }

    std::vector<std::string> packages;
    std::vector<std::string> ids;
    for (const std::string& item : json_top_level_objects(*items_array)) {
        const std::string name = json_find_string(item, "name").value_or("");
        const std::string base_id = sanitize_id(name.empty() ? "appimage-app" : name);
        const std::string id = unique_package_id(base_id, ids);
        ids.push_back(id);
        packages.push_back(yai_package_object_from_appimage_feed_item(item, id));
    }

    return repo_index_json_from_package_objects(packages);
}

std::string normalize_repo_source_index(const std::string& index_text) {
    // Repo sources are accepted in only two shapes: yai schema v1 or the public
    // AppImage feed v1. Both are validated and rewritten into schema v1 before
    // command code reads packages.
    if (json_find_int(index_text, "schema_version").value_or(0) == 1 &&
        json_find_array(index_text, "packages").has_value()) {
        repo_package_objects_from_index(index_text);
        return index_text;
    }
    if (json_find_int(index_text, "version").value_or(0) == 1 &&
        json_find_array(index_text, "items").has_value()) {
        const std::string normalized = normalize_appimage_feed_index(index_text);
        repo_package_objects_from_index(normalized);
        return normalized;
    }
    throw std::runtime_error(tr("unsupported repo source format"));
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

std::optional<RepoPackage> find_repo_package(const std::string& id) {
    const std::vector<RepoPackage> packages = load_repo_packages();
    for (const RepoPackage& package : packages) {
        if (package.id == id) {
            return package;
        }
    }
    if (!has_glob_wildcards(id)) {
        return std::nullopt;
    }

    std::vector<RepoPackage> matches;
    for (const RepoPackage& package : packages) {
        if (glob_match_case_insensitive(id, package.id)) {
            matches.push_back(package);
        }
    }
    if (matches.empty()) {
        throw std::runtime_error(tr("package pattern matched no repo packages: ") + id);
    }
    if (matches.size() > 1) {
        throw std::runtime_error(tr_format(
            "package pattern is ambiguous: {pattern} (matches: {matches})",
            {{"{pattern}", id}, {"{matches}", package_match_list(matches)}}));
    }
    return matches.front();
}

std::string package_match_list(const std::vector<RepoPackage>& packages) {
    std::vector<std::string> ids;
    for (const RepoPackage& package : packages) {
        ids.push_back(package.id);
    }
    std::sort(ids.begin(), ids.end());

    std::string out;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += ids[i];
    }
    return out;
}
