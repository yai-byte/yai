#include "yai.hpp"

// CLI parsing and AppImage download execution live here. Parsed options are
// intentionally reused by install and download, but download_file only stages
// bytes into a caller-provided target and reports transport failures with context.
// The higher-level download command chooses current_path()/{package.id}.AppImage
// and rejects existing targets before reaching download_file.

void print_usage() {
    std::cout
        << tr("Usage:\n")
        << tr("  yai search <keyword>\n")
        << tr("  yai info <package>\n")
        << tr("  yai repo list\n")
        << tr("  yai repo add <name> [url-or-path]\n")
        << tr("  yai repo update [name]\n")
        << tr("  yai repo remove <name-or-pattern>\n")
        << tr("  yai mirror list\n")
        << tr("  yai mirror use <ghfast|chengc|fastgit|yylx|llkk>\n")
        << tr("  yai mirror custom <xget-domain-or-template>\n")
        << tr("  yai mirror off\n")
        << tr("  yai download <package|url|owner/repo> [...]\n")
        << tr("              [--arch auto|x86_64|aarch64|x86|armv7|riscv64|ppc64le|s390x|loongarch64]\n")
        << tr("              [--download direct|mirror_first|direct_first]\n")
        << tr("              [--mirror-template <template>]\n")
        << tr("              [--downloader auto|curl|wget|wget2|aria2c]\n")
        << tr("              [--jobs <n>]\n")
        << tr("  yai install <package|path|url|owner/repo> [...] [--id <id>] [--name <name>]\n")
        << tr("              [--arch auto|x86_64|aarch64|x86|armv7|riscv64|ppc64le|s390x|loongarch64]\n")
        << tr("              [--download direct|mirror_first|direct_first]\n")
        << tr("              [--mirror-template <template>]\n")
        << tr("              [--downloader auto|curl|wget|wget2|aria2c]\n")
        << tr("              [--jobs <n>]\n")
        << tr("  yai update [id]\n")
        << tr("  yai upgrade <id|--all> [--yes] [--download direct|mirror_first|direct_first]\n")
        << tr("              [--mirror-template <template>]\n")
        << tr("              [--downloader auto|curl|wget|wget2|aria2c]\n")
        << tr("  yai rollback <id> [--yes]\n")
        << tr("  yai repair <id> [--yes]\n")
        << tr("  yai doctor\n")
        << tr("  yai remove <id> [--yes]\n")
        << tr("  yai list\n")
        << tr("  yai help\n");
    std::cout << tr("\nLanguage: follows system locale by default; set YAI_LANG=zh or YAI_LANG=en to override.\n");
    std::cout << tr("Package/id arguments support quoted * and ? patterns; supported commands may apply to multiple matches.\n");
    std::cout << tr("install/download accept multiple targets; --jobs controls parallel workers. --id/--name are single-install only.\n");
}

std::string read_option_value(int argc, char** argv, int& index, const std::string& option) {
    if (++index >= argc) {
        throw std::runtime_error(option + tr(" requires a value"));
    }
    return argv[index];
}

void parse_arch_option(InstallOptions& options, const std::string& value) {
    // The option feeds asset selection only. Runtime compatibility remains the
    // responsibility of the downloaded AppImage and host environment.
    const std::string requested = to_lower(trim(value));
    if (requested == "auto") {
        options.target_arch.clear();
    } else {
        options.target_arch = normalize_arch(requested);
        if (!is_supported_arch(options.target_arch)) {
            throw std::runtime_error(tr("--arch must be ") + supported_arch_list());
        }
    }
    options.arch_explicit = true;
}

void parse_download_strategy_option(InstallOptions& options, const std::string& value) {
    options.download_strategy = value;
    options.download_explicit = true;
    if (options.download_strategy != "direct" &&
        options.download_strategy != "mirror_first" &&
        options.download_strategy != "direct_first") {
        throw std::runtime_error(tr("--download must be direct, mirror_first, or direct_first"));
    }
}

