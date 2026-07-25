#include "yai.hpp"

#include <fstream>

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
