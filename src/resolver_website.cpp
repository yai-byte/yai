#include "yai.hpp"

// Host-bounded website crawling to discover AppImage download URLs.

std::string truncate_status_text(const std::string& value, std::size_t max_size) {
    if (value.size() <= max_size) {
        return value;
    }
    if (max_size <= 3) {
        return value.substr(0, max_size);
    }
    return value.substr(0, max_size - 3) + "...";
}

namespace {

class WebsiteSearchProgress {
public:
    explicit WebsiteSearchProgress(const std::string& package_id)
        : package_id_(package_id), interactive_(isatty(STDERR_FILENO) != 0) {
        // Search status is diagnostic output. Keep it on stderr so search, info,
        // and other tabular stdout contracts remain script-friendly.
        std::cerr << tr("yai: website search for ") << package_id_ << "\n";
    }

    void queued() {
        ++queued_;
    }

    void checked(const std::string& url) {
        ++checked_;
        latest_url_ = url;
        render("checking");
    }

    void skipped() {
        ++skipped_;
        render("skipped");
    }

    void candidate() {
        ++candidates_;
        render("found candidate");
    }

    void selected(const std::string& url) {
        clear_interactive_line();
        std::cerr << tr_format(
            "yai: website search selected {url} after checking {checked} page(s), queued {queued}, skipped {skipped}\n",
            {
                {"{url}", url},
                {"{checked}", std::to_string(checked_)},
                {"{queued}", std::to_string(queued_)},
                {"{skipped}", std::to_string(skipped_)},
            });
    }

private:
    void render(const std::string& state) {
        if (!interactive_) {
            return;
        }
        std::ostringstream line;
        line << tr_format(
            "yai: {state} website pages: checked {checked}, queued {queued}, candidates {candidates} - {url}",
            {
                {"{state}", state_text(state)},
                {"{checked}", std::to_string(checked_)},
                {"{queued}", std::to_string(queued_)},
                {"{candidates}", std::to_string(candidates_)},
                {"{url}", latest_url_},
            });
        std::string text = truncate_status_text(line.str(), 120);
        if (text.size() < last_width_) {
            text.append(last_width_ - text.size(), ' ');
        }
        last_width_ = text.size();
        std::cerr << '\r' << text << std::flush;
    }

    std::string state_text(const std::string& state) const {
        if (state == "checking") {
            return tr("checking");
        }
        if (state == "skipped") {
            return tr("skipped");
        }
        if (state == "found candidate") {
            return tr("found candidate");
        }
        return state;
    }

    void clear_interactive_line() {
        if (!interactive_ || last_width_ == 0) {
            return;
        }
        std::cerr << '\r' << std::string(last_width_, ' ') << '\r' << std::flush;
        last_width_ = 0;
    }

