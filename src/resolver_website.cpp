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
    int priority = 0;
    bool speculative = false;
    std::uint64_t seq = 0;
};

int website_url_priority(const std::string& url) {
    const std::string clean = strip_url_fragment_query(url);
    const std::size_t scheme = clean.find("://");
    const std::size_t path_start =
        scheme == std::string::npos ? 0 : clean.find('/', scheme + 3);
    const std::string lower =
        to_lower(path_start == std::string::npos ? "" : clean.substr(path_start));
    if (lower.find("appimage") != std::string::npos) {
        return 3;
    }
    if (lower.find("download") != std::string::npos) {
        return 2;
    }
    if (lower.find("linux") != std::string::npos) {
        return 1;
    }
    return 0;
}

constexpr int kWebsiteSeedPriority = 100;
constexpr std::size_t kWebsiteFetchConcurrency = 4;
constexpr std::size_t kWebsiteMaxPages = 96;

bool queue_item_better(const WebsiteQueueItem& a, const WebsiteQueueItem& b) {
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    return a.seq < b.seq;
}

struct WebsiteSearchState {
    std::vector<WebsiteQueueItem> queue;
    std::vector<std::string> seen;
    std::vector<std::string> appimage_urls;
    std::vector<std::string> allowed_hosts;
    std::uint64_t next_seq = 0;
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
    int depth,
    int priority,
    bool speculative) {
    if (url.empty() ||
        vector_contains(state.seen, url) ||
        queue_contains_url(state.queue, url) ||
        state.queue.size() >= 128) {
        return false;
    }
    state.queue.push_back(WebsiteQueueItem{
        url,
        depth,
        priority,
        speculative,
        state.next_seq++,
    });
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
        queue_website_url(
            state,
            progress,
            url,
            depth,
            website_url_priority(url),
            true);
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
            queue_website_url(
                state,
                progress,
                url,
                0,
                kWebsiteSeedPriority,
                false);
        }
    }
    queue_website_url(
        state,
        progress,
        source_page,
        0,
        kWebsiteSeedPriority,
        false);
    if (!homepage.empty() && homepage != source_page) {
        queue_website_url(
            state,
            progress,
            homepage,
            0,
            kWebsiteSeedPriority,
            false);
    }
    return state;
}

std::optional<std::vector<std::string>> fetch_website_links(
    const std::string& page_url,
    bool speculative) {
    try {
        const int timeout_ms =
            speculative ? kFetchTextSpeculativeTimeoutMs : kFetchTextDefaultTimeoutMs;
        std::vector<std::string> links =
            html_href_urls(fetch_text(page_url, timeout_ms), page_url);
        if (is_kde_stable_download_url(page_url)) {
            std::reverse(links.begin(), links.end());
        }
        return links;
    } catch (const std::exception&) {
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
        const std::string html = fetch_text_limited(
            link,
            kFetchTextDefaultTimeoutMs,
            kFetchTextLandingMaxBytes);
        const std::string landing_url =
            appimage_url_from_download_landing_html(html, link, arch);
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
    queue_website_url(
        state,
        progress,
        link,
        page.depth + 1,
        website_url_priority(link),
        false);
}

std::optional<std::size_t> next_website_queue_index(const WebsiteSearchState& state) {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < state.queue.size(); ++i) {
        if (vector_contains(state.seen, state.queue[i].url)) {
            continue;
        }
        if (!best.has_value() || queue_item_better(state.queue[i], state.queue[*best])) {
            best = i;
        }
    }
    return best;
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
    std::size_t pages_checked = 0;
    while (pages_checked < kWebsiteMaxPages) {
        std::vector<WebsiteQueueItem> batch;
        while (batch.size() < kWebsiteFetchConcurrency &&
               pages_checked + batch.size() < kWebsiteMaxPages) {
            const std::optional<std::size_t> index = next_website_queue_index(state);
            if (!index.has_value()) {
                break;
            }
            const WebsiteQueueItem page = state.queue[*index];
            state.seen.push_back(page.url);
            progress.checked(page.url);
            batch.push_back(page);
        }
        if (batch.empty()) {
            break;
        }

        std::vector<std::optional<std::vector<std::string>>> outcomes(batch.size());
        std::vector<std::thread> workers;
        workers.reserve(batch.size());
        for (std::size_t i = 0; i < batch.size(); ++i) {
            workers.emplace_back([&batch, &outcomes, i]() {
                outcomes[i] =
                    fetch_website_links(batch[i].url, batch[i].speculative);
            });
        }
        for (std::thread& worker : workers) {
            worker.join();
        }

        for (std::size_t i = 0; i < batch.size(); ++i) {
            ++pages_checked;
            const WebsiteQueueItem& page = batch[i];
            if (outcomes[i].has_value()) {
                continue;
            }
            progress.skipped();
            if (page.speculative) {
                const auto seen = std::find(state.seen.begin(), state.seen.end(), page.url);
                if (seen != state.seen.end()) {
                    state.seen.erase(seen);
                }
                state.queue.erase(
                    std::remove_if(
                        state.queue.begin(),
                        state.queue.end(),
                        [&page](const WebsiteQueueItem& item) {
                            return item.seq == page.seq;
                        }),
                    state.queue.end());
            }
        }

        for (std::size_t i = 0; i < batch.size(); ++i) {
            if (!outcomes[i].has_value()) {
                continue;
            }
            const WebsiteQueueItem& page = batch[i];
            collect_appimage_candidates(
                *outcomes[i], package, page, state, progress, arch);

            const std::string best =
                selected_website_candidate(state.appimage_urls, arch, progress);
            if (!best.empty()) {
                return best;
            }
        }
    }
    throw std::runtime_error(tr("no AppImage download link found on website pages for ") + package.id);
}
