#include "yai.hpp"

#include <cstdio>
#include <ctime>

// URL/HTML parsing helpers for AppImage candidate discovery.

namespace {

std::optional<std::int64_t> tm_to_utc_epoch(std::tm tm) {
    tm.tm_isdst = 0;
#if defined(_WIN32)
    const time_t t = _mkgmtime(&tm);
#else
    const time_t t = timegm(&tm);
#endif
    if (t == static_cast<time_t>(-1)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(t);
}

bool trailing_whitespace_only(const std::string& text, int consumed) {
    if (consumed < 0) {
        return false;
    }
    for (std::size_t i = static_cast<std::size_t>(consumed); i < text.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(text[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool website_url_looks_stale(const std::string& url) {
    const std::string lower = to_lower(strip_url_fragment_query(url));
    std::string path = lower;
    const std::size_t scheme = path.find("://");
    if (scheme != std::string::npos) {
        const std::size_t path_start = path.find('/', scheme + 3);
        path = path_start == std::string::npos ? "" : path.substr(path_start);
    }
    if (path.find("older") != std::string::npos) {
        return true;
    }
    std::size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        const std::size_t start = i;
        while (i < path.size() && path[i] != '/') {
            ++i;
        }
        const std::string seg = path.substr(start, i - start);
        if (seg == "old" || seg == "attic") {
            return true;
        }
    }
    return false;
}

int website_link_stale_penalty(const std::string& url) {
    return website_url_looks_stale(url) ? 1 : 0;
}

std::optional<std::int64_t> parse_directory_listing_mtime(const std::string& text) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int consumed = 0;
    if (std::sscanf(
            text.c_str(),
            "%d-%d-%d %d:%d:%d%n",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second,
            &consumed) >= 6) {
        if (!trailing_whitespace_only(text, consumed)) {
            return std::nullopt;
        }
    } else if (std::sscanf(
                   text.c_str(),
                   "%d-%d-%d %d:%d%n",
                   &year,
                   &month,
                   &day,
                   &hour,
                   &minute,
                   &consumed) == 5) {
        second = 0;
        if (!trailing_whitespace_only(text, consumed)) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return std::nullopt;
    }

    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    return tm_to_utc_epoch(tm);
}

std::optional<std::int64_t> parse_http_last_modified_mtime(const std::string& text) {
    char month_text[4] = {};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    char tz[8] = {};
    if (std::sscanf(
            text.c_str(),
            "%*3[a-zA-Z], %d %3[a-zA-Z] %d %d:%d:%d %7s",
            &day,
            month_text,
            &year,
            &hour,
            &minute,
            &second,
            tz) != 7) {
        return std::nullopt;
    }

    static const char* const months[] = {
        "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec",
    };
    const std::string month_lower = to_lower(std::string(month_text, 3));
    int month = -1;
    for (int i = 0; i < 12; ++i) {
        if (month_lower == months[i]) {
            month = i;
            break;
        }
    }
    if (month < 0) {
        return std::nullopt;
    }

    const std::string tz_lower = to_lower(std::string(tz));
    if (tz_lower != "gmt" && tz_lower != "utc") {
        return std::nullopt;
    }

    if (day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        return std::nullopt;
    }

    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    return tm_to_utc_epoch(tm);
}

std::string strip_url_fragment_query(std::string value) {
    const std::size_t fragment = value.find('#');
    if (fragment != std::string::npos) {
        value.erase(fragment);
    }
    const std::size_t query = value.find('?');
    if (query != std::string::npos) {
        value.erase(query);
    }
    return value;
}

std::string url_origin(const std::string& url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return "";
    }
    const std::size_t host_start = scheme + 3;
    const std::size_t path_start = url.find('/', host_start);
    return path_start == std::string::npos ? url : url.substr(0, path_start);
}

std::string url_host(const std::string& url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return "";
    }
    const std::size_t host_start = scheme + 3;
    const std::size_t path_start = url.find('/', host_start);
    std::string host = url.substr(
        host_start,
        path_start == std::string::npos ? std::string::npos : path_start - host_start);
    const std::size_t at = host.find('@');
    if (at != std::string::npos) {
        host.erase(0, at + 1);
    }
    const std::size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host.erase(colon);
    }
    host = to_lower(host);
    if (host.rfind("www.", 0) == 0) {
        host.erase(0, 4);
    }
    return host;
}

bool is_file_url(const std::string& url) {
    return to_lower(url).rfind("file://", 0) == 0;
}

bool is_appimage_catalog_url(const std::string& url) {
    const std::string host = url_host(url);
    if (host == "appimage.github.io" ||
        host == "appimagehub.com" ||
        host == "appimagehub.org") {
        return true;
    }

    const std::string lower = to_lower(strip_url_fragment_query(url));
    return lower.find("/appimage.github.io/") != std::string::npos ||
           lower.find("/appimagehub/") != std::string::npos;
}

std::string url_directory(const std::string& url) {
    const std::string clean = strip_url_fragment_query(url);
    const std::size_t slash = clean.find_last_of('/');
    if (slash == std::string::npos) {
        return clean + "/";
    }
    return clean.substr(0, slash + 1);
}

std::string resolve_href_url(const std::string& base_url, std::string href) {
    href = trim(replace_all(href, "&amp;", "&"));
    if (href.empty() ||
        href.find('{') != std::string::npos ||
        href.find('}') != std::string::npos ||
        href.rfind("mailto:", 0) == 0 ||
        href.rfind("javascript:", 0) == 0) {
        return "";
    }
    if (has_url_scheme(href)) {
        return href;
    }
    if (href.rfind("//", 0) == 0) {
        const std::size_t scheme = base_url.find("://");
        const std::string base_scheme = scheme == std::string::npos ? "https" : base_url.substr(0, scheme);
        return base_scheme + ":" + href;
    }
    if (href.front() == '/') {
        if (base_url.rfind("file://", 0) == 0) {
            return "file://" + href;
        }
        return url_origin(base_url) + href;
    }
    return url_directory(base_url) + href;
}

std::vector<std::string> html_href_urls(const std::string& html, const std::string& base_url) {
    std::vector<std::string> urls;
    const std::regex href_regex(R"(href\s*=\s*["']([^"']+)["'])", std::regex::icase);
    for (std::sregex_iterator it(html.begin(), html.end(), href_regex), end; it != end; ++it) {
        const std::string url = resolve_href_url(base_url, (*it)[1].str());
        if (!url.empty()) {
            urls.push_back(url);
        }
    }
    return urls;
}

