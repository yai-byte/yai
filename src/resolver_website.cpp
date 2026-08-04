#include "yai.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <queue>
#include <thread>

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
    std::optional<std::int64_t> mtime;
    int stale_penalty = 0;
};

constexpr int kWebsiteSeedPriority = 100;
constexpr std::size_t kMaxWebsiteCrawlConcurrency = 4;
constexpr std::size_t kWebsiteMaxPages = 96;
constexpr std::size_t kWebsiteCandidateSoftCap = 8;
constexpr std::size_t kWebsiteHeadFillMax = 4;
constexpr std::size_t kWebsiteListingFollowNonStaleMax = 3;
constexpr std::size_t kWebsiteListingFollowStaleMax = 1;

bool queue_item_better(const WebsiteQueueItem& a, const WebsiteQueueItem& b) {
    if (a.stale_penalty != b.stale_penalty) {
        return a.stale_penalty < b.stale_penalty;
    }
    if (a.mtime.has_value() != b.mtime.has_value()) {
        return a.mtime.has_value();
    }
    if (a.mtime.has_value() && b.mtime.has_value() && *a.mtime != *b.mtime) {
        return *a.mtime > *b.mtime;
    }
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    return a.seq < b.seq;
}

struct WebsiteSearchState {
    std::vector<WebsiteQueueItem> queue;
    std::vector<std::string> seen;
    std::vector<WebsiteLinkMeta> appimage_candidates;
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
    bool speculative,
    std::optional<std::int64_t> mtime = std::nullopt,
    std::optional<int> stale_penalty = std::nullopt) {
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
        mtime,
        stale_penalty.value_or(website_link_stale_penalty(url)),
    });
    progress.queued();
    return true;
}