void parse_mirror_template_option(InstallOptions& options, const std::string& value) {
    options.mirror_template = value;
    options.mirror_template_explicit = true;
}

std::string normalize_downloader(std::string value) {
    value = to_lower(trim(value));
    if (value == "aria2") {
        return "aria2c";
    }
    return value;
}

std::string supported_downloader_list() {
    return "auto, curl, wget, wget2, or aria2c";
}

void parse_downloader_option(InstallOptions& options, const std::string& value) {
    options.downloader = normalize_downloader(value);
    options.downloader_explicit = true;
    if (options.downloader != "auto" &&
        options.downloader != "curl" &&
        options.downloader != "wget" &&
        options.downloader != "wget2" &&
        options.downloader != "aria2c") {
        throw std::runtime_error(tr("--downloader must be ") + supported_downloader_list());
    }
}

void validate_mirror_options(const InstallOptions& options) {
    if (options.download_strategy != "direct" && options.mirror_template.empty()) {
        throw std::runtime_error(tr("--mirror-template is required when using a mirror download strategy"));
    }
}

bool parse_common_download_option(InstallOptions& options, int argc, char** argv, int& index, const std::string& arg) {
    if (arg == "--arch") {
        parse_arch_option(options, read_option_value(argc, argv, index, arg));
        return true;
    }
    if (arg == "--download") {
        parse_download_strategy_option(options, read_option_value(argc, argv, index, arg));
        return true;
    }
    if (arg == "--mirror-template") {
        parse_mirror_template_option(options, read_option_value(argc, argv, index, arg));
        return true;
    }
    if (arg == "--downloader") {
        parse_downloader_option(options, read_option_value(argc, argv, index, arg));
        return true;
    }
    return false;
}

void validate_install_option_text(const InstallOptions& options) {
    if (contains_line_break(options.target) ||
        contains_line_break(options.name) ||
        contains_line_break(options.target_arch) ||
        contains_line_break(options.mirror_template) ||
        contains_line_break(options.downloader)) {
        throw std::runtime_error(tr("install arguments must not contain line breaks"));
    }
}

void validate_download_option_text(const InstallOptions& options) {
    if (contains_line_break(options.target) ||
        contains_line_break(options.target_arch) ||
        contains_line_break(options.mirror_template) ||
        contains_line_break(options.downloader)) {
        throw std::runtime_error(tr("download arguments must not contain line breaks"));
    }
}

void fill_install_defaults_for_direct_target(InstallOptions& options) {
    if (looks_like_github_repo(options.target) || looks_like_repo_package_target(options.target)) {
        return;
    }

    const std::string base = basename_from_url(options.target);
    const std::string stripped = base_name_from_appimage_filename(base);
    if (options.id.empty()) {
        options.id = sanitize_id(stripped);
    }
    if (options.name.empty()) {
        options.name = stripped.empty() ? strip_appimage_suffix(base) : stripped;
    }
}

bool parse_install_identity_option(
    InstallOptions& options,
    int argc,
    char** argv,
    int& index,
    const std::string& arg) {
    if (arg == "--id") {
        options.id = sanitize_id(read_option_value(argc, argv, index, arg));
        options.id_explicit = true;
        return true;
    }
    if (arg == "--name") {
        options.name = read_option_value(argc, argv, index, arg);
        options.name_explicit = true;
        return true;
    }
    return false;
}

InstallOptions parse_install_options(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(tr("install requires a URL or owner/repo target"));
    }

    InstallOptions options;
    options.target = argv[2];

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!parse_install_identity_option(options, argc, argv, i, arg) &&
            !parse_common_download_option(options, argc, argv, i, arg)) {
            throw std::runtime_error(tr("unknown install option: ") + arg);
        }
    }

    validate_install_option_text(options);
    validate_mirror_options(options);
    fill_install_defaults_for_direct_target(options);
    return options;
}

InstallOptions parse_download_options(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(tr("download requires a URL, package, or owner/repo target"));
    }

    InstallOptions options;
    options.target = argv[2];

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!parse_common_download_option(options, argc, argv, i, arg)) {
            throw std::runtime_error(tr("unknown download option: ") + arg);
        }
    }

    validate_download_option_text(options);
    validate_mirror_options(options);
    return options;
}

