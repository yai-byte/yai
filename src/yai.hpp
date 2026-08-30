#pragma once

#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <ctime>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct InstallOptions {
    // Normalized CLI input shared by install, update, and download paths. It is
    // parsed once at the boundary so later code can focus on resolving and
    // staging sources instead of argv shape.
    std::string target;
    std::string id;
    std::string name;
    std::string target_arch;
    std::string download_strategy = "direct";
    std::string mirror_template;
    std::string downloader = "auto";
    bool id_explicit = false;
    bool name_explicit = false;
    bool arch_explicit = false;
    bool download_explicit = false;
    bool mirror_template_explicit = false;
    bool downloader_explicit = false;
    bool yes = false;
    bool recrawl = false;
};

struct GitHubReleaseAsset {
    std::string name;
    std::string url;
};

struct GitHubRelease {
    std::string owner;
    std::string repo;
    std::string tag;
    GitHubReleaseAsset asset;
};

struct ResolvedSource {
    // Boundary object between source resolution and file staging. Resolvers fill
    // the trusted upstream identity and selected asset; installers and download
    // commands only consume this shape.
    std::string source_kind = "url";
    std::string id;
    std::string name;
    std::string version;
    std::string arch;
    std::string source_url;
    std::string download_url;
    std::string http_etag;
    std::string http_last_modified;
    std::string http_content_length;
    std::string github_owner;
    std::string github_repo;
    std::string github_asset;
};

struct AppImageAppsEntry {
    std::string name;
    std::string github_repo;
    std::string direct_url;
    std::string homepage;
    std::string description;
    std::string license;
    std::string arch;
    std::string version;
};

struct AppImageDataEntry {
    std::string name;
    std::string github_repo;
    std::string direct_url;
    std::string gitlab_project;  // project path for GitLab-hosted AppImages
};

struct RepoPackage {
    // Canonical package record loaded from a schema-v1 repo index. AppImage feed
    // entries are normalized into this form before search, info, or install code
    // sees them.
    std::string id;
    std::string name;
    std::string summary;
    std::string homepage;
    std::string license;
    std::string source_type;
    std::string source_owner;
    std::string source_repo;
    std::string source_url;
    std::string source_reason;
    std::string asset_pattern;
    std::string download_url;                 // optional single-arch convenience
    std::map<std::string, std::string> download_urls; // arch -> url
    std::string resolved_at;                  // ISO-8601 UTC, may be empty
    std::string arch;                        // arch tag from X-AppImage-Arch
    std::string version;                     // version from X-AppImage-Version
    std::string source_origin;               // "feed" or "appimage_apps"
};

struct RepoEntry {
    std::string name;
    std::string location;
};

struct RepoResolveOptions {
    std::optional<fs::path> output;
    std::vector<std::string> arches; // empty → { current_arch() }; "all" → all canonical arches
    std::vector<std::string> types;  // empty → github_release, website_page, direct_url
    std::vector<std::string> packages; // empty → all
    bool overwrite = false;
    int concurrency = 0;  // 0 = auto-detect based on CPU cores
    bool aggressive = false;  // Force aggressive concurrency (8-16 threads)
    bool show_success = false;
    bool show_skip = false;
    bool show_fail = true;   // default --show 001
    bool summary = true;
};

struct NetworkConfig {
    // Stored network preference for GitHub Release assets. Mirror settings change
    // the transport URL only; they do not change the upstream source metadata or
    // the package trust boundary.
    bool exists = false;
    bool prompted = false;
    std::string provider = "direct";
    std::string download_strategy = "direct";
    std::string mirror_template;
};

struct MirrorProvider {
    // Built-in mirror template description used to rewrite public GitHub Release
    // downloads when the user opts into a proxy strategy.
    std::string name;
    std::string mirror_template;
    std::string description;
};


struct InstallPaths {
    // Every installed artifact for an id is derived here, which keeps install,
    // repair, remove, upgrade, and rollback operating on the same filesystem
    // layout.
    fs::path app_dir;
    fs::path appimage;
    fs::path extracted_dir;
    fs::path wrapper;
    fs::path desktop;
    fs::path metadata;
};

