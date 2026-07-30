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

std::string find_yaml_value(
    const std::map<std::string, std::string>& yaml,
    const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        auto it = yaml.find(key);
        if (it != yaml.end() && !it->second.empty()) {
            return it->second;
        }
    }
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

}  // namespace

// Fetch list of apps/ directory entries from GitHub API with pagination
std::vector<std::string> fetch_appimage_apps_list(int timeout_ms) {
    std::vector<std::string> all_names;
    int page = 1;

    while (true) {
        std::string url = std::string(kAppImageGithubRepoApiBase) +
            "/contents/apps?per_page=100&page=" + std::to_string(page);

        std::string response;
        try {
            response = fetch_text(url, timeout_ms);
        } catch (const std::exception&) {
            break;
        }

        if (response.empty()) break;

        // Parse JSON array
        auto items_json = json_top_level_objects(response);
        if (items_json.empty()) break;

        for (const auto& item_json : items_json) {
            std::string name = json_find_string(item_json, "name").value_or("");
            // Remove .md extension
            if (name.size() > 3 && name.compare(name.size() - 3, 3, ".md") == 0) {
                name = name.substr(0, name.size() - 3);
            }
            if (!name.empty()) {
                all_names.push_back(name);
            }
        }

        // Less than 100 items means we've reached the last page
        if (items_json.size() < 100) break;
        page++;
    }

    return all_names;
}

