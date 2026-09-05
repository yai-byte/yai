#include "yai.hpp"

// AppImage feed normalization into schema-v1 repo indexes.

std::string repo_index_json_from_package_objects(const std::vector<std::string>& packages);

namespace {
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
} // namespace

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
    // Overridable so the test suite can point the catalog at a local directory
    // instead of dialling out to the real AppImageHub site.
    return trimmed_env_url("YAI_APPIMAGE_CATALOG_BASE", "https://appimage.github.io") + "/" +
           url_encode(name) + "/";
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

std::optional<std::string> appimage_catalog_github_repo_from_html(const std::string& html) {
    // Filter out AppImageHub infrastructure repos that appear on every catalog page
    static const std::vector<std::string> excluded_owners = {
        "appimage",
        "appimagehub",
    };

    std::vector<std::string> candidate_repos;
    for (const std::string& url : html_href_urls(html, "")) {
        const std::optional<std::string> repo = github_repo_from_link(url);
        if (!repo.has_value()) {
            continue;
        }
        // Extract owner from "owner/repo" format
        const std::size_t slash = repo->find('/');
        if (slash == std::string::npos) {
            continue;
        }
        const std::string owner = to_lower(repo->substr(0, slash));
        bool excluded = false;
        for (const std::string& excluded_owner : excluded_owners) {
            if (owner == excluded_owner) {
                excluded = true;
                break;
            }
        }
        if (!excluded) {
            candidate_repos.push_back(*repo);
        }
    }

    // Return the first non-excluded repo found
    if (!candidate_repos.empty()) {
        return candidate_repos.front();
    }
    return std::nullopt;
}

std::optional<std::string> appimage_catalog_direct_download_url_from_html(
    const std::string& html,
    const std::string& base_url) {
    for (const std::string& url : html_href_urls(html, base_url)) {
        const std::string lower = to_lower(url);
        // Must end with .appimage (not just contain it in path)
        // or be a media download URL that serves AppImages
        if ((lower.size() >= 9 &&
             lower.compare(lower.size() - 9, 9, ".appimage") == 0) ||
            (lower.find("/dl/") != std::string::npos &&
             lower.find(".appimage") != std::string::npos)) {
            return url;
        }
    }
    return std::nullopt;
}