struct ProcessResult {
    // Captured child-process status plus merged stdout/stderr. Runtime probes use
    // timed_out to distinguish a still-running GUI launch from a failed command.
    int exit_code = 1;
    std::string output;
    bool timed_out = false;
    bool output_limit_exceeded = false;
};

struct ProcessOutput {
    int exit_code = 1;
    std::string stdout_text;
    std::string stderr_text;
};

struct DownloadProgressSample {
    std::chrono::steady_clock::time_point time;
    std::uintmax_t downloaded = 0;
};

struct Aria2RpcProgress {
    std::uintmax_t completed = 0;
    std::optional<std::uintmax_t> total;
    std::optional<double> speed_bps;
};

struct Aria2GlobalStat {
    std::uintmax_t num_active = 0;
    std::uintmax_t num_waiting = 0;
    std::uintmax_t num_stopped = 0;
};

struct DownloadProgressState {
    std::vector<DownloadProgressSample> samples;
    std::optional<std::chrono::steady_clock::time_point> last_progress_time;
    std::optional<Aria2RpcProgress> last_aria2_rpc;
    double bytes_per_second = 0.0;
};

struct RepairResult {
    // Runtime-mode probe result propagated to command code so it can write the
    // matching wrapper and tell users when FUSE caused a fallback.
    std::string mode;
    bool fuse_error_detected = false;
    std::string detail;
};

struct DesktopSource {
    fs::path root;
    bool cleanup = false;
};

enum class Language {
    English,
    Chinese,
};


// Semantic version of the yai binary, used by --version / -v and as the
// canonical source for any user-facing version string. Bump this on releases.
constexpr const char* kYaiVersion = "0.1.0";

extern const char* APPIMAGE_FEED_URL;

std::optional<std::string> env_string(const char* name);
std::string ascii_lower(std::string value);
Language current_language();
bool use_chinese();
std::string tr(const std::string& msgid);
std::string tr_format(
    const std::string& msgid,
    const std::vector<std::pair<std::string, std::string>>& replacements);
std::string home_dir();
fs::path expand_home_path(const std::string& path);
fs::path config_dir_path();
fs::path network_config_path();
fs::path github_blocklist_path();
bool contains_line_break(const std::string& value);
std::string trim(std::string value);
std::string basename_from_url(const std::string& url);
std::string strip_appimage_suffix(std::string value);
std::string sanitize_id(const std::string& value);
bool token_looks_like_arch(const std::string& token);
std::string base_name_from_appimage_filename(const std::string& filename);
std::string shell_escape_double_quoted(const std::string& value);
std::string desktop_escape(const std::string& value);
std::string to_lower(std::string value);
bool has_url_scheme(const std::string& value);
bool looks_like_github_repo(const std::string& value);
bool looks_like_github_repo_url(const std::string& url);
bool looks_like_gitlab_url(const std::string& url);
std::string extract_gitlab_project(const std::string& url);
bool looks_like_direct_download_url(const std::string& url);
bool looks_like_repo_package_target(const std::string& value);
bool looks_like_local_appimage_target(const std::string& value);
bool has_glob_wildcards(const std::string& value);
bool glob_match_case_insensitive(const std::string& pattern, const std::string& value);
std::vector<std::string> resolve_installed_package_ids(const std::string& pattern);
std::string resolve_installed_package_id(const std::string& pattern);
bool confirm_multi_match(
    const std::string& prompt,
    const std::vector<std::string>& matches,
    bool yes);
std::string url_encode(const std::string& value);
std::string replace_all(std::string value, const std::string& from, const std::string& to);
bool output_has_fuse_error(const std::string& output);
int run_process(const std::vector<std::string>& args);
ProcessResult run_process_capture(
    const std::vector<std::string>& args,
    const std::optional<fs::path>& cwd = std::nullopt,
    const std::vector<std::pair<std::string, std::string>>& env = {});
ProcessOutput run_process_capture_separate(
    const std::vector<std::string>& args,
    const std::optional<fs::path>& cwd = std::nullopt,
    const std::vector<std::pair<std::string, std::string>>& env = {});