// Parse a single apps/<Name>.md file from GitHub API
std::optional<AppImageAppsEntry> parse_appimage_apps_entry(
    const std::string& name,
    int timeout_ms) {
    try {
        // Fetch via GitHub API to get base64 content
        std::string url = std::string(kAppImageGithubRepoApiBase) +
            "/contents/apps/" + url_encode(name) + ".md";
        std::string response = fetch_text(url, timeout_ms);

        // Extract the base64 content field
        std::string content_b64 = json_find_string(response, "content").value_or("");
        if (content_b64.empty()) {
            // Fallback: try raw.githubusercontent.com
            std::string raw_url = std::string(kAppImageGithubRawBase) +
                "/apps/" + url_encode(name) + ".md";
            std::string raw_content = fetch_text(raw_url, timeout_ms);
            if (raw_content.empty()) return std::nullopt;

            // Parse the raw markdown
            std::string yaml = extract_yaml_frontmatter(raw_content);
            if (yaml.empty()) return std::nullopt;

            auto parsed = parse_yaml_frontmatter(yaml);
            AppImageAppsEntry entry;
            entry.name = name;

            // Extract GitHub repo from links
            std::string github_link_type = find_yaml_value(parsed, "links.type");
            std::string github_link_url = find_yaml_value(parsed, "links.url");
            if (github_link_type.empty()) {
                // Try alternative nesting with indexed keys
                for (const auto& [k, v] : parsed) {
                    bool is_link_type = false;
                    if (k == "links.type" ||
                        (k.size() > 11 && k.substr(0, 6) == "links." &&
                         k.substr(k.size() - 5) == ".type")) {
                        is_link_type = true;
                    }
                    if (is_link_type && to_lower(v) == "github") {
                        std::string prefix = k.substr(0, k.rfind(".type"));
                        entry.github_repo = extract_github_repo(
                            find_yaml_value(parsed, prefix + ".url"));
                        break;
                    }
                }
            } else if (to_lower(github_link_type) == "github" ||
                       to_lower(github_link_type) == "repository") {
                entry.github_repo = extract_github_repo(github_link_url);
            }

            // If still no github repo, try all link entries
            if (entry.github_repo.empty()) {
                for (const auto& [k, v] : parsed) {
                    if (k.find("url") != std::string::npos &&
                        v.find("github.com/") != std::string::npos) {
                        entry.github_repo = extract_github_repo(v);
                        break;
                    }
                }
            }

            // Extract direct download URL
            for (const auto& [k, v] : parsed) {
                if (k.find("url") != std::string::npos &&
                    looks_like_direct_download_url(v)) {
                    entry.direct_url = v;
                    break;
                }
            }

            // Extract homepage
            entry.homepage = find_yaml_value(parsed,
                {"links.Web", "links.Homepage", "links.Home"});
            if (entry.homepage.empty()) {
                for (const auto& [k, v] : parsed) {
                    if (k.find("type") != std::string::npos &&
                        (to_lower(v) == "web" || to_lower(v) == "homepage")) {
                        std::string prefix = k.substr(0, k.rfind(".type"));
                        entry.homepage = find_yaml_value(parsed, prefix + ".url");
                        break;
                    }
                }
            }

            // Extract description and license
            entry.description = find_yaml_value(parsed, "description");
            entry.license = find_yaml_value(parsed, "license");

            // Extract arch and version from desktop section
            entry.arch = find_yaml_value(parsed, "desktop.X-AppImage-Arch");
            entry.version = find_yaml_value(parsed, "desktop.X-AppImage-Version");

            return entry;
        }

        // Decode base64 content (GitHub API returns it with newlines)
        // Remove newlines from base64 string
        std::string clean_b64;
        for (char c : content_b64) {
            if (c != '\n' && c != '\r') clean_b64 += c;
        }
        std::string decoded = base64_decode(clean_b64);

        // Extract YAML frontmatter
        std::string yaml = extract_yaml_frontmatter(decoded);
        if (yaml.empty()) return std::nullopt;

        auto parsed = parse_yaml_frontmatter(yaml);
        AppImageAppsEntry entry;
        entry.name = name;

        // Extract GitHub repo from links - look for the GitHub type link
        bool found_github = false;
        bool found_download = false;
        for (const auto& [k, v] : parsed) {
            // Match both "links.type" and "links.N.type" patterns
            bool is_link_type = false;
            std::string prefix;
            if (k == "links.type" ||
                (k.size() > 11 && k.substr(0, 6) == "links." &&
                 k.substr(k.size() - 5) == ".type")) {
                is_link_type = true;
                prefix = k.substr(0, k.rfind(".type"));
            }
            if (is_link_type) {
                std::string link_type = to_lower(v);
                std::string link_url = find_yaml_value(parsed, prefix + ".url");

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
        }

        // If no explicit GitHub link found, search for GitHub URLs in any link
        if (entry.github_repo.empty()) {
            for (const auto& [k, v] : parsed) {
                if (k.find("url") != std::string::npos &&
                    v.find("github.com/") != std::string::npos) {
                    entry.github_repo = extract_github_repo(v);
                    break;
                }
            }
        }

        // Extract description and license
        entry.description = find_yaml_value(parsed, "description");
        entry.license = find_yaml_value(parsed, "license");

        // Extract arch and version from desktop section
        entry.arch = find_yaml_value(parsed,
            "desktop.X-AppImage-Arch");
        entry.version = find_yaml_value(parsed,
            "desktop.X-AppImage-Version");

        return entry;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Parse a single data/<Name> file from raw.githubusercontent.com
std::optional<AppImageDataEntry> parse_appimage_data_entry(
    const std::string& name,
    int timeout_ms) {
    try {
        std::string url = std::string(kAppImageGithubRawBase) +
            "/data/" + url_encode(name);
        std::string content = fetch_text(url, timeout_ms);

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
            } else if (looks_like_github_repo_url(primary_url)) {
                entry.github_repo = extract_github_repo(primary_url);
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

        // If still no direct_url, check comments for download links
        if (entry.direct_url.empty()) {
            for (const auto& comment_url : comment_urls) {
                if (looks_like_direct_download_url(comment_url)) {
                    entry.direct_url = comment_url;
                    break;
                }
            }
        }

        // If we found nothing useful, return nullopt
        if (entry.github_repo.empty() && entry.direct_url.empty()) {
            return std::nullopt;
        }

        return entry;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Lookup a data/ entry by package name (used during repo resolve)
std::optional<AppImageDataEntry> lookup_appimage_data_entry(
    const std::string& package_name,
    int timeout_ms) {
    // Try the package name as-is
    auto entry = parse_appimage_data_entry(package_name, timeout_ms);
    if (entry.has_value()) return entry;

    // Try sanitized ID (lowercase)
    std::string lower_name = to_lower(package_name);
    if (lower_name != package_name) {
        entry = parse_appimage_data_entry(lower_name, timeout_ms);
        if (entry.has_value()) return entry;
    }

    return std::nullopt;
}

// Lookup an apps/ entry by package name (used during repo resolve)
std::optional<AppImageAppsEntry> lookup_appimage_apps_entry(
    const std::string& package_name,
    int timeout_ms) {
    // Try the package name as-is
    auto entry = parse_appimage_apps_entry(package_name, timeout_ms);
    if (entry.has_value()) return entry;

    // Try sanitized ID (lowercase)
    std::string lower_name = to_lower(package_name);
    if (lower_name != package_name) {
        entry = parse_appimage_apps_entry(lower_name, timeout_ms);
        if (entry.has_value()) return entry;
    }

    return std::nullopt;
}

// Merge an apps/ entry into an existing RepoPackage
RepoPackage merge_apps_entry_into_package(
    const AppImageAppsEntry& entry,
    const RepoPackage& existing) {
    RepoPackage merged = existing;

    // Mark source origin as appimage_apps
    merged.source_origin = "appimage_apps";

    // Supplement metadata from apps/ (only if feed didn't provide it)
    if (merged.summary.empty() && !entry.description.empty()) {
        merged.summary = entry.description;
    }
    if (merged.license.empty() && !entry.license.empty()) {
        merged.license = entry.license;
    }
    if (merged.homepage.empty() && !entry.homepage.empty()) {
        merged.homepage = entry.homepage;
    }

    // Add arch and version from apps/
    if (!entry.arch.empty()) {
        merged.arch = entry.arch;
    }
    if (!entry.version.empty()) {
        merged.version = entry.version;
    }

    // Supplement GitHub repo if feed didn't have it
    if (merged.source_owner.empty() && merged.source_repo.empty() &&
        !entry.github_repo.empty()) {
        std::size_t slash = entry.github_repo.find('/');
        if (slash != std::string::npos) {
            merged.source_owner = entry.github_repo.substr(0, slash);
            merged.source_repo = entry.github_repo.substr(slash + 1);
            if (merged.source_type.empty() ||
                merged.source_type == "website_page" ||
                merged.source_type == "unavailable") {
                merged.source_type = "github_release";
                merged.asset_pattern = ".*\\.AppImage$";
            }
        }
    }

    return merged;
}
