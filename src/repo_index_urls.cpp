#include "yai.hpp"

// Helpers for optional download_url / download_urls fields on schema-v1 packages.

std::optional<std::string> repo_package_download_url_for_arch(
    const RepoPackage& package,
    const std::string& arch) {
    const std::string key = normalize_arch(arch);
    const auto it = package.download_urls.find(key);
    if (it != package.download_urls.end() && !it->second.empty()) {
        return it->second;
    }
    if (!package.download_url.empty() && package.download_urls.empty()) {
        return package.download_url;
    }
    return std::nullopt;
}

bool repo_package_has_download_url_for_arch(const RepoPackage& package, const std::string& arch) {
    return repo_package_download_url_for_arch(package, arch).has_value();
}

void repo_package_set_download_url(
    RepoPackage& package,
    const std::string& arch,
    const std::string& url,
    bool overwrite,
    bool mirror_to_download_url) {
    const std::string key = normalize_arch(arch);
    if (!overwrite) {
        const auto it = package.download_urls.find(key);
        if (it != package.download_urls.end() && !it->second.empty()) {
            return;
        }
    }

    package.download_urls[key] = url;
    if (mirror_to_download_url) {
        package.download_url = url;
    }
    package.resolved_at = current_utc_timestamp();
}
