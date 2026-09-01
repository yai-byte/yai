#include "yai.hpp"

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

GitHubRelease resolve_github_latest(
    const std::string& repo_target,
    const std::string& asset_pattern,
    const std::string& arch) {
    const std::size_t slash = repo_target.find('/');
    const std::string owner = repo_target.substr(0, slash);
    const std::string repo = repo_target.substr(slash + 1);
    enforce_github_release_policy(owner, repo);
    const std::string api_url = github_api_base() + "/repos/" + owner + "/" + repo + "/releases/latest";
    std::string json;
    try {
        json = fetch_text(api_url);
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
            std::cerr << tr("yai: download failed from ")
                      << candidate << tr(": ") << last_error << "\n";
        }
    }
    throw std::runtime_error(tr("all download candidates failed: ") + last_error);
}
