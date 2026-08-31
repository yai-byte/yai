#include "yai.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace {

fs::path file_url_path(const std::string& url) {
    return fs::path(url.substr(7));
}

// Renders a file mtime the same way curl reports Last-Modified for file://
// responses: IMF-fixdate, GMT, second precision. The weekday and month names
// are hard-coded because strftime's %a/%b follow the process locale, and a
// localized string would never match the value curl stored at install time.
std::string http_date_from_file_time(const fs::path& path) {
    std::error_code ec;
    const fs::file_time_type mtime = fs::last_write_time(path, ec);
    if (ec) {
        return "";
    }

    // C++17 has no portable file_clock::to_sys(); rebase through "now".
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        mtime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t seconds = std::chrono::system_clock::to_time_t(system_time);

    std::tm parts{};
    if (gmtime_r(&seconds, &parts) == nullptr) {
        return "";
    }

    static const char* const day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const month_names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (parts.tm_wday < 0 || parts.tm_wday > 6 || parts.tm_mon < 0 || parts.tm_mon > 11) {
        return "";
    }

    char buffer[64];
    const int written = std::snprintf(
        buffer,
        sizeof(buffer),
        "%s, %02d %s %04d %02d:%02d:%02d GMT",
        day_names[parts.tm_wday],
        parts.tm_mday,
        month_names[parts.tm_mon],
        parts.tm_year + 1900,
        parts.tm_hour,
        parts.tm_min,
        parts.tm_sec);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
        return "";
    }
    return buffer;
}

fs::path unique_temp_headers_path() {
    static std::atomic<unsigned long> sequence{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const unsigned long seq = sequence.fetch_add(1);
    return fs::temp_directory_path() /
           ("yai-freshness-" + std::to_string(getpid()) + "-" +
            std::to_string(now) + "-" + std::to_string(seq) + ".headers");
}

}  // namespace

bool http_validators_empty(const HttpValidators& v) {
    return v.etag.empty() && v.last_modified.empty() && v.content_length.empty();
}

HttpValidators parse_http_validators_from_headers(const fs::path& headers) {
    HttpValidators result;
    std::ifstream in(headers);
    if (!in) {
        return result;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        const std::string lower = to_lower(line);

        const std::string etag_prefix = "etag:";
        if (lower.rfind(etag_prefix, 0) == 0) {
            const std::string value = trim(line.substr(etag_prefix.size()));
            if (!value.empty()) {
                result.etag = value;
            }
            continue;
        }

        const std::string lm_prefix = "last-modified:";
        if (lower.rfind(lm_prefix, 0) == 0) {
            const std::string value = trim(line.substr(lm_prefix.size()));
            if (!value.empty()) {
                result.last_modified = value;
            }
            continue;
        }

        const std::string cl_prefix = "content-length:";
        if (lower.rfind(cl_prefix, 0) == 0) {
            const std::string value = trim(line.substr(cl_prefix.size()));
            if (!value.empty()) {
                result.content_length = value;
            }
        }
    }

    return result;
}

UrlFreshness compare_http_validators(const HttpValidators& stored, const HttpValidators& remote) {
    const bool has_etag_pair = !stored.etag.empty() && !remote.etag.empty();
    const bool has_lm_pair = !stored.last_modified.empty() && !remote.last_modified.empty();
    const bool has_cl_pair = !stored.content_length.empty() && !remote.content_length.empty();

    // A differing validator always means the bytes differ. Content-Length is
    // checked independently of the strong validators: Last-Modified has
    // one-second resolution, so two writes inside the same second share it
    // while still differing in size.
    if (has_etag_pair && stored.etag != remote.etag) {
        return UrlFreshness::Changed;
    }
    if (has_lm_pair && stored.last_modified != remote.last_modified) {
        return UrlFreshness::Changed;
    }
    if (has_cl_pair && stored.content_length != remote.content_length) {
        return UrlFreshness::Changed;
    }

    // Equal Content-Length alone never proves the bytes are unchanged: an
    // in-place rewrite that keeps the size is invisible to a length check.
    // Without a matching ETag or Last-Modified, fall through to Unknown so the
    // upgrade path really compares the content instead of trusting the length.
    if (!has_etag_pair && !has_lm_pair) {
        return UrlFreshness::Unknown;
    }

    return UrlFreshness::Unchanged;
}