bool html_looks_like_directory_listing(const std::string& html) {
    return html.find("Parent Directory") != std::string::npos ||
           html.find("Last modified") != std::string::npos ||
           html.find("[DIR]") != std::string::npos;
}

namespace {

bool listing_href_skipped(const std::string& href) {
    return href.empty() || href.find("?C=") != std::string::npos;
}

std::string first_listing_row_href(const std::string& row) {
    const std::regex href_regex(R"(href\s*=\s*["']([^"']+)["'])", std::regex::icase);
    for (std::sregex_iterator it(row.begin(), row.end(), href_regex), end; it != end; ++it) {
        const std::string href = (*it)[1].str();
        if (!listing_href_skipped(href)) {
            return href;
        }
    }
    return "";
}

std::optional<std::int64_t> listing_row_mtime(const std::string& row) {
    const std::regex mtime_regex(R"((\d{4}-\d{2}-\d{2} \d{2}:\d{2}))");
    std::smatch match;
    if (std::regex_search(row, match, mtime_regex)) {
        return parse_directory_listing_mtime(match[1].str());
    }
    return std::nullopt;
}

}  // namespace

std::vector<WebsiteLinkMeta> html_directory_listing_links(
    const std::string& html,
    const std::string& base_url) {
    std::vector<WebsiteLinkMeta> links;
    if (!html_looks_like_directory_listing(html)) {
        return links;
    }

    std::size_t pos = 0;
    while (pos < html.size()) {
        const std::size_t tr_start = html.find("<tr", pos);
        if (tr_start == std::string::npos) {
            break;
        }
        const std::size_t content_start = html.find('>', tr_start);
        if (content_start == std::string::npos) {
            break;
        }
        const std::size_t tr_end = html.find("</tr>", content_start);
        if (tr_end == std::string::npos) {
            break;
        }
        const std::string row = html.substr(content_start + 1, tr_end - content_start - 1);
        pos = tr_end + 5;

        if (row.find("Parent Directory") != std::string::npos) {
            continue;
        }

        const std::string href = first_listing_row_href(row);
        if (href.empty()) {
            continue;
        }

        const std::string url = resolve_href_url(base_url, href);
        if (url.empty()) {
            continue;
        }

        links.push_back(WebsiteLinkMeta{
            url,
            listing_row_mtime(row),
            website_link_stale_penalty(url),
        });
    }
    return links;
}

