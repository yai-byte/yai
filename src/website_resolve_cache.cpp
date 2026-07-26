#include "yai.hpp"

#include <chrono>

namespace {

std::int64_t utc_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::optional<std::int64_t> json_find_int64(const std::string& text, const std::string& key) {
    const std::optional<std::size_t> start = json_value_start_after_key(text, key);
    if (!start.has_value()) {
        return std::nullopt;
    }
    std::size_t end = *start;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    if (end == *start) {
        return std::nullopt;
    }
    return std::stoll(text.substr(*start, end - *start));
}

WebsiteResolveCacheEntry parse_website_resolve_cache_entry(const std::string& object_text) {
    WebsiteResolveCacheEntry entry;
    entry.package_id = json_find_string(object_text, "package_id").value_or("");
    entry.arch = json_find_string(object_text, "arch").value_or("");
    entry.source_url = json_find_string(object_text, "source_url").value_or("");
    entry.download_url = json_find_string(object_text, "download_url").value_or("");
    entry.resolved_at = json_find_int64(object_text, "resolved_at").value_or(0);
    entry.validators.etag = json_find_string(object_text, "http_etag").value_or("");
    entry.validators.last_modified =
        json_find_string(object_text, "http_last_modified").value_or("");
    entry.validators.content_length =
        json_find_string(object_text, "http_content_length").value_or("");
    return entry;
}

std::string website_resolve_cache_entry_json(const WebsiteResolveCacheEntry& entry) {
    return std::string(
               "  {\n"
               "    \"package_id\": \"") +
           json_escape_string(entry.package_id) + "\",\n"
           "    \"arch\": \"" + json_escape_string(entry.arch) + "\",\n"
           "    \"source_url\": \"" + json_escape_string(entry.source_url) + "\",\n"
           "    \"download_url\": \"" + json_escape_string(entry.download_url) + "\",\n"
           "    \"resolved_at\": " + std::to_string(entry.resolved_at) + ",\n"
           "    \"http_etag\": \"" + json_escape_string(entry.validators.etag) + "\",\n"
           "    \"http_last_modified\": \"" +
           json_escape_string(entry.validators.last_modified) + "\",\n"
           "    \"http_content_length\": \"" +
           json_escape_string(entry.validators.content_length) + "\"\n"
           "  }";
}

std::string website_resolve_cache_entry_key(const WebsiteResolveCacheEntry& entry) {
    return website_resolve_cache_key(entry.package_id, entry.arch, entry.source_url);
}

}  // namespace

fs::path website_resolve_cache_path() {
    return expand_home_path(".local/share/yai/website-resolve-cache.json");
}

std::string website_resolve_cache_key(
    const std::string& package_id,
    const std::string& arch,
    const std::string& source_url) {
    return sanitize_id(package_id) + "|" + normalize_arch(arch) + "|" +
           strip_url_fragment_query(source_url);
}

std::vector<WebsiteResolveCacheEntry> load_website_resolve_cache() {
    const fs::path path = website_resolve_cache_path();
    if (!fs::exists(path)) {
        return {};
    }

    const std::string text = read_text_file(path);
    if (text.empty() || text.front() != '[') {
        return {};
    }

    std::vector<WebsiteResolveCacheEntry> entries;
    for (const std::string& object : json_top_level_objects(text)) {
        const WebsiteResolveCacheEntry entry = parse_website_resolve_cache_entry(object);
        if (entry.package_id.empty() || entry.arch.empty() || entry.source_url.empty()) {
            continue;
        }
        entries.push_back(entry);
    }
    return entries;
}

void save_website_resolve_cache(const std::vector<WebsiteResolveCacheEntry>& entries) {
    const fs::path path = website_resolve_cache_path();
    ensure_directory(path.parent_path());

    std::string json = "[\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        json += website_resolve_cache_entry_json(entries[i]);
        if (i + 1 < entries.size()) {
            json += ",";
        }
        json += "\n";
    }
    json += "]\n";
    write_text_file(path, json);
}

std::optional<WebsiteResolveCacheEntry> find_website_resolve_cache_entry(
    const std::vector<WebsiteResolveCacheEntry>& entries,
    const std::string& package_id,
    const std::string& arch,
    const std::string& source_url) {
    const std::string key = website_resolve_cache_key(package_id, arch, source_url);
    for (const WebsiteResolveCacheEntry& entry : entries) {
        if (website_resolve_cache_entry_key(entry) == key) {
            return entry;
        }
    }
    return std::nullopt;
}

bool website_resolve_cache_entry_expired(
    const WebsiteResolveCacheEntry& entry,
    std::int64_t now_epoch_seconds) {
    if (entry.resolved_at <= 0) {
        return true;
    }
    return now_epoch_seconds - entry.resolved_at > kWebsiteResolveCacheTtlSeconds;
}

void upsert_website_resolve_cache_entry(WebsiteResolveCacheEntry entry) {
    if (entry.resolved_at <= 0) {
        entry.resolved_at = utc_epoch_seconds();
    }

    std::vector<WebsiteResolveCacheEntry> entries = load_website_resolve_cache();
    const std::string key = website_resolve_cache_entry_key(entry);
    bool replaced = false;
    for (WebsiteResolveCacheEntry& existing : entries) {
        if (website_resolve_cache_entry_key(existing) == key) {
            existing = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        entries.push_back(entry);
    }
    save_website_resolve_cache(entries);
}

bool website_cached_download_url_usable(
    const std::string& download_url,
    const HttpValidators& stored) {
    if (!is_appimage_download_url(download_url)) {
        return false;
    }
    const UrlFreshnessResult result = probe_url_freshness(download_url, stored);
    return result.status == UrlFreshness::Unchanged ||
           result.status == UrlFreshness::Unknown;
}
