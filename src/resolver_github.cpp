#include "yai.hpp"

#include <chrono>
#include <cstdlib>
#include <future>
#include <mutex>
#include <unordered_map>

// GitHub release resolution, blocklists, and mirror-aware download strategy.

std::string trimmed_env_url(const char* env_name, const std::string& default_value) {
    const char* env = std::getenv(env_name);
    std::string base =
        env == nullptr || std::string(env).empty() ? default_value : std::string(env);
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

std::string github_api_base() {
    return trimmed_env_url("YAI_GITHUB_API_BASE", "https://api.github.com");
}

// The AppImageHub catalog lives in a fixed upstream repository. Both bases are
// overridable so the test suite can point them at local files instead of
// reaching the real network.
std::string appimage_github_api_base() {
    return trimmed_env_url("YAI_APPIMAGE_GITHUB_API_BASE", kAppImageGithubRepoApiBase);
}

std::string appimage_github_raw_base() {
    return trimmed_env_url("YAI_APPIMAGE_GITHUB_RAW_BASE", kAppImageGithubRawBase);
}

bool github_repo_matches_local_blocklist(const std::string& repo_target) {
    const fs::path file = github_blocklist_path();
    if (!fs::exists(file)) {
        return false;
    }

    std::ifstream in(file);
    std::string line;
    const std::string target = to_lower(repo_target);
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (to_lower(line) == target) {
            return true;
        }
    }
    return false;
}