    std::string package_id_;
    bool interactive_ = false;
    std::size_t checked_ = 0;
    std::size_t queued_ = 0;
    std::size_t skipped_ = 0;
    std::size_t candidates_ = 0;
    std::size_t last_width_ = 0;
    std::string latest_url_;
};

struct WebsiteQueueItem {
    std::string url;
    int depth = 0;
};

struct WebsiteSearchState {
    std::vector<WebsiteQueueItem> queue;
    std::vector<std::string> seen;
    std::vector<std::string> appimage_urls;
    std::vector<std::string> allowed_hosts;
};

bool queue_contains_url(const std::vector<WebsiteQueueItem>& queue, const std::string& url) {
    for (const WebsiteQueueItem& item : queue) {
        if (item.url == url) {
            return true;
        }
    }
    return false;
}

bool queue_website_url(
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress,
    const std::string& url,
    int depth) {
    if (url.empty() ||
        vector_contains(state.seen, url) ||
        queue_contains_url(state.queue, url) ||
        state.queue.size() >= 128) {
        return false;
    }
    state.queue.push_back(WebsiteQueueItem{url, depth});
    progress.queued();
    return true;
}

std::vector<std::string> common_project_download_urls(const std::string& project_url) {
    const std::string base = is_file_url(project_url) ? url_directory(project_url) : url_origin(project_url) + "/";
    if (base.empty()) {
        return {};
    }
    return {
        base + "download",
        base + "downloads",
        base + "download.html",
        base + "downloads.html",
        base + "en/download",
        base + "en/downloads",
        base + "en/download.html",
        base + "en/downloads.html",
        base + "linux",
        base + "linux/download",
    };
}

void queue_common_project_download_urls(
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress,
    const std::string& project_url,
    int depth) {
    for (const std::string& url : common_project_download_urls(project_url)) {
        queue_website_url(state, progress, url, depth);
    }
}

WebsiteSearchState initial_website_search_state(
    const RepoPackage& package,
    WebsiteSearchProgress& progress) {
    WebsiteSearchState state;
    const std::string source_page = strip_unexpanded_url_placeholder(package.source_url);
    const std::string homepage = strip_unexpanded_url_placeholder(package.homepage);
    const std::vector<std::string> hint_urls = official_download_hint_urls(package);
    state.allowed_hosts = allowed_website_hosts(package, hint_urls);
    // Hints are queued before feed homepage/source pages so known official
    // download URLs win over broader marketing pages when both exist.
    for (const std::string& url : hint_urls) {
        if (url != source_page && url != homepage) {
            state.queue.push_back(WebsiteQueueItem{url, 0});
            progress.queued();
        }
    }
    state.queue.push_back(WebsiteQueueItem{source_page, 0});
    progress.queued();
    if (!homepage.empty() && homepage != source_page) {
        state.queue.push_back(WebsiteQueueItem{homepage, 0});
        progress.queued();
    }
    return state;
}

std::optional<std::vector<std::string>> fetch_website_links(
    const std::string& page_url,
    WebsiteSearchProgress& progress) {
    progress.checked(page_url);
    try {
        std::vector<std::string> links = html_href_urls(fetch_text(page_url), page_url);
        if (is_kde_stable_download_url(page_url)) {
            std::reverse(links.begin(), links.end());
        }
        return links;
    } catch (const std::exception&) {
        progress.skipped();
        return std::nullopt;
    }
}

void record_appimage_candidate(
    const std::string& link,
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress) {
    state.appimage_urls.push_back(link);
    progress.candidate();
}

bool should_probe_appimage_landing_url(const std::string& url, const RepoPackage& package) {
    const std::string lower = to_lower(strip_url_fragment_query(url));
    return is_appimage_download_url(url) &&
           lower.find("download") != std::string::npos &&
           package_name_matches_url(package, url);
}

std::string resolved_appimage_candidate(
    const std::string& link,
    const RepoPackage& package,
    const std::string& arch) {
    if (!should_probe_appimage_landing_url(link, package)) {
        return link;
    }
    try {
        const std::string landing_url =
            appimage_url_from_download_landing_html(fetch_text(link), link, arch);
        if (!landing_url.empty()) {
            return landing_url;
        }
    } catch (const std::exception&) {
    }
    return link;
}

bool should_queue_website_link(
    const std::string& link,
    const RepoPackage& package,
    const WebsiteQueueItem& page,
    const WebsiteSearchState& state) {
    return page.depth < 2 &&
           should_follow_download_page(
               link,
               package,
               state.allowed_hosts,
               is_appimage_catalog_url(page.url)) &&
           !vector_contains(state.seen, link) &&
           state.queue.size() < 128;
}

void queue_website_link(
    const std::string& link,
    const RepoPackage& package,
    const WebsiteQueueItem& page,
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress) {
    if (is_appimage_catalog_url(page.url) && package_name_matches_url(package, link)) {
        // AppImageHub/AppImage catalog pages are allowed to hand off to a
        // matching project site once; later pages must stay inside the
        // expanded allowed-host set or be a package-name AppImage match.
        add_allowed_host(state.allowed_hosts, link);
        queue_common_project_download_urls(state, progress, link, page.depth + 1);
    }
    queue_website_url(state, progress, link, page.depth + 1);
}

void collect_appimage_candidates(
    const std::vector<std::string>& links,
    const RepoPackage& package,
    const WebsiteQueueItem& page,
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress,
    const std::string& arch) {
    for (const std::string& link : links) {
        if (is_allowed_appimage_candidate(link, package, state.allowed_hosts)) {
            record_appimage_candidate(
                resolved_appimage_candidate(link, package, arch),
                state,
                progress);
            continue;
        }
        if (should_queue_website_link(link, package, page, state)) {
            queue_website_link(link, package, page, state, progress);
        }
    }
}

std::string selected_website_candidate(
    const std::vector<std::string>& appimage_urls,
    const std::string& arch,
    WebsiteSearchProgress& progress) {
    const std::string best = best_appimage_url_from_candidates(appimage_urls, arch);
    if (!best.empty()) {
        progress.selected(best);
    }
    return best;
}

}  // namespace

std::string resolve_website_appimage_download(const RepoPackage& package, const std::string& arch) {
    WebsiteSearchProgress progress(package.id);
    WebsiteSearchState state = initial_website_search_state(package, progress);

    // AppImageHub and appimage.github.io pages are catalogs, not download trust
    // boundaries. They may bridge to a package-matching project host once, after
    // which normal allowed-host checks and AppImage asset scoring apply.
    for (std::size_t index = 0; index < state.queue.size() && index < 96; ++index) {
        const WebsiteQueueItem page = state.queue[index];
        if (vector_contains(state.seen, page.url)) {
            continue;
        }
        state.seen.push_back(page.url);

        const std::optional<std::vector<std::string>> links = fetch_website_links(page.url, progress);
        if (!links.has_value()) {
            continue;
        }
        collect_appimage_candidates(*links, package, page, state, progress, arch);

        const std::string best = selected_website_candidate(state.appimage_urls, arch, progress);
        if (!best.empty()) {
            return best;
        }
    }
    throw std::runtime_error(tr("no AppImage download link found on website pages for ") + package.id);
}
