#include "yai.hpp"

#include <cstring>

namespace {

// Simple base64 decoder for GitHub API content field
std::string base64_decode(const std::string& encoded) {
    static const std::string table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    decoded.reserve(encoded.size() * 3 / 4);

    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        auto pos = table.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return decoded;
}

// Extract YAML frontmatter (between --- markers) from markdown content
std::string extract_yaml_frontmatter(const std::string& content) {
    const std::string delimiter = "---";
    std::size_t start = content.find(delimiter);
    if (start == std::string::npos) return "";
    start += delimiter.size();
    std::size_t end = content.find(delimiter, start);
    if (end == std::string::npos) return "";
    return content.substr(start, end - start);
}

// Parse YAML frontmatter into key-value pairs (supports simple nesting via indentation)
std::map<std::string, std::string> parse_yaml_frontmatter(const std::string& yaml) {
    std::map<std::string, std::string> result;
    std::istringstream stream(yaml);
    std::string line;
    std::string current_section;
    std::string current_subkey;
    int list_index = 0;

    while (std::getline(stream, line)) {
        // Remove trailing carriage return
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip empty lines
        if (trim(line).empty()) continue;

        // List item (starts with "- ")
        if (trim(line).rfind("- ", 0) == 0) {
            line = trim(line).substr(2);
            // Parse "key: value" within list item
            std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = trim(line.substr(0, colon));
                std::string value = trim(line.substr(colon + 1));

                // Generate a subkey for this list item
                if (!current_section.empty()) {
                    current_subkey = std::to_string(list_index++);
                }

                if (!current_section.empty() && !current_subkey.empty()) {
                    result[current_section + "." + current_subkey + "." + key] = value;
                }
            }
            continue;
        }

        // Key: value or key: (start of nested section)
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));

        // If line starts with spaces/tabs, it's a sub-key
        bool is_indented = (line.find_first_not_of(" \t") > 0);

        if (value.empty()) {
            // Start of a nested section
            current_section = key;
            current_subkey.clear();
            list_index = 0;
            if (!is_indented) {
                result[key] = "";
            }
        } else {
            // Simple key: value
            // Remove quotes if present
            if (value.size() >= 2 &&
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }

            if (is_indented && !current_section.empty()) {
                // Sub-key within a section (or within a list item)
                if (!current_subkey.empty()) {
                    result[current_section + "." + current_subkey + "." + key] = value;
                } else {
                    result[current_section + "." + key] = value;
                }
            } else {
                current_section = key;
                current_subkey.clear();
                list_index = 0;
                result[key] = value;
            }
        }
    }
    return result;
}

// Find a value in parsed YAML by checking common key patterns
std::string find_yaml_value(
    const std::map<std::string, std::string>& yaml,
    const std::string& key) {
    auto it = yaml.find(key);
    if (it != yaml.end()) return it->second;
    return "";
}

// Extract owner/repo from a GitHub URL or path
std::string extract_github_repo(const std::string& url) {
    std::string value = trim(url);
    std::string orig = value;

    // Handle full URLs (most common, check first)
    if (value.find("github.com/") != std::string::npos) {
        std::size_t pos = value.find("github.com/");
        std::string path = value.substr(pos + 11);
        std::size_t qf = path.find_first_of("?#");
        if (qf != std::string::npos) path = path.substr(0, qf);
        if (path.find("/releases") != std::string::npos) {
            path = path.substr(0, path.find("/releases"));
        }
        while (!path.empty() && path.back() == '/') {
            path.pop_back();
        }
        std::size_t slash = path.find('/');
        if (slash != std::string::npos) {
            std::string owner = path.substr(0, slash);
            std::string repo = path.substr(slash + 1);
            if (repo.size() > 4 &&
                repo.compare(repo.size() - 4, 4, ".git") == 0) {
                repo = repo.substr(0, repo.size() - 4);
            }
            if (!owner.empty() && !repo.empty()) {
                return owner + "/" + repo;
            }
        }
    }

    // Handle "owner/repo" format directly
    if (value.find('/') != std::string::npos &&
        value.find(' ') == std::string::npos) {
        std::size_t slash = value.find('/');
        std::string owner = value.substr(0, slash);
        std::string repo_part = value.substr(slash + 1);
        if (repo_part.find("/releases") != std::string::npos) {
            repo_part = repo_part.substr(0, repo_part.find("/releases"));
        }
        if (repo_part.size() > 4 &&
            repo_part.compare(repo_part.size() - 4, 4, ".git") == 0) {
            repo_part = repo_part.substr(0, repo_part.size() - 4);
        }
        while (!repo_part.empty() && repo_part.back() == '/') {
            repo_part.pop_back();
        }
        if (!owner.empty() && !repo_part.empty()) {
            return owner + "/" + repo_part;
        }
    }

    return "";
}

}  // namespace