bool github_repo_matches_builtin_blocklist(const std::string& repo_target) {
    const std::string lower = to_lower(repo_target);
    const std::vector<std::string> blocked_terms = {
        "crack",
        "keygen",
        "warez",
        "piracy",
        "pirated",
        "ransomware",
        "malware",
        "phishing",
        "botnet",
    };
    for (const std::string& term : blocked_terms) {
        if (lower.find(term) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void enforce_github_release_policy(const std::string& owner, const std::string& repo) {
    const std::string repo_target = owner + "/" + repo;
    if (github_repo_matches_local_blocklist(repo_target) ||
        github_repo_matches_builtin_blocklist(repo_target)) {
        throw std::runtime_error(
            tr("451 Unavailable For Legal Reasons: GitHub repository is blocked by yai policy: ") +
            repo_target);
    }
}

namespace {

// In-process + on-disk caches for GitHub /releases/latest responses.
//
// Scheme A (in-process): the same owner/repo is fetched at most once per yai
// process, even when resolve_github_latest runs concurrently across many arches
// or packages. A shared_future lets concurrent waiters block on a single fetch.
//
// Scheme B (on-disk TTL): a recent response is reused across separate yai runs
// so repeated `repo resolve` / `upgrade` invocations spend ~0 GitHub API
// tokens. It is disabled whenever YAI_GITHUB_API_BASE is overridden (tests and
// GitHub Enterprise mocks change their responses between runs) or the TTL is 0.

std::mutex g_github_latest_mutex;
std::unordered_map<std::string, std::shared_future<std::string>> g_github_latest_cache;

constexpr std::chrono::seconds kDefaultGithubLatestCacheTtl(3600);

std::chrono::seconds github_latest_cache_ttl() {
    const char* env = std::getenv("YAI_GITHUB_CACHE_TTL_SECONDS");
    if (env == nullptr || std::string(env).empty()) {
        return kDefaultGithubLatestCacheTtl;
    }
    const long value = std::strtol(env, nullptr, 10);
    if (value <= 0) {
        return std::chrono::seconds(0);
    }
    return std::chrono::seconds(value);
}

// Only the real GitHub API gets an on-disk cache: tests and enterprise setups
// override YAI_GITHUB_API_BASE with a backend whose contents change between
// runs, so persisting those responses would serve stale data.
bool github_disk_cache_enabled(const std::string& api_base) {
    return api_base == "https://api.github.com" && github_latest_cache_ttl() > std::chrono::seconds(0);
}

fs::path github_latest_cache_dir() {
    return config_dir_path() / "cache" / "github-releases-latest";
}

std::string github_latest_cache_key(const std::string& api_base, const std::string& owner, const std::string& repo) {
    return api_base + "\n" + owner + "/" + repo;
}

fs::path github_latest_cache_file(const std::string& owner, const std::string& repo) {
    return github_latest_cache_dir() / (owner + "+" + repo + ".json");
}

bool github_latest_cache_fresh(const fs::path& file, std::chrono::seconds ttl) {
    std::error_code ec;
    const auto mtime = fs::last_write_time(file, ec);
    if (ec) {
        return false;
    }
    const auto age = std::chrono::duration_cast<std::chrono::seconds>(
        fs::file_time_type::clock::now() - mtime);
    return age <= ttl;
}

void write_github_latest_cache_file(const fs::path& file, const std::string& json) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        yai_debug_stream() << tr("yai: cannot create GitHub cache dir: ") << ec.message() << "\n";
        return;
    }
    try {
        write_text_file_atomic(file, json);
    } catch (const std::exception& ex) {
        yai_debug_stream() << tr("yai: cannot write GitHub cache: ") << ex.what() << "\n";
    }
}

// Fetches the /releases/latest JSON, consulting the on-disk TTL cache first and
// falling back to a stale on-disk response when the network is unreachable (so a
// rate-limited `repo resolve` still succeeds). When force_refresh is set (repo
// resolve --overwrite) the on-disk cache is bypassed entirely: a fresh response
// is fetched (and written back) and a network failure is surfaced instead of
// masking it with stale data. Throws only when no usable response exists.
std::string fetch_github_releases_latest_json_impl(
    const std::string& api_base, const std::string& owner, const std::string& repo,
    bool force_refresh) {
    const std::string api_url = api_base + "/repos/" + owner + "/" + repo + "/releases/latest";
    const bool disk_enabled = github_disk_cache_enabled(api_base);
    const std::chrono::seconds ttl = github_latest_cache_ttl();
    const fs::path cache_file = github_latest_cache_file(owner, repo);

    // force_refresh skips reading the on-disk TTL cache (including the stale
    // fallback) but still writes the fresh response and shares the in-process
    // cache with concurrent arches within this run.
    if (!force_refresh && disk_enabled && fs::exists(cache_file)) {
        if (github_latest_cache_fresh(cache_file, ttl)) {
            try {
                const std::string cached = read_text_file(cache_file);
                if (!cached.empty()) {
                    yai_debug_stream() << tr("yai: github release cache hit (disk): ")
                              << owner << "/" << repo << "\n";
                    return cached;
                }
            } catch (const std::exception& ex) {
                yai_debug_stream() << tr("yai: cannot read GitHub cache, refetching: ")
                          << ex.what() << "\n";
            }
        }
        // Stale on-disk cache present: try the network, but fall back to the
        // stale copy if the API is unreachable so resolution still succeeds.
        try {
            const std::string json = fetch_text(api_url);
            write_github_latest_cache_file(cache_file, json);
            return json;
        } catch (const std::exception& ex) {
            yai_debug_stream() << tr("yai: github fetch failed; using stale cache: ")
                      << ex.what() << "\n";
            try {
                const std::string cached = read_text_file(cache_file);
                if (!cached.empty()) {
                    return cached;
                }
            } catch (...) {
            }
            throw;
        }
    }

    const std::string json = fetch_text(api_url);
    if (disk_enabled) {
        write_github_latest_cache_file(cache_file, json);
    }
    return json;
}

// In-process deduplicating wrapper. Concurrent callers for the same owner/repo
// share one fetch; a failed fetch is evicted so later callers retry instead of
// permanently caching the error.
std::string fetch_github_releases_latest_json(
    const std::string& api_base, const std::string& owner, const std::string& repo,
    bool force_refresh) {
    const std::string key = github_latest_cache_key(api_base, owner, repo);
    std::shared_future<std::string> future;
    std::shared_ptr<std::promise<std::string>> promise;

    {
        std::lock_guard<std::mutex> lock(g_github_latest_mutex);
        auto it = g_github_latest_cache.find(key);
        if (it != g_github_latest_cache.end()) {
            future = it->second;
        } else {
            promise = std::make_shared<std::promise<std::string>>();
            future = promise->get_future().share();
            g_github_latest_cache.emplace(key, future);
        }
    }

    if (promise == nullptr) {
        return future.get();
    }

    try {
        const std::string json = fetch_github_releases_latest_json_impl(api_base, owner, repo, force_refresh);
        promise->set_value(json);
        return json;
    } catch (...) {
        promise->set_exception(std::current_exception());
        {
            std::lock_guard<std::mutex> lock(g_github_latest_mutex);
            g_github_latest_cache.erase(key);
        }
        throw;
    }
}

}  // namespace

GitHubRelease resolve_github_latest(
    const std::string& repo_target,
    const std::string& asset_pattern,
    const std::string& arch,
    bool force_refresh) {
    const std::size_t slash = repo_target.find('/');
    const std::string owner = repo_target.substr(0, slash);
    const std::string repo = repo_target.substr(slash + 1);
    enforce_github_release_policy(owner, repo);
    std::string json;
    try {
        json = fetch_github_releases_latest_json(github_api_base(), owner, repo, force_refresh);
    } catch (const std::exception& ex) {
        const std::string& msg = ex.what();
        const bool is_rate_limit =
            msg.find("403") != std::string::npos &&
            (msg.find("rate limit") != std::string::npos ||
             msg.find("API rate limit") != std::string::npos ||
             msg.find("secondary rate limit") != std::string::npos);
        const bool is_forbidden =
            msg.find("403") != std::string::npos &&
            msg.find("rate limit") == std::string::npos &&
            msg.find("API rate limit") == std::string::npos;
        if (is_rate_limit) {
            const char* token = std::getenv("YAI_GITHUB_TOKEN");
            if (token != nullptr && *token != '\0') {
                throw std::runtime_error(
                    tr("GitHub API rate limit exceeded for ") + repo_target +
                    tr(". Your YAI_GITHUB_TOKEN may be invalid or expired. ") +
                    tr("Please check your token at https://github.com/settings/tokens"));
            }
            throw std::runtime_error(
                tr("GitHub API rate limit exceeded for ") + repo_target +
                tr(". Set YAI_GITHUB_TOKEN environment variable to a GitHub Personal Access Token ") +
                tr("(https://github.com/settings/tokens) to increase the limit from 60 to 5000 requests/hour, ") +
                tr("or wait for the rate limit window to reset."));
        }
        if (is_forbidden) {
            throw std::runtime_error(
                tr("GitHub API returned 403 Forbidden for ") + repo_target +
                tr(". The repository may be private, restricted, or blocked. ") +
                tr("Use YAI_GITHUB_TOKEN with appropriate permissions to access private repositories."));
        }
        throw;
    }
    const std::string tag = json_find_string(json, "tag_name").value_or("latest");
    const std::vector<std::string> urls = json_find_all_strings(json, "browser_download_url");
    const std::string effective_arch = arch.empty() ? current_arch() : normalize_arch(arch);

    std::optional<std::regex> asset_regex;
    if (!asset_pattern.empty()) {
        try {
            asset_regex.emplace(asset_pattern, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error& ex) {
            throw std::runtime_error(tr("invalid asset_pattern for ") + repo_target + tr(": ") + ex.what());
        }
    }

    int best_score = -1;
    GitHubReleaseAsset best;
    for (const std::string& url : urls) {
        const std::string name = basename_from_url(url);
        if (asset_regex.has_value() && !std::regex_search(name, *asset_regex)) {
            continue;
        }
        const int score = appimage_asset_score(name, effective_arch);
        if (score > best_score) {
            best_score = score;
            best = GitHubReleaseAsset{name, url};
        }
    }

    if (best_score < 0) {
        throw std::runtime_error(tr("no AppImage asset matched architecture ") + effective_arch);
    }

    return GitHubRelease{owner, repo, tag, best};
}

std::string mirror_url_for(const std::string& mirror_template, const ResolvedSource& source) {
    std::string out = mirror_template;
    std::string raw_url_noscheme = source.source_url;
    const std::size_t scheme = raw_url_noscheme.find("://");
    if (scheme != std::string::npos) {
        raw_url_noscheme.erase(0, scheme + 3);
    }
    out = replace_all(out, "{raw_url}", source.source_url);
    out = replace_all(out, "{raw_url_noscheme}", raw_url_noscheme);
    out = replace_all(out, "{url}", url_encode(source.source_url));
    out = replace_all(out, "{owner}", source.github_owner);
    out = replace_all(out, "{repo}", source.github_repo);
    out = replace_all(out, "{tag}", source.version);
    out = replace_all(out, "{asset}", source.github_asset.empty() ? basename_from_url(source.source_url) : source.github_asset);
    return out;
}

std::string download_with_strategy(
    ResolvedSource& source,
    const InstallOptions& options,
    const fs::path& target) {
    // Mirror strategy is a transport fallback list. The original source_url
    // remains the upstream identity written to metadata even when the actual
    // bytes came through a proxy. Validators from the successful transfer are
    // written onto source for metadata persistence.
    std::vector<std::string> candidates;
    if (options.download_strategy == "direct") {
        candidates.push_back(source.source_url);
    } else {
        const std::string mirror_url = mirror_url_for(options.mirror_template, source);
        if (options.download_strategy == "mirror_first") {
            candidates.push_back(mirror_url);
            candidates.push_back(source.source_url);
        } else {
            candidates.push_back(source.source_url);
            candidates.push_back(mirror_url);
        }
    }

    std::string last_error;
    for (const std::string& candidate : candidates) {
        try {
            const HttpValidators validators = download_file(candidate, target, options.downloader);
            source.http_etag = validators.etag;
            source.http_last_modified = validators.last_modified;
            source.http_content_length = validators.content_length;
            return candidate;
        } catch (const std::exception& ex) {
            last_error = ex.what();
            yai_debug_stream() << tr("yai: download failed from ")
                      << candidate << tr(": ") << last_error << "\n";
        }
    }
    throw std::runtime_error(tr("all download candidates failed: ") + last_error);
}