ProcessResult run_process_capture_timeout(
    const std::vector<std::string>& args,
    int timeout_ms,
    const std::optional<fs::path>& cwd = std::nullopt,
    const std::vector<std::pair<std::string, std::string>>& env = {},
    std::size_t max_output_bytes = 0);
std::string format_byte_count(std::uintmax_t bytes);
std::string format_duration_seconds(double seconds);
std::size_t display_width(const std::string& value);
std::string truncate_display_width(const std::string& value, std::size_t max_width);
std::size_t terminal_width();
bool stdout_color_enabled();
std::string color_green(const std::string& text);
struct HttpValidators {
    std::string etag;
    std::string last_modified;
    std::string content_length;
};

enum class UrlFreshness {
    Unchanged,
    Changed,
    Unknown,
    Error,
};

struct UrlFreshnessResult {
    UrlFreshness status;
    std::string detail;
    HttpValidators remote;
};

bool http_validators_empty(const HttpValidators& v);
HttpValidators parse_http_validators_from_headers(const fs::path& headers);
UrlFreshness compare_http_validators(const HttpValidators& stored, const HttpValidators& remote);
UrlFreshnessResult probe_url_freshness(const std::string& url, const HttpValidators& stored);
HttpValidators validators_from_metadata(const fs::path& metadata);

std::optional<std::uintmax_t> download_total_from_headers(const fs::path& headers);

std::optional<Aria2RpcProgress> parse_aria2_tell_active_response(const std::string& json);
std::optional<Aria2GlobalStat> parse_aria2_global_stat(const std::string& json);
bool aria2_rpc_session_finished(const Aria2GlobalStat& stat);
std::optional<Aria2RpcProgress> merge_aria2_rpc_progress(
    const std::optional<Aria2RpcProgress>& previous,
    const std::optional<Aria2RpcProgress>& current);
std::optional<Aria2RpcProgress> query_aria2_rpc_progress(std::uint16_t port);
void request_aria2_force_shutdown(std::uint16_t port);

std::uint16_t allocate_loopback_tcp_port();

struct DownloadToolCommand {
    std::vector<std::string> args;
    std::optional<std::uint16_t> aria2_rpc_port;
};

// GitHub (+ known GitHub mirrors) keep high aria2 concurrency; other CDNs use a
// lower split to reduce mid-transfer throttling on Range-sensitive hosts.
int aria2_connections_for_url(const std::string& url);

DownloadToolCommand build_downloader_command(
    const std::string& downloader,
    const std::string& url,
    const fs::path& part,
    const fs::path& headers);

double download_progress_recent_speed(
    DownloadProgressState& state,
    const std::chrono::steady_clock::time_point& now,
    std::uintmax_t downloaded);
std::string progress_bar(
    std::optional<std::uintmax_t> total,
    std::uintmax_t downloaded,
    std::size_t width,
    int tick);
void render_download_progress(
    const fs::path& part,
    const fs::path& headers,
    const std::chrono::steady_clock::time_point& start,
    int tick,
    std::size_t& last_width,
    DownloadProgressState& state,
    std::optional<std::uint16_t> aria2_rpc_port = std::nullopt);
void clear_download_progress(std::size_t& last_width);

struct BatchProgressEvent {
    enum class Kind { Progress, Clear } kind = Kind::Progress;
    std::uintmax_t done = 0;
    std::optional<std::uintmax_t> total;
    double rate_bps = 0.0;
    double elapsed = 0.0;
    // Negative means unknown (same convention as single-package progress).
    double total_seconds = -1.0;
    double left_seconds = -1.0;
};

std::optional<BatchProgressEvent> parse_batch_progress_event(const std::string& line);
std::string format_batch_progress_event(const BatchProgressEvent& event);
std::string format_batch_progress_clear_event();
int batch_event_fd();
// Same layout as single-package TTY progress (stats left, bar right), sized to columns.
std::string format_download_progress_line(
    std::uintmax_t downloaded,
    std::optional<std::uintmax_t> total,
    double bytes_per_second,
    double elapsed,
    double total_seconds,
    double left_seconds,
    std::size_t columns,
    int tick);

class BatchTerminalUi {
public:
    explicit BatchTerminalUi(std::size_t total_tasks);