std::optional<std::string> appimage_catalog_homepage_from_html(const std::string& html) {
    // CDN and static resource domains to exclude
    static const std::vector<std::string> excluded_hosts = {
        "cdnjs.cloudflare.com",
        "cdn.jsdelivr.net",
        "fonts.googleapis.com",
        "fonts.gstatic.com",
        "maxcdn.bootstrapcdn.com",
        "stackpath.bootstrapcdn.com",
        "unpkg.com",
        "jsdelivr.net",
        "gravatar.com",
        "avatars.githubusercontent.com",
    };
    
    // Static file extensions to exclude
    static const std::vector<std::string> excluded_extensions = {
        ".css", ".js", ".png", ".jpg", ".jpeg", ".gif", ".svg",
        ".ico", ".woff", ".woff2", ".ttf", ".eot",
    };
    
    // Collect valid non-excluded URLs with scores
    struct CandidateUrl {
        std::string url;
        std::string host;
        int score;
    };
    std::vector<CandidateUrl> candidates;
    
    for (const std::string& url : html_href_urls(html, "")) {
        const std::string host = url_host(url);
        const std::string lower_url = to_lower(url);
        
        if (host.empty()) {
            continue;
        }
        
        // Exclude AppImageHub infrastructure
        if (host == "appimage.github.io" ||
            host == "github.com" ||
            host == "githubusercontent.com" ||
            host == "appimagehub.com" ||
            host == "appimagehub.org") {
            continue;
        }
        
        // Exclude CDN and static resource domains
        bool excluded_host = false;
        for (const std::string& excluded : excluded_hosts) {
            if (host == excluded ||
                (host.size() > excluded.size() &&
                 host.compare(host.size() - excluded.size(), excluded.size(), excluded) == 0 &&
                 host[host.size() - excluded.size() - 1] == '.')) {
                excluded_host = true;
                break;
            }
        }
        if (excluded_host) {
            continue;
        }
        
        // Exclude static file extensions
        bool excluded_ext = false;
        for (const std::string& ext : excluded_extensions) {
            if (lower_url.find(ext) != std::string::npos) {
                excluded_ext = true;
                break;
            }
        }
        if (excluded_ext) {
            continue;
        }
        
        // Exclude discourse and other AppImage support sites
        if (lower_url.find("discourse.appimage") != std::string::npos) {
            continue;
        }
        
        // Skip URLs that are direct download files
        if (lower_url.size() >= 9 &&
            lower_url.compare(lower_url.size() - 9, 9, ".appimage") == 0) {
            continue;
        }
        
        // Score the URL
        int score = 0;
        
        // Penalize URLs from appimage.org domain (not the project's own domain)
        if (host == "appimage.org" ||
            (host.size() > 11 && host.compare(host.size() - 11, 11, ".appimage.org") == 0)) {
            score -= 10;
        }
        
        // Penalize URLs from appimagehub domains
        if (host.find("appimagehub") != std::string::npos) {
            score -= 10;
        }
        
        // Reward root-level URLs (homepages)
        std::string path_part = lower_url;
        const std::size_t scheme_end = path_part.find("://");
        if (scheme_end != std::string::npos) {
            const std::size_t path_start = path_part.find('/', scheme_end + 3);
            if (path_start != std::string::npos) {
                path_part = path_part.substr(path_start);
            } else {
                path_part = "/";
            }
        }
        // Root URLs have path "/" or empty after the host
        if (path_part == "/" || path_part.empty()) {
            score += 3;
        } else if (path_part.size() > 1 && path_part.back() == '/' &&
                   path_part.find('/', 1) == path_part.size() - 1) {
            // Like "/something/" - second-level page
            score += 1;
        }
        
        // Penalize download/release pages
        if (lower_url.find("/download") != std::string::npos) {
            score -= 2;
        }
        if (lower_url.find("/release") != std::string::npos) {
            score -= 2;
        }
        
        // Prefer HTTPS
        if (lower_url.rfind("https://", 0) == 0) {
            score += 1;
        }
        
        candidates.push_back({url, host, score});
    }
    
    // Return the highest-scoring candidate
    if (!candidates.empty()) {
        std::sort(candidates.begin(), candidates.end(),
            [](const CandidateUrl& a, const CandidateUrl& b) {
                return a.score > b.score;
            });
        return candidates.front().url;
    }
    
    return std::nullopt;
}

AppImageCatalogSources fetch_appimage_catalog_sources(
    const std::string& name,
    int timeout_ms) {
    AppImageCatalogSources sources;
    const std::string catalog_url = appimage_catalog_page_url(name);
    try {
        const std::string html = fetch_text(catalog_url, timeout_ms);
        sources.fetched = true;
        sources.github_repo = appimage_catalog_github_repo_from_html(html);
        sources.direct_url = appimage_catalog_direct_download_url_from_html(html, catalog_url);
        sources.homepage = appimage_catalog_homepage_from_html(html);
    } catch (const std::exception&) {
        sources.fetched = false;
    }
    return sources;
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
    // Track feed package IDs for matching against apps/ entries
    std::map<std::string, std::string> feed_id_to_json;  // feed_id -> package JSON
    std::map<std::string, std::string> feed_id_to_name;  // feed_id -> package name

    for (const std::string& item : json_top_level_objects(*items_array)) {
        const std::string name = json_find_string(item, "name").value_or("");
        const std::string base_id = sanitize_id(name.empty() ? "appimage-app" : name);
        const std::string id = unique_package_id(base_id, ids);
        ids.push_back(id);
        std::string pkg_json = yai_package_object_from_appimage_feed_item(item, id);
        feed_id_to_json[id] = pkg_json;
        feed_id_to_name[id] = name;
        packages.push_back(pkg_json);
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