HttpValidators validators_from_metadata(const fs::path& metadata) {
    HttpValidators v;
    v.etag = metadata_value(metadata, "http_etag").value_or("");
    v.last_modified = metadata_value(metadata, "http_last_modified").value_or("");
    v.content_length = metadata_value(metadata, "http_content_length").value_or("");
    return v;
}

UrlFreshnessResult probe_url_freshness(const std::string& url, const HttpValidators& stored) {
    if (is_file_url(url)) {
        const fs::path path = file_url_path(url);
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) {
            return {UrlFreshness::Error, tr("file not found: ") + path.string(), {}};
        }
        const std::uintmax_t size = fs::file_size(path, ec);
        if (ec) {
            return {UrlFreshness::Error, tr("failed to read file size: ") + ec.message(), {}};
        }
        HttpValidators remote;
        remote.content_length = std::to_string(size);
        // Without a last_modified validator, an equal-size rewrite would leave
        // Content-Length as the only comparable pair and be misreported as
        // Unchanged. The mtime makes same-length in-place rewrites visible.
        remote.last_modified = http_date_from_file_time(path);
        return {compare_http_validators(stored, remote), "", remote};
    }

    if (!executable_available("curl")) {
        return {UrlFreshness::Unknown, tr("curl is not available for freshness probe"), {}};
    }

    const fs::path headers = unique_temp_headers_path();
    const ProcessResult result = run_process_capture_timeout({
        "curl",
        "--fail",
        "--location",
        "--silent",
        "--show-error",
        "--head",
        "--user-agent",
        "yai/1.0 (+https://github.com/AppImage/yai)",
        "--max-time",
        "10",
        "--dump-header",
        headers.string(),
        "--output",
        "/dev/null",
        url,
    }, 12000);

    const HttpValidators remote = parse_http_validators_from_headers(headers);
    const bool removed = remove_best_effort(headers);
    (void)removed;

    if (result.exit_code != 0 || result.timed_out) {
        std::string detail = result.timed_out ? tr("HEAD request timed out") : trim(result.output);
        if (detail.empty()) {
            detail = tr("HEAD request failed");
        }
        return {UrlFreshness::Error, detail, remote};
    }

    if (http_validators_empty(remote)) {
        return {UrlFreshness::Unknown, "", remote};
    }

    return {compare_http_validators(stored, remote), "", remote};
}

std::optional<std::int64_t> probe_url_last_modified_mtime(const std::string& url) {
    // Listing-driven file:// fixtures should not invent mtimes from local files.
    if (is_file_url(url) || !executable_available("curl")) {
        return std::nullopt;
    }

    const fs::path headers = unique_temp_headers_path();
    const ProcessResult result = run_process_capture_timeout({
        "curl",
        "--fail",
        "--location",
        "--silent",
        "--show-error",
        "--head",
        "--user-agent",
        "yai/1.0 (+https://github.com/AppImage/yai)",
        "--max-time",
        "10",
        "--dump-header",
        headers.string(),
        "--output",
        "/dev/null",
        url,
    }, 12000);

    const HttpValidators remote = parse_http_validators_from_headers(headers);
    const bool removed = remove_best_effort(headers);
    (void)removed;

    if (result.exit_code != 0 || result.timed_out || remote.last_modified.empty()) {
        return std::nullopt;
    }
    return parse_http_last_modified_mtime(remote.last_modified);
}

bool is_url_accessible(const std::string& url) {
    if (is_file_url(url)) {
        return fs::exists(file_url_path(url));
    }
    if (!executable_available("curl")) {
        return true;
    }
    const ProcessResult result = run_process_capture_timeout({
        "curl",
        "--fail",
        "--location",
        "--silent",
        "--show-error",
        "--head",
        "--user-agent",
        "yai/1.0 (+https://github.com/AppImage/yai)",
        "--max-time",
        "10",
        "--output",
        "/dev/null",
        url,
    }, 12000);
    return result.exit_code == 0 && !result.timed_out;
}