std::vector<std::string> common_project_download_urls(const std::string& project_url) {
    const std::string origin = url_origin(project_url);
    if (origin.empty()) {
        return {};
    }
    const std::string base = origin + "/";
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
        base + "release",
        base + "releases",
        base + "en/releases",
        base + "en/release",
        base + "platform",
        base + "platforms",
        base + "version",
        base + "versions",
        base + "en/platform",
        base + "en/platforms",
        base + "en/download/linux",
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

std::optional<std::vector<WebsiteLinkMeta>> fetch_website_links(
    const std::string& page_url,
    bool speculative) {
    try {
        const int timeout_ms =
            speculative ? kFetchTextSpeculativeTimeoutMs : kFetchTextDefaultTimeoutMs;
        return website_page_link_metas(fetch_text(page_url, timeout_ms), page_url);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void record_appimage_candidate(
    WebsiteLinkMeta meta,
    const WebsiteQueueItem& page,
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress) {
    if (!meta.mtime.has_value()) {
        meta.mtime = page.mtime;
    }
    meta.stale_penalty = std::max(
        {meta.stale_penalty, page.stale_penalty, website_link_stale_penalty(meta.url)});
    state.appimage_candidates.push_back(std::move(meta));
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
    return page.depth < 3 &&
           should_follow_download_page(
               link,
               package,
               state.allowed_hosts,
               is_appimage_catalog_url(page.url)) &&
           !vector_contains(state.seen, link) &&
           state.queue.size() < 128;
}

void queue_website_link(
    const WebsiteLinkMeta& meta,
    const RepoPackage& package,
    const WebsiteQueueItem& page,
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress) {
    const std::string& link = meta.url;
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
        false,
        meta.mtime,
        meta.stale_penalty);
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
    const std::vector<WebsiteLinkMeta>& links,
    const RepoPackage& package,
    const WebsiteQueueItem& page,
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress,
    const std::string& arch) {
    std::vector<WebsiteLinkMeta> follow_metas;
    for (const WebsiteLinkMeta& meta : links) {
        if (is_allowed_appimage_candidate(meta.url, package, state.allowed_hosts)) {
            WebsiteLinkMeta candidate = meta;
            candidate.url = resolved_appimage_candidate(meta.url, package, arch);
            record_appimage_candidate(std::move(candidate), page, state, progress);
            continue;
        }
        if (should_queue_website_link(meta.url, package, page, state)) {
            follow_metas.push_back(meta);
        }
    }

    const std::vector<WebsiteLinkMeta> to_queue =
        select_listing_follow_metas_for_enqueue(
            follow_metas,
            kWebsiteListingFollowNonStaleMax,
            kWebsiteListingFollowStaleMax);
    for (const WebsiteLinkMeta& meta : to_queue) {
        // Re-check should_queue: queue growth / seen set may have changed while
        // recording AppImages; also prune may keep URLs that later fail host rules
        // only if should_queue already passed — still re-check depth/seen/queue cap.
        if (should_queue_website_link(meta.url, package, page, state)) {
            queue_website_link(meta, package, page, state, progress);
        }
    }
}

std::size_t non_stale_appimage_candidate_count(
    const std::vector<WebsiteLinkMeta>& candidates,
    const std::string& arch) {
    const std::string effective = arch.empty() ? current_arch() : normalize_arch(arch);
    std::size_t count = 0;
    for (const WebsiteLinkMeta& candidate : candidates) {
        if (candidate.stale_penalty != 0 ||
            !is_appimage_download_url(candidate.url) ||
            appimage_asset_score(basename_from_url(candidate.url), effective) < 0) {
            continue;
        }
        ++count;
    }
    return count;
}

std::optional<std::int64_t> best_non_stale_candidate_mtime(
    const std::vector<WebsiteLinkMeta>& candidates,
    const std::string& arch) {
    const std::string effective = arch.empty() ? current_arch() : normalize_arch(arch);
    std::optional<std::int64_t> best;
    for (const WebsiteLinkMeta& candidate : candidates) {
        if (candidate.stale_penalty != 0 ||
            !candidate.mtime.has_value() ||
            !is_appimage_download_url(candidate.url) ||
            appimage_asset_score(basename_from_url(candidate.url), effective) < 0) {
            continue;
        }
        if (!best.has_value() || *candidate.mtime > *best) {
            best = candidate.mtime;
        }
    }
    return best;
}

bool queue_has_newer_non_stale_mtime(
    const WebsiteSearchState& state,
    const std::optional<std::int64_t>& best_non_stale_mtime) {
    for (const WebsiteQueueItem& item : state.queue) {
        if (vector_contains(state.seen, item.url) ||
            item.stale_penalty != 0 ||
            !item.mtime.has_value()) {
            continue;
        }
        if (!best_non_stale_mtime.has_value() || *item.mtime > *best_non_stale_mtime) {
            return true;
        }
    }
    return false;
}

bool website_search_should_stop(
    const WebsiteSearchState& state,
    const std::string& arch,
    std::size_t pages_checked) {
    if (pages_checked >= kWebsiteMaxPages) {
        return true;
    }
    const std::size_t non_stale = non_stale_appimage_candidate_count(state.appimage_candidates, arch);
    if (non_stale == 0) {
        return false;
    }
    if (non_stale >= kWebsiteCandidateSoftCap) {
        return true;
    }
    return !queue_has_newer_non_stale_mtime(
        state, best_non_stale_candidate_mtime(state.appimage_candidates, arch));
}

void fill_missing_candidate_mtimes_from_head(
    std::vector<WebsiteLinkMeta>& candidates,
    const std::string& arch) {
    std::vector<std::size_t> missing;
    const std::string effective = arch.empty() ? current_arch() : normalize_arch(arch);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].mtime.has_value() ||
            !is_appimage_download_url(candidates[i].url) ||
            appimage_asset_score(basename_from_url(candidates[i].url), effective) < 0) {
            continue;
        }
        missing.push_back(i);
    }
    std::sort(missing.begin(), missing.end(), [&](std::size_t a, std::size_t b) {
        return website_candidate_better(candidates[a], candidates[b], arch);
    });
    const std::size_t limit = std::min(kWebsiteHeadFillMax, missing.size());
    for (std::size_t n = 0; n < limit; ++n) {
        const std::optional<std::int64_t> mtime =
            probe_url_last_modified_mtime(candidates[missing[n]].url);
        if (mtime.has_value()) {
            candidates[missing[n]].mtime = mtime;
        }
    }
}