    void log_parent(const std::string& message);
    void log_line(std::size_t index, const std::string& target, const std::string& message);
    void apply_event(std::size_t index, const std::string& target, const BatchProgressEvent& event);
    void clear_task(std::size_t index);
    void shutdown();

private:
    struct ProgressRow {
        std::string target;
        BatchProgressEvent event;
        int tick = 0;
    };

    std::string task_prefix(std::size_t index, const std::string& target) const;
    void clear_footer_locked();
    void redraw_footer_locked();
    std::string format_footer_row(std::size_t index, const ProgressRow& row) const;

    std::mutex mutex_;
    std::size_t total_ = 0;
    bool tty_ = false;
    std::map<std::size_t, ProgressRow> active_;
    std::size_t footer_lines_ = 0;
};

struct StreamingBatchResult {
    int exit_code = 1;
};

// Inputs for a single batched child process. Bundling these into a struct keeps
// call sites readable and avoids the 7-parameter signature that previously
// forced callers to remember argument order.
struct BatchTaskRequest {
    std::vector<std::string> args;
    std::optional<fs::path> cwd;
    std::vector<std::pair<std::string, std::string>> base_env;
    std::size_t index = 0;
    std::string target;
};

StreamingBatchResult run_batch_task_streaming(
    const BatchTaskRequest& request,
    BatchTerminalUi& ui);

ProcessResult run_process_capture_download_progress(
    const std::vector<std::string>& args,
    const fs::path& part,
    const fs::path& headers,
    std::optional<std::uint16_t> aria2_rpc_port = std::nullopt);
std::string normalize_arch(const std::string& value);
bool is_supported_arch(const std::string& value);
std::string supported_arch_list();
std::string current_arch();
std::vector<std::string> canonical_arches();
void ensure_directory(const fs::path& path);
void remove_required(const fs::path& path, const std::string& context);
void remove_all_required(const fs::path& path, const std::string& context);
bool remove_best_effort(const fs::path& path);
bool remove_all_best_effort(const fs::path& path);
void write_executable_file(const fs::path& path, const std::string& content);
void write_text_file(const fs::path& path, const std::string& content);
void write_text_file_atomic(const fs::path& path, const std::string& content);
std::string read_text_file(const fs::path& path);
void copy_file_overwrite(const fs::path& from, const fs::path& to);
std::vector<MirrorProvider> built_in_mirror_providers();
std::optional<MirrorProvider> mirror_provider_by_name(const std::string& name);
bool template_has_placeholder(const std::string& value);
std::string normalize_custom_mirror_template(std::string value);
std::optional<std::string> key_value_file_value(const fs::path& file, const std::string& key);
NetworkConfig load_network_config();
void write_network_config(const NetworkConfig& config);
std::string china_network_disclaimer();
InstallPaths paths_for(const std::string& id);
void print_usage();
void print_version();
std::string read_option_value(int argc, char** argv, int& index, const std::string& option);
void parse_arch_option(InstallOptions& options, const std::string& value);
void parse_download_strategy_option(InstallOptions& options, const std::string& value);
void parse_mirror_template_option(InstallOptions& options, const std::string& value);
void parse_downloader_option(InstallOptions& options, const std::string& value);
void validate_mirror_options(const InstallOptions& options);
InstallOptions parse_install_options(int argc, char** argv);
InstallOptions parse_download_options(int argc, char** argv);
bool executable_available(const std::string& name);
HttpValidators download_file(const std::string& url, const fs::path& target, const std::string& downloader);
constexpr int kFetchTextDefaultTimeoutMs = 15000;
constexpr int kFetchTextFeedTimeoutMs = 120000;
constexpr int kFetchTextSpeculativeTimeoutMs = 5000;
constexpr int kFetchTextFallbackTimeoutMs = 5000;
constexpr int kFetchTextResolveParallelWaitMs = 3000;
constexpr std::uintmax_t kFetchTextLandingMaxBytes = 512ull * 1024ull;

// Remote index defaults (yai-repo). Used by yai update to fetch a centrally
// resolved index.json when YAI_REPO_INDEX is not set.
constexpr const char* kRepoIndexUrlGithub =
    "https://raw.githubusercontent.com/yai-byte/yai-repo/main/index.json";
