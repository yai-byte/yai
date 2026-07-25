#include "yai.hpp"

#include <atomic>
#include <chrono>
#include <fstream>

namespace {

fs::path file_url_path(const std::string& url) {
    return fs::path(url.substr(7));
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

    int comparable_pairs = 0;

    if (has_etag_pair) {
        ++comparable_pairs;
        if (stored.etag != remote.etag) {
            return UrlFreshness::Changed;
        }
    }

    if (has_lm_pair) {
        ++comparable_pairs;
        if (stored.last_modified != remote.last_modified) {
            return UrlFreshness::Changed;
        }
    }

    if (!has_etag_pair && !has_lm_pair && has_cl_pair) {
        ++comparable_pairs;
        if (stored.content_length != remote.content_length) {
            return UrlFreshness::Changed;
        }
    }

    if (comparable_pairs == 0) {
        return UrlFreshness::Unknown;
    }

    return UrlFreshness::Unchanged;
}

UrlFreshnessResult probe_url_freshness(const std::string& url, const HttpValidators& stored) {
    if (is_file_url(url)) {
        const fs::path path = file_url_path(url);
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) {
            return {UrlFreshness::Error, "file not found: " + path.string(), {}};
        }
        const std::uintmax_t size = fs::file_size(path, ec);
        if (ec) {
            return {UrlFreshness::Error, "failed to read file size: " + ec.message(), {}};
        }
        HttpValidators remote;
        remote.content_length = std::to_string(size);
        return {compare_http_validators(stored, remote), "", remote};
    }

    if (!executable_available("curl")) {
        return {UrlFreshness::Unknown, "curl is not available for freshness probe", {}};
    }

    const fs::path headers = unique_temp_headers_path();
    const ProcessResult result = run_process_capture_timeout({
        "curl",
        "--fail",
        "--location",
        "--silent",
        "--show-error",
        "--head",
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
        std::string detail = result.timed_out ? "HEAD request timed out" : trim(result.output);
        if (detail.empty()) {
            detail = "HEAD request failed";
        }
        return {UrlFreshness::Error, detail, remote};
    }

    if (http_validators_empty(remote)) {
        return {UrlFreshness::Unknown, "", remote};
    }

    return {compare_http_validators(stored, remote), "", remote};
}