std::string selected_website_candidate(
    std::vector<WebsiteLinkMeta>& appimage_candidates,
    const std::string& arch,
    WebsiteSearchProgress& progress) {
    fill_missing_candidate_mtimes_from_head(appimage_candidates, arch);
    const std::string best = best_website_appimage_url(appimage_candidates, arch);
    if (!best.empty()) {
        progress.selected(best);
    }
    return best;
}

}  // namespace

// Pipeline stage: a single page fetch with its metadata and result slot
struct FetchTask {
    WebsiteQueueItem page;
    std::optional<std::vector<WebsiteLinkMeta>> outcome;
    std::future<std::optional<std::vector<WebsiteLinkMeta>>> future;
    bool completed = false;
};

void process_fetch_results(
    std::vector<FetchTask>& completed_tasks,
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress,
    const RepoPackage& package,
    const std::string& arch,
    std::size_t& pages_checked) {
    for (auto& task : completed_tasks) {
        ++pages_checked;
        const WebsiteQueueItem& page = task.page;

        if (!task.outcome.has_value()) {
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
            continue;
        }

        collect_appimage_candidates(
            *task.outcome, package, page, state, progress, arch);
    }
    completed_tasks.clear();
}

std::string resolve_website_appimage_download(const RepoPackage& package, const std::string& arch) {
    WebsiteSearchProgress progress(package.id);
    WebsiteSearchState state = initial_website_search_state(package, progress);

    // Pipeline-based website crawling: maintain up to kMaxWebsiteCrawlConcurrency
    // in-flight requests. As each completes, process its result immediately
    // and dispatch the next queued URL. This eliminates batch-level waiting
    // where one slow URL blocks all others in the same batch.
    std::size_t pages_checked = 0;
    std::vector<FetchTask> in_flight;
    std::vector<FetchTask> completed_tasks;

    auto dispatch_next = [&]() -> bool {
        if (in_flight.size() >= kMaxWebsiteCrawlConcurrency) {
            return false;
        }
        if (pages_checked + in_flight.size() >= kWebsiteMaxPages) {
            return false;
        }
        if (website_search_should_stop(state, arch, pages_checked + in_flight.size())) {
            return false;
        }

        const std::optional<std::size_t> index = next_website_queue_index(state);
        if (!index.has_value()) {
            return false;
        }
        WebsiteQueueItem page = state.queue[*index];
        state.seen.push_back(page.url);
        progress.checked(page.url);

        FetchTask task;
        task.page = page;
        task.future = std::async(std::launch::async,
            [page]() -> std::optional<std::vector<WebsiteLinkMeta>> {
                return fetch_website_links(page.url, page.speculative);
            });
        in_flight.push_back(std::move(task));
        return true;
    };

    // Initial dispatch: fill the pipeline
    while (dispatch_next()) {
    }

    // Process results and refill the pipeline
    while (!in_flight.empty()) {
        // Check for completed tasks
        auto it = in_flight.begin();
        while (it != in_flight.end()) {
            if (it->future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                it->outcome = it->future.get();
                completed_tasks.push_back(std::move(*it));
                it = in_flight.erase(it);
            } else {
                ++it;
            }
        }

        // Process completed results
        if (!completed_tasks.empty()) {
            process_fetch_results(completed_tasks, state, progress, package, arch, pages_checked);
        }

        // Refill the pipeline
        while (dispatch_next()) {
        }

        // Check stop conditions
        if (website_search_should_stop(state, arch, pages_checked)) {
            break;
        }
        if (next_website_queue_index(state) == std::nullopt &&
            !state.appimage_candidates.empty()) {
            break;
        }

        // If still in-flight but nothing completed, yield briefly
        if (!in_flight.empty() && completed_tasks.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Drain any remaining completed tasks
    for (auto& task : in_flight) {
        task.outcome = task.future.get();
        completed_tasks.push_back(std::move(task));
    }
    if (!completed_tasks.empty()) {
        process_fetch_results(completed_tasks, state, progress, package, arch, pages_checked);
    }

    const std::string best =
        selected_website_candidate(state.appimage_candidates, arch, progress);
    if (!best.empty()) {
        return best;
    }
    throw std::runtime_error(tr("no AppImage download link found on website pages for ") + package.id);
}