constexpr const char* kRepoIndexUrlGitee =
    "https://gitee.com/no11no16/yai-repo/raw/main/index.json";
constexpr int kRepoIndexFreshnessDefaultDays = 7;
constexpr int kFetchRepoIndexTimeoutMs = 30000;

constexpr const char* kAppImageGithubRepoApiBase =
    "https://api.github.com/repos/AppImage/appimage.github.io";
constexpr const char* kAppImageGithubRawBase =
    "https://raw.githubusercontent.com/AppImage/appimage.github.io/master";
constexpr int kFetchAppImageGithubListTimeoutMs = 15000;
constexpr int kFetchAppImageGithubEntryTimeoutMs = 10000;
constexpr int kFetchAppImageGithubResolveTimeoutMs = 15000;

std::string fetch_text(const std::string& url);
std::string fetch_text(const std::string& url, int timeout_ms);
std::string fetch_text_limited(
    const std::string& url,
    int timeout_ms,
    std::uintmax_t max_bytes);

struct FetchedTextResult {
    std::string content;
    std::string effective_url;
};

FetchedTextResult fetch_text_with_effective_url(
    const std::string& url,
    int timeout_ms,
    std::uintmax_t max_bytes);
std::string json_unescape_string(const std::string& value);
std::optional<std::string> json_string_after(const std::string& text, std::size_t key_pos);
std::optional<std::string> json_find_string(const std::string& text, const std::string& key);
std::vector<std::string> json_find_all_strings(const std::string& text, const std::string& key);
std::optional<std::size_t> json_value_start_after_key(const std::string& text, const std::string& key);
std::optional<std::string> json_extract_balanced(
    const std::string& text,
    std::size_t start,
    char open_ch,
    char close_ch);
std::optional<std::string> json_find_object(const std::string& text, const std::string& key);
std::optional<std::string> json_find_array(const std::string& text, const std::string& key);
std::optional<int> json_find_int(const std::string& text, const std::string& key);
// Returns the textual representation of a JSON number value (int or float) for
// the given key. Useful for IDs that exceed int range (e.g. GitLab pipeline
// IDs) or when a field may be either a quoted string or a bare number.
std::optional<std::string> json_find_number_as_string(const std::string& text, const std::string& key);
std::map<std::string, std::string> json_find_string_map(const std::string& object_text, const std::string& key);
std::vector<std::string> json_top_level_objects(const std::string& array_text);
fs::path repo_index_path();
fs::path repos_dir_path();
fs::path repo_config_path();
fs::path named_repo_index_path(const std::string& name);
std::string json_escape_string(const std::string& value);
std::string collapse_whitespace(const std::string& value);
std::string html_to_plain_text(const std::string& value);
std::string current_utc_timestamp();
std::string load_repo_index_text();
RepoPackage parse_repo_package(const std::string& object_text);
std::string serialize_repo_package(const RepoPackage& package);
std::optional<std::string> repo_package_download_url_for_arch(
    const RepoPackage& package,
    const std::string& arch);
bool repo_package_has_download_url_for_arch(const RepoPackage& package, const std::string& arch);
void repo_package_set_download_url(
    RepoPackage& package,
    const std::string& arch,
    const std::string& url,
    bool overwrite,
    bool mirror_to_download_url);
std::vector<RepoPackage> load_repo_packages();
std::vector<std::string> repo_package_objects_from_index(const std::string& index_text);
bool repo_index_is_locally_writable();
void save_repo_packages_index(const std::vector<RepoPackage>& packages, const fs::path& path);
void upsert_repo_package_download_urls(const RepoPackage& updated);
void upsert_overlay_package_download_url(const RepoPackage& updated);
RepoPackage merge_repo_package_download_url_fields(
    const RepoPackage& incoming,
    const RepoPackage& previous);