bool looks_like_direct_download_url(const std::string& url) {
    if (url.empty()) return false;
    std::string lower = to_lower(url);
    // Accept http/https scheme
    const bool has_http =
        lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0;
    // Accept file:// scheme for local test fixtures
    const bool has_file = lower.rfind("file://", 0) == 0;
    if (!has_http && !has_file) return false;
    // Should contain a file extension that looks like an AppImage or archive
    if (lower.find(".appimage") != std::string::npos) return true;
    // GitHub release download URLs
    if (lower.find("/releases/download/") != std::string::npos) return true;
    return false;
}

bool looks_like_github_repo_url(const std::string& url) {
    if (url.empty()) return false;
    std::string lower = to_lower(url);
    // Match https://github.com/owner/repo (but not releases/download paths)
    if (lower.find("github.com/") == std::string::npos) return false;
    if (lower.find("/releases/download/") != std::string::npos) return false;
    if (lower.find("/releases") != std::string::npos) return false;
    return true;
}

bool looks_like_gitlab_url(const std::string& url) {
    if (url.empty()) return false;
    std::string lower = to_lower(url);
    return lower.find("gitlab.com/") != std::string::npos ||
           lower.find("gitlab.") != std::string::npos;
}

std::string extract_gitlab_project(const std::string& url) {
    std::string value = trim(url);

    // Handle https://gitlab.com/group/project/-/... patterns
    if (value.find("gitlab.com/") != std::string::npos) {
        std::size_t pos = value.find("gitlab.com/");
        std::string path = value.substr(pos + 11);
        // Stop at /-/ (GitLab special prefix for pipelines, releases, etc.)
        std::size_t sep = path.find("/-/");
        if (sep != std::string::npos) {
            path = path.substr(0, sep);
        }
        // Stop at query or fragment
        std::size_t qf = path.find_first_of("?#");
        if (qf != std::string::npos) path = path.substr(0, qf);
        // Remove trailing slash
        while (!path.empty() && path.back() == '/') {
            path.pop_back();
        }
        return path;
    }

    // Handle self-hosted GitLab instances (e.g. gitlab.gnome.org, gitlab.inkscape.org)
    if (value.find("gitlab.") != std::string::npos) {
        std::size_t pos = value.find("://");
        if (pos == std::string::npos) pos = 0;
        // Find the path after the host
        std::size_t host_end = value.find('/', pos + 3);
        if (host_end != std::string::npos) {
            std::string path = value.substr(host_end + 1);
            // Stop at /-/ (GitLab special prefix)
            std::size_t sep = path.find("/-/");
            if (sep != std::string::npos) {
                path = path.substr(0, sep);
            }
            std::size_t qf = path.find_first_of("?#");
            if (qf != std::string::npos) path = path.substr(0, qf);
            while (!path.empty() && path.back() == '/') {
                path.pop_back();
            }
            // Must contain at least one slash (group/project)
            if (path.find('/') != std::string::npos) {
                return path;
            }
        }
    }

    return "";
}



// Fetches the markdown content for an apps/<name>.md file.
// Tries the GitHub API (base64 content) first, then falls back to
// raw.githubusercontent.com. Returns nullopt if both paths fail.
std::optional<std::string> fetch_apps_markdown(const std::string& name, int timeout_ms) {
    try {
        const std::string url = appimage_github_api_base() +
            "/contents/apps/" + url_encode(name) + ".md";
        const std::string response = fetch_text(url, timeout_ms);
        const std::string content_b64 = json_find_string(response, "content").value_or("");
        if (!content_b64.empty()) {
            std::string clean_b64;
            for (char c : content_b64) {
                if (c != '\n' && c != '\r') clean_b64 += c;
            }
            return base64_decode(clean_b64);
        }
    } catch (const std::exception&) {
        // Fall through to raw.githubusercontent.com
    }
    try {
        const std::string raw_url = appimage_github_raw_base() +
            "/apps/" + url_encode(name) + ".md";
        std::string raw_content = fetch_text(raw_url, timeout_ms);
        if (!raw_content.empty()) return raw_content;
    } catch (const std::exception&) {
        // Both paths failed
    }
    return std::nullopt;
}

