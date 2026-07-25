#include "yai.hpp"

// GitHub release resolution, blocklists, and mirror-aware download strategy.

std::string github_api_base() {
    const char* env = std::getenv("YAI_GITHUB_API_BASE");
    std::string base = env == nullptr || std::string(env).empty()
        ? "https://api.github.com"
        : std::string(env);
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
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
    // GitHub resolution stops at choosing one release asset and recording where
    // it came from. It does not download bytes, apply mirrors, or decide install
    // side effects.
    const std::size_t slash = repo_target.find('/');
    const std::string owner = repo_target.substr(0, slash);
    const std::string repo = repo_target.substr(slash + 1);
    enforce_github_release_policy(owner, repo);
    const std::string api_url = github_api_base() + "/repos/" + owner + "/" + repo + "/releases/latest";
    const std::string json = fetch_text(api_url);
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
    const ResolvedSource& source,
    const InstallOptions& options,
    const fs::path& target) {
    // Mirror strategy is a transport fallback list. The original source_url
    // remains the upstream identity written to metadata even when the actual
    // bytes came through a proxy.
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
            download_file(candidate, target, options.downloader);
            return candidate;
        } catch (const std::exception& ex) {
            last_error = ex.what();
            std::cerr << tr("yai: download failed from ")
                      << candidate << ": " << last_error << "\n";
        }
    }
    throw std::runtime_error(tr("all download candidates failed: ") + last_error);
}
