# Final review — split resolver.cpp
# SDD Progress — split resolver.cpp

Workspace: /home/fsx/yai (in-place; no usable git)
Plan: docs/superpowers/plans/2026-07-25-split-resolver-cpp.md
Spec: docs/superpowers/specs/2026-07-25-split-resolver-cpp-design.md

Global constraints: no behavior/signature changes; no yai.hpp split; no commits; mechanical moves; no website algorithm refactor

## Tasks


Task 1: complete (review clean)

Task 2: complete (review Approved after promoting landing_html to yai.hpp)

Task 3: complete (review clean)

Task 4: complete (review Approved)

Task 5: complete (all smokes pass)

  338 src/resolver.cpp
  165 src/resolver_github.cpp
  371 src/resolver_url.cpp
  343 src/resolver_website.cpp
 1217 总计
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


src/yai.hpp:385:std::string appimage_url_from_download_landing_html(
src/resolver_website.cpp:243:            appimage_url_from_download_landing_html(fetch_text(link), link, arch);
src/resolver_url.cpp:353:std::string appimage_url_from_download_landing_html(
src/resolver_url.cpp:370:    return appimage_url_from_download_landing_html(read_text_file(path), base_url, arch);

### src/resolver.cpp
8:bool contains_case_insensitive(const std::string& value, const std::string& needle) {
9:    return to_lower(value).find(to_lower(needle)) != std::string::npos;
12:bool package_matches_keyword(const RepoPackage& package, const std::string& keyword) {
13:    if (has_glob_wildcards(keyword)) {
14:        return glob_match_case_insensitive(keyword, package.id) ||
15:               glob_match_case_insensitive(keyword, package.name) ||
16:               glob_match_case_insensitive(keyword, package.summary);
19:           contains_case_insensitive(package.id, keyword) ||
20:           contains_case_insensitive(package.name, keyword) ||
21:           contains_case_insensitive(package.summary, keyword);
24:namespace {
26:std::string install_arch_for_options(const InstallOptions& options) {
30:ResolvedSource with_install_arch(ResolvedSource source, const InstallOptions& options) {
39:std::string stage_appimage_source(
46:    if (source.source_kind == "local_path") {
47:        copy_file_overwrite(source.source_url, target);
52:    for (int redirects = 0; redirects < 3; ++redirects) {
55:            appimage_url_from_download_landing_page(target, downloaded_url, options.target_arch);
56:        if (landing_appimage_url.empty()) {
57:            if (file_looks_like_html(target)) {
59:                if (!removed) {
60:                    std::cerr << tr("yai: warning: failed to clean downloaded HTML landing page\n");
66:        if (landing_appimage_url == downloaded_url) {
70:        std::cerr << tr("yai: downloaded an AppImage landing page; following embedded AppImage link\n");
72:        if (!removed) {
### src/resolver_github.cpp
5:std::string github_api_base() {
10:    while (!base.empty() && base.back() == '/') {
16:bool github_repo_matches_local_blocklist(const std::string& repo_target) {
18:    if (!fs::exists(file)) {
22:    std::ifstream in(file);
25:    while (std::getline(in, line)) {
27:        if (line.empty() || line.front() == '#') {
30:        if (to_lower(line) == target) {
37:bool github_repo_matches_builtin_blocklist(const std::string& repo_target) {
50:    for (const std::string& term : blocked_terms) {
51:        if (lower.find(term) != std::string::npos) {
58:void enforce_github_release_policy(const std::string& owner, const std::string& repo) {
60:    if (github_repo_matches_local_blocklist(repo_target) ||
61:        github_repo_matches_builtin_blocklist(repo_target)) {
63:            tr("451 Unavailable For Legal Reasons: GitHub repository is blocked by yai policy: ") +
68:GitHubRelease resolve_github_latest(
78:    enforce_github_release_policy(owner, repo);
86:    if (!asset_pattern.empty()) {
96:    for (const std::string& url : urls) {
98:        if (asset_regex.has_value() && !std::regex_search(name, *asset_regex)) {
102:        if (score > best_score) {
108:    if (best_score < 0) {
115:std::string mirror_url_for(const std::string& mirror_template, const ResolvedSource& source) {
119:    if (scheme != std::string::npos) {
132:std::string download_with_strategy(
### src/resolver_url.cpp
5:std::string strip_url_fragment_query(std::string value) {
7:    if (fragment != std::string::npos) {
11:    if (query != std::string::npos) {
17:std::string url_origin(const std::string& url) {
19:    if (scheme == std::string::npos) {
27:std::string url_host(const std::string& url) {
29:    if (scheme == std::string::npos) {
38:    if (at != std::string::npos) {
42:    if (colon != std::string::npos) {
46:    if (host.rfind("www.", 0) == 0) {
52:bool is_file_url(const std::string& url) {
53:    return to_lower(url).rfind("file://", 0) == 0;
56:bool is_appimage_catalog_url(const std::string& url) {
58:    if (host == "appimage.github.io" ||
69:std::string url_directory(const std::string& url) {
72:    if (slash == std::string::npos) {
78:std::string resolve_href_url(const std::string& base_url, std::string href) {
80:    if (href.empty() ||
87:    if (has_url_scheme(href)) {
90:    if (href.rfind("//", 0) == 0) {
95:    if (href.front() == '/') {
96:        if (base_url.rfind("file://", 0) == 0) {
99:        return url_origin(base_url) + href;
101:    return url_directory(base_url) + href;
104:std::vector<std::string> html_href_urls(const std::string& html, const std::string& base_url) {
### src/resolver_website.cpp
5:std::string truncate_status_text(const std::string& value, std::size_t max_size) {
6:    if (value.size() <= max_size) {
9:    if (max_size <= 3) {
15:namespace {
17:class WebsiteSearchProgress {
19:    explicit WebsiteSearchProgress(const std::string& package_id)
20:        : package_id_(package_id), interactive_(isatty(STDERR_FILENO) != 0) {
23:        std::cerr << tr("yai: website search for ") << package_id_ << "\n";
26:    void queued() {
30:    void checked(const std::string& url) {
33:        render("checking");
36:    void skipped() {
38:        render("skipped");
41:    void candidate() {
43:        render("found candidate");
46:    void selected(const std::string& url) {
47:        clear_interactive_line();
48:        std::cerr << tr_format(
59:    void render(const std::string& state) {
60:        if (!interactive_) {
64:        line << tr_format(
74:        if (text.size() < last_width_) {
81:    std::string state_text(const std::string& state) const {
82:        if (state == "checking") {
83:            return tr("checking");