// Fills an AppImageAppsEntry from parsed YAML frontmatter. Scans link entries
// (type/url pairs) for GitHub, Download, and Web/Homepage links in a single
// pass, then applies pattern-based fallback searches for any field not found.
void fill_apps_entry_from_yaml(AppImageAppsEntry& entry, const std::map<std::string, std::string>& parsed) {
    bool found_github = false;
    bool found_download = false;

    // Primary: single pass over all link type/url pairs
    for (const auto& [k, v] : parsed) {
        bool is_link_type = false;
        std::string prefix;
        if (k == "links.type" ||
            (k.size() > 11 && k.substr(0, 6) == "links." &&
             k.substr(k.size() - 5) == ".type")) {
            is_link_type = true;
            prefix = k.substr(0, k.rfind(".type"));
        }
        if (!is_link_type) continue;

        const std::string link_type = to_lower(v);
        const std::string link_url = find_yaml_value(parsed, prefix + ".url");

        if ((link_type == "github" || link_type == "repository") &&
            !link_url.empty() && !found_github) {
            entry.github_repo = extract_github_repo(link_url);
            found_github = true;
        } else if (link_type == "download" &&
                   !link_url.empty() && !found_download) {
            entry.direct_url = link_url;
            found_download = true;
        } else if ((link_type == "web" || link_type == "homepage") &&
                   !link_url.empty() && entry.homepage.empty()) {
            entry.homepage = link_url;
        }
    }

    // Fallback: search for GitHub URLs in any link field
    if (entry.github_repo.empty()) {
        for (const auto& [k, v] : parsed) {
            if (k.find("url") != std::string::npos &&
                v.find("github.com/") != std::string::npos) {
                entry.github_repo = extract_github_repo(v);
                break;
            }
        }
    }

    // Fallback: search for direct download URLs in any link field
    if (entry.direct_url.empty()) {
        for (const auto& [k, v] : parsed) {
            if (k.find("url") != std::string::npos &&
                looks_like_direct_download_url(v)) {
                entry.direct_url = v;
                break;
            }
        }
    }

    // Fallback: search for homepage via type/url pattern
    if (entry.homepage.empty()) {
        for (const auto& [k, v] : parsed) {
            if (k.find("type") != std::string::npos &&
                (to_lower(v) == "web" || to_lower(v) == "homepage")) {
                const std::string prefix = k.substr(0, k.rfind(".type"));
                entry.homepage = find_yaml_value(parsed, prefix + ".url");
                break;
            }
        }
    }

    // Scalar fields
    entry.description = find_yaml_value(parsed, "description");
    entry.license = find_yaml_value(parsed, "license");
    entry.arch = find_yaml_value(parsed, "desktop.X-AppImage-Arch");
    entry.version = find_yaml_value(parsed, "desktop.X-AppImage-Version");
}