// --- Index strategy and remote index support ---
enum class IndexStrategy { Auto, Trust, Live };
std::string detect_index_region();
std::string default_repo_index_url();
std::string fetch_remote_repo_index_text();
std::string repo_index_updated_at(const std::string& index_text);
bool repo_index_is_fresh(const std::string& index_text, int threshold_days);
fs::path resolved_overlay_path();
std::vector<RepoPackage> load_repo_packages_with_overlay();
std::string merge_named_repo_index_text(
    const RepoEntry& entry,
    const std::string& incoming_index_text);
std::optional<std::string> github_repo_from_link(std::string value);
std::optional<std::string> appimage_feed_github_repo(const std::string& item_text);
std::optional<std::string> appimage_feed_homepage(const std::string& item_text);
std::string appimage_catalog_page_url(const std::string& name);
std::string strip_unexpanded_url_placeholder(std::string url);
std::optional<std::string> appimage_feed_direct_download_url(const std::string& item_text);
struct AppImageCatalogSources {
    std::optional<std::string> github_repo;
    std::optional<std::string> direct_url;
    std::optional<std::string> homepage;
    bool fetched = false;
};
AppImageCatalogSources fetch_appimage_catalog_sources(
    const std::string& name,
    int timeout_ms = 15000);

std::vector<std::string> fetch_appimage_apps_list(
    int timeout_ms = kFetchAppImageGithubListTimeoutMs);
std::optional<AppImageAppsEntry> parse_appimage_apps_entry(
    const std::string& name,
    int timeout_ms = kFetchAppImageGithubEntryTimeoutMs);
std::optional<AppImageDataEntry> parse_appimage_data_entry(
    const std::string& name,
    int timeout_ms = kFetchAppImageGithubEntryTimeoutMs);
std::optional<AppImageDataEntry> lookup_appimage_data_entry(
    const std::string& package_name,
    int timeout_ms = kFetchAppImageGithubResolveTimeoutMs);
std::optional<AppImageAppsEntry> lookup_appimage_apps_entry(
    const std::string& package_name,
    int timeout_ms = kFetchAppImageGithubResolveTimeoutMs);
RepoPackage merge_apps_entry_into_package(
    const AppImageAppsEntry& entry,
    const RepoPackage& existing);
std::string resolve_gitlab_appimage_download(
    const std::string& gitlab_base,
    const std::string& project_path,
    const std::string& arch = "");
std::string resolve_gitlab_ci_artifact(
    const std::string& ci_url);
std::string yai_package_object_from_appimage_feed_item(
    const std::string& item_text,
    const std::string& id);
std::string normalize_appimage_feed_index(const std::string& feed_text);
std::string normalize_repo_source_index(const std::string& index_text);
std::string load_repo_source_text(const std::string& location);
std::vector<RepoEntry> load_repo_entries();
void write_repo_entries(const std::vector<RepoEntry>& entries);
std::string validate_repo_name(const std::string& value);
std::string validate_repo_location(const std::string& value);
void rebuild_repo_index_from_cached_files(const std::vector<RepoEntry>& entries);
std::vector<RepoPackage> find_repo_packages(const std::string& pattern);
std::optional<RepoPackage> find_repo_package(const std::string& id);
bool contains_case_insensitive(const std::string& value, const std::string& needle);
bool package_matches_keyword(const RepoPackage& package, const std::string& keyword);
int appimage_asset_score(const std::string& asset_name, const std::string& arch);
std::string github_api_base();
bool github_repo_matches_local_blocklist(const std::string& repo_target);
bool github_repo_matches_builtin_blocklist(const std::string& repo_target);
void enforce_github_release_policy(const std::string& owner, const std::string& repo);
GitHubRelease resolve_github_latest(
    const std::string& repo_target,
    const std::string& asset_pattern = "",
    const std::string& arch = "");
std::string mirror_url_for(const std::string& mirror_template, const ResolvedSource& source);
std::string download_with_strategy(
    ResolvedSource& source,
    const InstallOptions& options,
    const fs::path& target);