std::vector<WebsiteLinkMeta> website_page_link_metas(
    const std::string& html,
    const std::string& base_url) {
    if (html_looks_like_directory_listing(html)) {
        const std::vector<WebsiteLinkMeta> listing_links = html_directory_listing_links(html, base_url);
        if (!listing_links.empty()) {
            return listing_links;
        }
    }

    std::vector<WebsiteLinkMeta> links;
    for (const std::string& url : html_href_urls(html, base_url)) {
        links.push_back(WebsiteLinkMeta{url, std::nullopt, website_link_stale_penalty(url)});
    }
    return links;
}

bool website_candidate_better(
    const WebsiteLinkMeta& a,
    const WebsiteLinkMeta& b,
    const std::string& arch) {
    const std::string effective = arch.empty() ? current_arch() : normalize_arch(arch);
    const int sa = appimage_asset_score(basename_from_url(a.url), effective);
    const int sb = appimage_asset_score(basename_from_url(b.url), effective);
    // Reject invalid scores first; among valid (≥0) prefer non-stale, then mtime,
    // then arch/basename as tie-break (design §5).
    if (sa < 0 || sb < 0) {
        if (sa != sb) {
            return sa > sb;
        }
    }
    if (a.stale_penalty != b.stale_penalty) {
        return a.stale_penalty < b.stale_penalty;
    }
    if (a.mtime.has_value() != b.mtime.has_value()) {
        return a.mtime.has_value();
    }
    if (a.mtime.has_value() && b.mtime.has_value() && *a.mtime != *b.mtime) {
        return *a.mtime > *b.mtime;
    }
    if (sa != sb) {
        return sa > sb;
    }
    return false;
}

std::string best_website_appimage_url(
    const std::vector<WebsiteLinkMeta>& candidates,
    const std::string& arch) {
    const std::string effective = arch.empty() ? current_arch() : normalize_arch(arch);
    const WebsiteLinkMeta* best = nullptr;
    for (const WebsiteLinkMeta& candidate : candidates) {
        if (!is_appimage_download_url(candidate.url)) {
            continue;
        }
        if (appimage_asset_score(basename_from_url(candidate.url), effective) < 0) {
            continue;
        }
        if (best == nullptr || website_candidate_better(candidate, *best, arch)) {
            best = &candidate;
        }
    }
    return best == nullptr ? "" : best->url;
}

bool is_kde_stable_download_url(const std::string& url) {
    const std::string lower = to_lower(strip_url_fragment_query(url));
    return lower.find("download.kde.org/stable/kdenlive/") != std::string::npos ||
           lower.find("download.kde.org/stable/krita/") != std::string::npos;
}

bool is_appimage_download_url(const std::string& url) {
    const std::string lower = to_lower(strip_url_fragment_query(url));
    return lower.size() >= 9 &&
           lower.compare(lower.size() - 9, 9, ".appimage") == 0;
}

namespace {

std::vector<std::string> html_appimage_urls(const std::string& html, const std::string& base_url) {
    std::vector<std::string> urls;
    for (const std::string& link : html_href_urls(html, base_url)) {
        if (is_appimage_download_url(link)) {
            urls.push_back(link);
        }
    }

    const std::regex value_regex(R"(value\s*=\s*["']([^"']+)["'])", std::regex::icase);
    for (std::sregex_iterator it(html.begin(), html.end(), value_regex), end; it != end; ++it) {
        const std::string url = resolve_href_url(base_url, (*it)[1].str());
        if (is_appimage_download_url(url)) {
            urls.push_back(url);
        }
    }
    return urls;
}

} // namespace