// Parse a single apps/<Name>.md file from GitHub
std::optional<AppImageAppsEntry> parse_appimage_apps_entry(
    const std::string& name,
    int timeout_ms) {
    try {
        const auto content = fetch_apps_markdown(name, timeout_ms);
        if (!content.has_value()) return std::nullopt;

        const std::string yaml = extract_yaml_frontmatter(*content);
        if (yaml.empty()) return std::nullopt;

        const auto parsed = parse_yaml_frontmatter(yaml);
        AppImageAppsEntry entry;
        entry.name = name;
        fill_apps_entry_from_yaml(entry, parsed);
        return entry;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Parse a single data/<Name> file from raw.githubusercontent.com
std::optional<AppImageDataEntry> parse_appimage_data_entry(
    const std::string& name,
    int timeout_ms) {
    std::string content;
    try {
        // First try raw.githubusercontent.com
        std::string url = appimage_github_raw_base() +
            "/data/" + url_encode(name);
        content = fetch_text(url, timeout_ms);
    } catch (const std::exception&) {
        // raw.githubusercontent.com may be unreachable. Fall back to
        // the GitHub API which returns base64-encoded content.
        try {
            std::string api_url =
                appimage_github_api_base() + "/contents/data/" + url_encode(name);
            std::string api_json = fetch_text(api_url, timeout_ms);
            auto json_content = json_find_string(api_json, "content");
            if (json_content.has_value()) {
                content = base64_decode(*json_content);
                // Remove trailing newline if present in the encoding
                if (!content.empty() && content.back() == '\n') {
                    content.pop_back();
                }
            } else {
                return std::nullopt;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    try {
        AppImageDataEntry entry;
        entry.name = name;

        std::istringstream stream(content);
        std::string line;
        std::vector<std::string> comment_urls;
        std::string primary_url;

        while (std::getline(stream, line)) {
            // Remove trailing carriage return
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Skip empty lines
            std::string trimmed = trim(line);
            if (trimmed.empty()) continue;

            // Comment line
            if (trimmed.front() == '#') {
                // Extract URL from comment (remove # and whitespace)
                std::string comment_content = trim(trimmed.substr(1));
                if (!comment_content.empty() &&
                    (comment_content.find("http://") == 0 ||
                     comment_content.find("https://") == 0 ||
                     comment_content.find("file://") == 0)) {
                    comment_urls.push_back(comment_content);
                }
                continue;
            }

            // Non-comment line should be a URL
            if (trimmed.find("http://") == 0 ||
                trimmed.find("https://") == 0 ||
                trimmed.find("file://") == 0) {
                if (primary_url.empty()) {
                    primary_url = trimmed;
                }
            }
        }

        // Analyze the URL(s)
        if (!primary_url.empty()) {
            if (looks_like_direct_download_url(primary_url)) {
                entry.direct_url = primary_url;
                // Also extract GitLab project for release page fallback
                if (looks_like_gitlab_url(primary_url)) {
                    entry.gitlab_project = extract_gitlab_project(primary_url);
                }
            } else if (looks_like_github_repo_url(primary_url)) {
                entry.github_repo = extract_github_repo(primary_url);
            } else if (looks_like_gitlab_url(primary_url)) {
                entry.gitlab_project = extract_gitlab_project(primary_url);
                // Treat GitLab URLs with .AppImage as direct download
                if (to_lower(primary_url).find(".appimage") != std::string::npos) {
                    entry.direct_url = primary_url;
                }
            }
        }

        // If primary URL didn't give us what we need, check comments
        if (entry.github_repo.empty()) {
            for (const auto& comment_url : comment_urls) {
                if (looks_like_github_repo_url(comment_url)) {
                    entry.github_repo = extract_github_repo(comment_url);
                    break;
                }
            }
        }

        // If still no direct_url but found gitlab_project, that's sufficient
        if (entry.direct_url.empty() && entry.gitlab_project.empty()) {
            for (const auto& comment_url : comment_urls) {
                if (looks_like_direct_download_url(comment_url)) {
                    entry.direct_url = comment_url;
                    break;
                }
            }
        }

        // Check comments for GitLab project URLs
        if (entry.gitlab_project.empty()) {
            for (const auto& comment_url : comment_urls) {
                if (looks_like_gitlab_url(comment_url)) {
                    entry.gitlab_project = extract_gitlab_project(comment_url);
                    break;
                }
            }
        }

        // If we found nothing useful, return nullopt
        if (entry.github_repo.empty() && entry.direct_url.empty() &&
            entry.gitlab_project.empty()) {
            return std::nullopt;
        }

        return entry;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Generic lookup: tries the package name as-is, then lowercase.
template <typename Entry>
std::optional<Entry> lookup_appimage_entry(
    const std::string& package_name,
    int timeout_ms,
    std::optional<Entry> (*parser)(const std::string&, int)) {
    auto entry = parser(package_name, timeout_ms);
    if (entry) return entry;
    const std::string lower_name = to_lower(package_name);
    if (lower_name != package_name) {
        entry = parser(lower_name, timeout_ms);
        if (entry) return entry;
    }
    return std::nullopt;
}

// Lookup a data/ entry by package name (used during repo resolve)
std::optional<AppImageDataEntry> lookup_appimage_data_entry(
    const std::string& package_name,
    int timeout_ms) {
    return lookup_appimage_entry(package_name, timeout_ms, parse_appimage_data_entry);
}

// Lookup an apps/ entry by package name (used during repo resolve)
std::optional<AppImageAppsEntry> lookup_appimage_apps_entry(
    const std::string& package_name,
    int timeout_ms) {
    return lookup_appimage_entry(package_name, timeout_ms, parse_appimage_apps_entry);
}


