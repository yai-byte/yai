# Task 4 review
#include "yai.hpp"

// Install-source dispatch: package match helpers, staging, resolve_install_source
// routing, and interactive network/mirror prompts. GitHub, URL/HTML, and website
// crawl implementations live in resolver_github.cpp, resolver_url.cpp, and
// resolver_website.cpp.

bool contains_case_insensitive(const std::string& value, const std::string& needle) {
    return to_lower(value).find(to_lower(needle)) != std::string::npos;
}

bool package_matches_keyword(const RepoPackage& package, const std::string& keyword) {
    if (has_glob_wildcards(keyword)) {
        return glob_match_case_insensitive(keyword, package.id) ||
               glob_match_case_insensitive(keyword, package.name) ||
               glob_match_case_insensitive(keyword, package.summary);
    }
    return keyword.empty() ||
           contains_case_insensitive(package.id, keyword) ||
           contains_case_insensitive(package.name, keyword) ||
           contains_case_insensitive(package.summary, keyword);
}

namespace {

std::string install_arch_for_options(const InstallOptions& options) {
    return options.target_arch.empty() ? current_arch() : normalize_arch(options.target_arch);
}

ResolvedSource with_install_arch(ResolvedSource source, const InstallOptions& options) {
    // --arch is only an asset-selection hint persisted for future update. It does
    // not imply that yai can run a non-native AppImage on this host.
    source.arch = install_arch_for_options(options);
    return source;
}

}  // namespace

std::string stage_appimage_source(
    const ResolvedSource& source,
...
8:bool contains_case_insensitive(const std::string& value, const std::string& needle) {
24:namespace {
26:std::string install_arch_for_options(const InstallOptions& options) {
39:std::string stage_appimage_source(
82:namespace {
84:ResolvedSource resolve_local_install_source(const InstallOptions& options) {
220:ResolvedSource resolve_install_source(const InstallOptions& options) {
236:bool source_uses_github_release_download(const ResolvedSource& source) {
241:NetworkConfig prompt_china_network_config() {
296:NetworkConfig prompt_github_release_proxy_for_this_download(NetworkConfig config) {
319:InstallOptions apply_network_config_to_options(
## Makefile
SRC := \
	src/arch.cpp \
	src/appimage.cpp \
	src/cli_download.cpp \
	src/commands.cpp \
	src/commands_doctor.cpp \
	src/commands_lifecycle.cpp \
	src/commands_query.cpp \
	src/commands_repo.cpp \
	src/commands_upgrade.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/resolver.cpp \
	src/resolver_github.cpp \
	src/resolver_url.cpp \
	src/resolver_website.cpp

  338 src/resolver.cpp
  165 src/resolver_github.cpp
  371 src/resolver_url.cpp
  343 src/resolver_website.cpp
 1217 总计