bool executable_available(const std::string& name) {
    if (name.find('/') != std::string::npos) {
        return access(name.c_str(), X_OK) == 0;
    }

    const std::optional<std::string> path_env = env_string("PATH");
    if (!path_env.has_value()) {
        return false;
    }
    std::stringstream paths(*path_env);
    std::string dir;
    while (std::getline(paths, dir, ':')) {
        const fs::path candidate = fs::path(dir.empty() ? "." : dir) / name;
        if (access(candidate.c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> requested_downloader_priority(const std::string& downloader, const std::string& url) {
    const std::string normalized = normalize_downloader(downloader);
    if (normalized != "auto") {
        return {normalized};
    }
    if (is_file_url(url)) {
        return {"curl"};
    }
    return {"aria2c", "wget2", "wget", "curl"};
}

std::vector<std::string> available_downloaders(const std::string& downloader, const std::string& url) {
    const std::vector<std::string> requested = requested_downloader_priority(downloader, url);
    std::vector<std::string> available;
    for (const std::string& name : requested) {
        if (executable_available(name)) {
            available.push_back(name);
        }
    }
    if (!available.empty()) {
        return available;
    }
    if (normalize_downloader(downloader) == "auto") {
        throw std::runtime_error(tr("no supported downloader is available; install curl, wget, wget2, or aria2c"));
    }
    throw std::runtime_error(tr("requested downloader is not available: ") + requested.front());
}

std::vector<std::string> downloader_command(
    const std::string& downloader,
    const std::string& url,
    const fs::path& part,
    const fs::path& headers) {
    if (downloader == "curl") {
        return {
            "curl",
            "--fail",
            "--location",
            "--silent",
            "--show-error",
            "--retry",
            "3",
            "--continue-at",
            "-",
            "--dump-header",
            headers.string(),
            "--output",
            part.string(),
            url,
        };
    }
    if (downloader == "wget" || downloader == "wget2") {
        return {
            downloader,
            "--quiet",
            "--tries=3",
            "--continue",
            "--output-document",
            part.string(),
            url,
        };
    }
    if (downloader == "aria2c") {
        return {
            "aria2c",
            "--quiet=true",
            "--console-log-level=error",
            "--summary-interval=0",
            "--download-result=hide",
            "--auto-save-interval=1",
            "--allow-overwrite=true",
            "--auto-file-renaming=false",
            "--continue=true",
            "--file-allocation=none",
            "--max-connection-per-server=16",
            "--split=16",
            "--max-tries=3",
            "--retry-wait=1",
            "--dir",
            part.parent_path().string(),
            "--out",
            part.filename().string(),
            url,
        };
    }
    throw std::runtime_error(tr("unsupported downloader: ") + downloader);
}

void prefetch_download_headers(const std::string& url, const fs::path& headers) {
    if (is_file_url(url) || !executable_available("curl")) {
        return;
    }

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
    if (result.exit_code != 0 || result.timed_out || !download_total_from_headers(headers).has_value()) {
        const bool removed = remove_best_effort(headers);
        (void)removed;
    }
}

void remove_download_temps(const fs::path& part, const fs::path& headers) {
    const bool part_removed = remove_best_effort(part);
    const bool headers_removed = remove_best_effort(headers);
    const bool aria_removed = remove_best_effort(part.string() + ".aria2");
    if (!part_removed || !headers_removed || !aria_removed) {
        std::cerr << tr("yai: warning: failed to clean temporary download files\n");
    }
}

void run_downloader(
    const std::string& downloader,
    const std::string& url,
    const fs::path& part,
    const fs::path& headers) {
    const ProcessResult result = run_process_capture_download_progress(
        downloader_command(downloader, url, part, headers),
        part,
        headers);
    if (result.exit_code != 0) {
        const std::string detail = trim(result.output);
        throw std::runtime_error(
            downloader + tr(" failed with exit code ") + std::to_string(result.exit_code) +
            (detail.empty() ? "" : tr(": ") + detail));
    }
}

void download_file(const std::string& url, const fs::path& target, const std::string& downloader) {
    // Tool-native progress is suppressed so yai owns localized TTY progress on
    // stderr. curl also dumps headers to derive Content-Length during the same
    // transfer; other tools still report downloaded bytes and speed.
    const fs::path part = target.string() + ".part";
    const fs::path headers = target.string() + ".headers";
    const std::vector<std::string> downloaders = available_downloaders(downloader, url);

    std::string last_error;
    for (std::size_t i = 0; i < downloaders.size(); ++i) {
        const std::string& selected = downloaders[i];
        remove_required(part, tr("preparing temporary download file"));
        remove_required(headers, tr("preparing temporary download headers"));
        remove_required(part.string() + ".aria2", tr("preparing temporary aria2 state"));

        try {
            if (selected != "curl") {
                prefetch_download_headers(url, headers);
            }
            run_downloader(selected, url, part, headers);
            const bool headers_removed = remove_best_effort(headers);
            const bool aria_removed = remove_best_effort(part.string() + ".aria2");
            if (!headers_removed || !aria_removed) {
                std::cerr << tr("yai: warning: failed to clean temporary download metadata\n");
            }
            std::error_code ec;
            fs::rename(part, target, ec);
            if (ec) {
                throw std::runtime_error(tr("failed to move downloaded file into place: ") + ec.message());
            }
            return;
        } catch (const std::exception& ex) {
            last_error = selected + ": " + ex.what();
            remove_download_temps(part, headers);
            if (i + 1 < downloaders.size()) {
                std::cerr << tr("yai: downloader failed, trying next: ")
                          << last_error << "\n";
            }
        }
    }

    throw std::runtime_error(tr("all selected downloaders failed: ") + last_error);
}

namespace {

int curl_max_time_seconds(int timeout_ms) {
    if (timeout_ms <= 0) {
        return 1;
    }
    return (timeout_ms + 999) / 1000;
}

std::vector<std::string> fetch_text_curl_args(
    const std::string& url,
    int timeout_ms,
    std::optional<std::uintmax_t> max_bytes) {
    std::vector<std::string> args = {
        "curl",
        "--fail",
        "--location",
        "--silent",
        "--show-error",
        "--max-time",
        std::to_string(curl_max_time_seconds(timeout_ms)),
        "--header",
        "Accept: application/vnd.github+json",
    };
    if (max_bytes.has_value() && *max_bytes > 0) {
        args.push_back("--max-filesize");
        args.push_back(std::to_string(*max_bytes));
        if (!is_file_url(url)) {
            args.push_back("-r");
            args.push_back("0-" + std::to_string(*max_bytes - 1));
        }
    }
    args.push_back(url);
    return args;
}

}  // namespace

std::string fetch_text(const std::string& url) {
    return fetch_text(url, kFetchTextDefaultTimeoutMs);
}

std::string fetch_text(const std::string& url, int timeout_ms) {
    return fetch_text_limited(url, timeout_ms, 0);
}

std::string fetch_text_limited(
    const std::string& url,
    int timeout_ms,
    std::uintmax_t max_bytes) {
    const std::optional<std::uintmax_t> limit =
        max_bytes == 0 ? std::nullopt : std::optional<std::uintmax_t>{max_bytes};
    const ProcessResult result = run_process_capture_timeout(
        fetch_text_curl_args(url, timeout_ms, limit),
        timeout_ms + 2000,
        std::nullopt,
        {},
        static_cast<std::size_t>(max_bytes));
    if (result.output_limit_exceeded) {
        throw std::runtime_error(
            tr("failed to fetch ") + url + tr(": response exceeded maximum size"));
    }
    if (result.timed_out || result.exit_code != 0) {
        throw std::runtime_error(tr("failed to fetch ") + url + tr(": ") + result.output);
    }
    return result.output;
}