std::string strip_url_fragment_query(std::string value);
struct WebsiteLinkMeta {
    std::string url;
    std::optional<std::int64_t> mtime;
    int stale_penalty = 0;
};
bool website_url_looks_stale(const std::string& url);
int website_link_stale_penalty(const std::string& url);
int website_url_priority(const std::string& url);
bool website_follow_meta_better(const WebsiteLinkMeta& a, const WebsiteLinkMeta& b);
bool website_listing_follow_prune_applies(const std::vector<WebsiteLinkMeta>& follow_metas);
std::vector<WebsiteLinkMeta> select_listing_follow_metas_for_enqueue(
    const std::vector<WebsiteLinkMeta>& follow_metas,
    std::size_t non_stale_max = 3,
    std::size_t stale_max = 1);
std::optional<std::int64_t> parse_directory_listing_mtime(const std::string& text);
std::optional<std::int64_t> parse_http_last_modified_mtime(const std::string& text);
std::optional<std::int64_t> probe_url_last_modified_mtime(const std::string& url);
bool is_url_accessible(const std::string& url);
std::string url_origin(const std::string& url);
std::string url_host(const std::string& url);
bool is_file_url(const std::string& url);
bool is_appimage_catalog_url(const std::string& url);
std::string url_directory(const std::string& url);
std::string resolve_href_url(const std::string& base_url, std::string href);
std::vector<std::string> html_href_urls(const std::string& html, const std::string& base_url);
bool html_looks_like_directory_listing(const std::string& html);
std::vector<WebsiteLinkMeta> html_directory_listing_links(
    const std::string& html,
    const std::string& base_url);
std::vector<WebsiteLinkMeta> website_page_link_metas(
    const std::string& html,
    const std::string& base_url);
bool website_candidate_better(
    const WebsiteLinkMeta& a,
    const WebsiteLinkMeta& b,
    const std::string& arch);
std::string best_website_appimage_url(
    const std::vector<WebsiteLinkMeta>& candidates,
    const std::string& arch = "");
bool is_kde_stable_download_url(const std::string& url);
bool is_appimage_download_url(const std::string& url);
bool vector_contains(const std::vector<std::string>& values, const std::string& value);
bool package_name_matches_url(const RepoPackage& package, const std::string& url);
std::vector<std::string> official_download_hint_urls(const RepoPackage& package);
void add_allowed_host(std::vector<std::string>& hosts, const std::string& url);
std::vector<std::string> allowed_website_hosts(
    const RepoPackage& package,
    const std::vector<std::string>& hint_urls);
bool host_matches_allowed(const std::string& host, const std::vector<std::string>& allowed_hosts);
bool is_allowed_website_url(
    const std::string& url,
    const RepoPackage& package,
    const std::vector<std::string>& allowed_hosts,
    bool allow_package_name_match);
bool should_follow_download_page(
    const std::string& url,
    const RepoPackage& package,
    const std::vector<std::string>& allowed_hosts,
    bool allow_package_name_match);
bool is_allowed_appimage_candidate(
    const std::string& url,
    const RepoPackage& package,
    const std::vector<std::string>& allowed_hosts);
std::string best_appimage_url_from_candidates(
    const std::vector<std::string>& candidates,
    const std::string& arch = "");
bool file_looks_like_html(const fs::path& path);
std::string appimage_url_from_download_landing_html(
    const std::string& html,
    const std::string& base_url,
    const std::string& arch = "");
std::string appimage_url_from_download_landing_page(
    const fs::path& path,
    const std::string& base_url,
    const std::string& arch = "");
std::string truncate_status_text(const std::string& value, std::size_t max_size);
std::string resolve_website_appimage_download(
    const RepoPackage& package,
    const std::string& arch = "");
std::string stage_appimage_source(
    ResolvedSource& source,
    const InstallOptions& options,
    const fs::path& target);
ResolvedSource resolve_install_source(const InstallOptions& options);
ResolvedSource resolve_repo_package_install_source(const InstallOptions& options);
ResolvedSource resolve_repo_package_install_source(const InstallOptions& options, const RepoPackage& package);
bool source_uses_github_release_download(const ResolvedSource& source);
NetworkConfig prompt_china_network_config();
NetworkConfig prompt_github_release_proxy_for_this_download(NetworkConfig config);
InstallOptions apply_network_config_to_options(
    const InstallOptions& options,
    const ResolvedSource& source);