bool vector_contains(const std::vector<std::string>& values, const std::string& value) {
    for (const std::string& item : values) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

bool package_name_matches_url(const RepoPackage& package, const std::string& url) {
    const std::string lower = to_lower(url);
    if (!package.id.empty() && lower.find(to_lower(package.id)) != std::string::npos) {
        return true;
    }
    const std::string name_id = sanitize_id(package.name);
    return !name_id.empty() && lower.find(to_lower(name_id)) != std::string::npos;
}

std::vector<std::string> official_download_hint_urls(const RepoPackage& package) {
    // Curated hints are seeded before generic homepage crawling for projects
    // whose real AppImage is known to live on a dedicated download host or page.
    const std::string id = to_lower(package.id);
    const std::string name = to_lower(package.name);
    std::vector<std::string> urls;
    if (id.find("kdenlive") != std::string::npos || name.find("kdenlive") != std::string::npos) {
        urls.push_back("https://kdenlive.org/en/download/");
        urls.push_back("https://download.kde.org/stable/kdenlive/");
    }
    if (id.find("krita") != std::string::npos || name.find("krita") != std::string::npos) {
        urls.push_back("https://krita.org/en/download/");
        urls.push_back("https://download.kde.org/stable/krita/");
    }
    if (id.find("gimp") != std::string::npos || name.find("gimp") != std::string::npos) {
        urls.push_back("https://www.gimp.org/downloads/");
    }
    return urls;
}

void add_allowed_host(std::vector<std::string>& hosts, const std::string& url) {
    const std::string host = url_host(url);
    if (!host.empty() && !vector_contains(hosts, host)) {
        hosts.push_back(host);
    }
}

std::vector<std::string> allowed_website_hosts(
    const RepoPackage& package,
    const std::vector<std::string>& hint_urls) {
    // Website-page crawling is intentionally domain bounded: start from the feed
    // source, declared homepage, and built-in official download hints. The only
    // dynamic expansion is the catalog-to-project bridge below, and only when the
    // URL visibly matches the package name.
    std::vector<std::string> hosts;
    add_allowed_host(hosts, strip_unexpanded_url_placeholder(package.source_url));
    add_allowed_host(hosts, strip_unexpanded_url_placeholder(package.homepage));
    for (const std::string& url : hint_urls) {
        add_allowed_host(hosts, url);
    }
    return hosts;
}

bool host_matches_allowed(const std::string& host, const std::vector<std::string>& allowed_hosts) {
    for (const std::string& allowed : allowed_hosts) {
        if (host == allowed ||
            (host.size() > allowed.size() &&
             host.compare(host.size() - allowed.size(), allowed.size(), allowed) == 0 &&
             host[host.size() - allowed.size() - 1] == '.')) {
            return true;
        }
    }
    return false;
}

bool is_allowed_website_url(
    const std::string& url,
    const RepoPackage& package,
    const std::vector<std::string>& allowed_hosts,
    bool allow_package_name_match) {
    if (is_file_url(url)) {
        return true;
    }
    const std::string host = url_host(url);
    return !host.empty() &&
           (host_matches_allowed(host, allowed_hosts) ||
            (allow_package_name_match && package_name_matches_url(package, url)));
}

bool should_follow_download_page(
    const std::string& url,
    const RepoPackage& package,
    const std::vector<std::string>& allowed_hosts,
    bool allow_package_name_match) {
    // A website_page source is not permission to crawl the wider web. Reject
    // discussion/social hosts, ordinary GitHub HTML pages, assets from unrelated
    // domains, and static resources so catalog noise cannot become a candidate
    // source.
    const std::string lower = to_lower(strip_url_fragment_query(url));
    if (lower.empty() ||
        !is_allowed_website_url(url, package, allowed_hosts, allow_package_name_match) ||
        lower.find("github.com/") != std::string::npos ||
        (lower.find("appimage.github.io/") != std::string::npos &&
         !package_name_matches_url(package, lower)) ||
        lower.find("bugs.") != std::string::npos ||
        lower.find("bugtracker") != std::string::npos ||
        lower.find("donat") != std::string::npos ||
        lower.find("docs") != std::string::npos ||
        lower.find("forum") != std::string::npos ||
        lower.find("reddit.com") != std::string::npos ||
        lower.find("old.reddit.com") != std::string::npos ||
        lower.find("lemmy.") != std::string::npos ||
        lower.find("discord.") != std::string::npos ||
        lower.find("twitter.com") != std::string::npos ||
        lower.find("x.com") != std::string::npos ||
        lower.find("facebook.com") != std::string::npos ||
        lower.find("youtube.com") != std::string::npos ||
        lower.find(".css") != std::string::npos ||
        lower.find(".js") != std::string::npos ||
        lower.find(".png") != std::string::npos ||
        lower.find(".jpg") != std::string::npos ||
        lower.find(".jpeg") != std::string::npos ||
        lower.find(".svg") != std::string::npos) {
        return false;
    }
    if (is_kde_stable_download_url(lower)) {
        return true;
    }
    if ((lower.find("www.gimp.org/") != std::string::npos ||
         lower.find("krita.org/") != std::string::npos ||
         lower.find("kdenlive.org/") != std::string::npos) &&
        lower.find("download") == std::string::npos &&
        lower.find("linux") == std::string::npos &&
        lower.find("appimage") == std::string::npos) {
        return false;
    }
    if (allow_package_name_match && package_name_matches_url(package, lower)) {
        return true;
    }
    return lower.find("download") != std::string::npos ||
           lower.find("linux") != std::string::npos ||
           lower.find("appimage") != std::string::npos;
}

bool is_allowed_appimage_candidate(
    const std::string& url,
    const RepoPackage& package,
    const std::vector<std::string>& allowed_hosts) {
    // A candidate must be both an AppImage-looking URL and inside the allowed
    // project/hint host set, except for the controlled package-name catalog
    // bridge handled by is_allowed_website_url.
    return is_appimage_download_url(url) &&
           is_allowed_website_url(url, package, allowed_hosts, true);
}

std::string best_appimage_url_from_candidates(
    const std::vector<std::string>& candidates,
    const std::string& arch) {
    const std::string effective_arch = arch.empty() ? current_arch() : normalize_arch(arch);
    int best_score = -1;
    std::string best_url;
    for (const std::string& url : candidates) {
        if (!is_appimage_download_url(url)) {
            continue;
        }
        const int score = appimage_asset_score(basename_from_url(url), effective_arch);
        if (score > best_score) {
            best_score = score;
            best_url = url;
        }
    }
    if (best_score < 0) {
        return "";
    }
    return best_url;
}

bool file_looks_like_html(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string prefix(512, '\0');
    in.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    prefix.resize(static_cast<std::size_t>(in.gcount()));
    prefix = to_lower(trim(prefix));
    return prefix.rfind("<!doctype html", 0) == 0 ||
           prefix.rfind("<html", 0) == 0 ||
           prefix.find("<html") != std::string::npos;
}

namespace {

bool text_looks_like_html(std::string text) {
    if (text.size() > 512) {
        text.resize(512);
    }
    text = to_lower(trim(text));
    return text.rfind("<!doctype html", 0) == 0 ||
           text.rfind("<html", 0) == 0 ||
           text.find("<html") != std::string::npos;
}

} // namespace

std::string appimage_url_from_download_landing_html(
    const std::string& html,
    const std::string& base_url,
    const std::string& arch) {
    if (!text_looks_like_html(html)) {
        return "";
    }
    return best_appimage_url_from_candidates(html_appimage_urls(html, base_url), arch);
}

std::string appimage_url_from_download_landing_page(
    const fs::path& path,
    const std::string& base_url,
    const std::string& arch) {
    if (!file_looks_like_html(path)) {
        return "";
    }
    return appimage_url_from_download_landing_html(read_text_file(path), base_url, arch);
}