std::optional<std::string> metadata_value(const fs::path& file, const std::string& key);
fs::path readable_metadata_path(const InstallPaths& paths);
bool metadata_exists(const InstallPaths& paths);
std::string sha256_file(const fs::path& path);
bool has_image_extension(const fs::path& path);
int path_depth(const fs::path& path);
std::optional<fs::path> find_upstream_desktop_file(const fs::path& root);
fs::path desktop_temp_dir(const InstallPaths& paths);
std::optional<DesktopSource> desktop_source_from_appimage(const InstallPaths& paths);
void cleanup_desktop_source(const InstallPaths& paths, const DesktopSource& source);
std::optional<fs::path> find_icon_by_name(const fs::path& root, const std::string& icon_name);
std::optional<fs::path> find_first_icon(const fs::path& root);
std::optional<fs::path> install_desktop_icon(
    const InstallPaths& paths,
    const fs::path& root,
    const std::string& icon_name);
std::string desktop_key_base(const std::string& key);
bool is_safe_upstream_desktop_key(const std::string& key);
std::optional<std::string> read_desktop_icon_name(const fs::path& desktop_file);
void write_wrapper(const InstallPaths& paths, const std::string& mode);
void chmod_user_executable(const fs::path& path);
RepairResult extract_appimage(const InstallPaths& paths);
RepairResult detect_run_mode(const InstallPaths& paths);
std::optional<std::string> render_upstream_desktop_entry(
    const InstallPaths& paths,
    const std::string& name,
    const fs::path& root,
    const fs::path& desktop_file);
void write_desktop_entry(const InstallPaths& paths, const std::string& name);
void write_metadata(
    const InstallPaths& paths,
    const ResolvedSource& source,
    const std::string& mode);
void print_mode_line(const std::string& mode);
void print_fuse_fallback_line();
void download_app(int argc, char** argv);
void install_app(int argc, char** argv);
RepairResult repair_installed_package(const std::string& id);
void repair_app(int argc, char** argv);
fs::path previous_version_dir(const InstallPaths& paths);
void save_previous_version(const InstallPaths& paths);
void restore_previous_version(const std::string& id);
void rollback_app(int argc, char** argv);
void cleanup_update_candidate(const InstallPaths& candidate_paths);
std::vector<std::string> collect_upgradable_package_ids();
void update_app(int argc, char** argv);
void upgrade_app(int argc, char** argv);
void remove_if_exists(const fs::path& path);
void remove_app(int argc, char** argv);
void list_apps();
void search_packages(int argc, char** argv);
void info_package(int argc, char** argv);
void repo_list_app(int argc);
void repo_add_app(int argc, char** argv);
void repo_update_app(int argc, char** argv);
void repo_remove_app(int argc, char** argv);
RepoResolveOptions parse_repo_resolve_options(int argc, char** argv);
int calculate_default_concurrency(bool aggressive);
void repo_resolve_app(int argc, char** argv);
void repo_app(int argc, char** argv);
std::vector<std::string> resolve_configured_repo_names(const std::string& pattern);
void mirror_list_app(int argc);
void mirror_use_app(int argc, char** argv);
void mirror_custom_app(int argc, char** argv);
void mirror_off_app(int argc);
void mirror_app(int argc, char** argv);
void doctor_app_files(int& warnings);
void doctor_app(int argc);

// Signal / interrupt handling
void install_signal_handler();
bool was_interrupted();
void check_interrupt();
void cleanup_orphan_downloads();

// Best-effort logind "idle" inhibitor: while an instance lives, asks logind
// (via systemd-inhibit) to keep the system from auto-suspending. It is a no-op
// when systemd-inhibit is unavailable and in batch worker children (their
// parent already holds the lock). The lock is released automatically when the
// process exits, including abnormal termination (crash/SIGKILL): the inhibitor
// child blocks on a pipe whose write end lives in yai, so the kernel closes it
// on death and the lock goes away by itself.
class IdleInhibitor {
public:
    IdleInhibitor();
    ~IdleInhibitor();
    IdleInhibitor(const IdleInhibitor&) = delete;
    IdleInhibitor& operator=(const IdleInhibitor&) = delete;

private:
    int pipe_write_fd_ = -1;
    pid_t child_pid_ = -1;
};
