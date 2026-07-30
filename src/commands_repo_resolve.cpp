#include "yai.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <future>
#include <mutex>
#include <thread>

namespace {

constexpr const char* kDefaultResolveTypes[] = {
    "github_release",
    "website_page",
    "direct_url",
};

void parse_show_mask(const std::string& value, RepoResolveOptions& options) {
    if (value.size() != 3 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch == '0' || ch == '1';
        })) {
        throw std::runtime_error(tr("--show must be three digits of 0 or 1"));
    }
    options.show_success = value[0] == '1';
    options.show_skip = value[1] == '1';
    options.show_fail = value[2] == '1';
}

int parse_concurrency_value(const std::string& value) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        throw std::runtime_error(tr("--concurrency must be a positive integer"));
    }
    const unsigned long parsed = std::stoul(value);
    if (parsed == 0 || parsed > 32) {
        throw std::runtime_error(tr("--concurrency must be between 1 and 32"));
    }
    return static_cast<int>(parsed);
}

std::vector<std::string> resolve_arches(const RepoResolveOptions& options) {
    if (options.arches.empty()) {
        return {current_arch()};
    }
    for (const std::string& arch : options.arches) {
        if (to_lower(trim(arch)) == "all") {
            return canonical_arches();
        }
    }
    std::vector<std::string> arches;
    arches.reserve(options.arches.size());
    for (const std::string& arch : options.arches) {
        const std::string normalized = normalize_arch(arch);
        if (!is_supported_arch(normalized)) {
            throw std::runtime_error(tr("--arch must be ") + supported_arch_list() + tr(", or all"));
        }
        arches.push_back(normalized);
    }
    return arches;
}

bool package_type_selected(const RepoResolveOptions& options, const std::string& source_type) {
    if (options.types.empty()) {
        for (const char* allowed : kDefaultResolveTypes) {
            if (source_type == allowed) {
                return true;
            }
        }
        return false;
    }
    return std::find(options.types.begin(), options.types.end(), source_type) != options.types.end();
}

bool package_id_selected(const RepoResolveOptions& options, const std::string& id) {
    if (options.packages.empty()) {
        return true;
    }
    return std::find(options.packages.begin(), options.packages.end(), id) != options.packages.end();
}

struct ResolveLine {
    enum class Kind { Success, Skip, Fail };
    Kind kind = Kind::Fail;
    std::string text;
};

struct ResolveCounters {
    std::size_t resolved = 0;
    std::size_t skipped = 0;
    std::size_t failed = 0;
};

// Returns true when at least one arch wrote a download URL into `package`.
bool resolve_one_package(
    RepoPackage& package,
    const std::vector<std::string>& arches,
    bool overwrite,
    ResolveCounters& counters,
    std::vector<ResolveLine>& lines,
    std::mutex* mutex) {
    const std::string host_arch = current_arch();
    bool wrote_url = false;

    struct ArchResult {
        std::string arch;
        bool skip = false;
        ResolvedSource source;
        std::exception_ptr exception;
        bool success = false;
    };

    std::vector<ArchResult> results(arches.size());
    std::vector<std::future<void>> tasks;

    for (std::size_t arch_index = 0; arch_index < arches.size(); ++arch_index) {
        results[arch_index].arch = arches[arch_index];
        const std::string& arch = arches[arch_index];

        if (!overwrite && repo_package_has_download_url_for_arch(package, arch)) {
            results[arch_index].skip = true;
            continue;
        }

        tasks.push_back(std::async(std::launch::async,
            [&package, arch, arch_index, &results]() {
                try {
                    InstallOptions opt;
                    opt.target = package.id;
                    opt.target_arch = arch;
                    opt.arch_explicit = true;
                    opt.recrawl = true;
                    results[arch_index].source = resolve_repo_package_install_source(opt, package);
                    results[arch_index].success = true;
                } catch (...) {
                    results[arch_index].exception = std::current_exception();
                }
            }));
    }

    for (auto& task : tasks) {
        task.get();
    }

    for (std::size_t arch_index = 0; arch_index < arches.size(); ++arch_index) {
        const auto& result = results[arch_index];
        const std::string& arch = result.arch;

        if (result.skip) {
            ResolveLine line;
            line.kind = ResolveLine::Kind::Skip;
            line.text = tr_format("skip {id} {arch}", {{"{id}", package.id}, {"{arch}", arch}});
            if (mutex != nullptr) {
                std::lock_guard<std::mutex> lock(*mutex);
                ++counters.skipped;
                lines.push_back(std::move(line));
            } else {
                ++counters.skipped;
                lines.push_back(std::move(line));
            }
            continue;
        }

        if (result.success) {
            const bool mirror =
                arch_index == 0 || normalize_arch(arch) == normalize_arch(host_arch);
            if (mutex != nullptr) {
                std::lock_guard<std::mutex> lock(*mutex);
                repo_package_set_download_url(
                    package,
                    arch,
                    result.source.download_url,
                    overwrite,
                    mirror);
            } else {
                repo_package_set_download_url(
                    package,
                    arch,
                    result.source.download_url,
                    overwrite,
                    mirror);
            }
            wrote_url = true;

            ResolveLine line;
            line.kind = ResolveLine::Kind::Success;
            line.text = tr_format(
                "ok {id} {arch} {url}",
                {{"{id}", package.id}, {"{arch}", arch}, {"{url}", result.source.download_url}});
            if (mutex != nullptr) {
                std::lock_guard<std::mutex> lock(*mutex);
                ++counters.resolved;
                lines.push_back(std::move(line));
            } else {
                ++counters.resolved;
                lines.push_back(std::move(line));
            }
        } else {
            std::string error_msg;
            try {
                if (result.exception) {
                    std::rethrow_exception(result.exception);
                }
                error_msg = tr("unknown error");
            } catch (const std::exception& ex) {
                error_msg = ex.what();
            }

            ResolveLine line;
            line.kind = ResolveLine::Kind::Fail;
            line.text = tr_format(
                "fail {id} {arch}: {error}",
                {{"{id}", package.id}, {"{arch}", arch}, {"{error}", error_msg}});
            if (mutex != nullptr) {
                std::lock_guard<std::mutex> lock(*mutex);
                ++counters.failed;
                lines.push_back(std::move(line));
            } else {
                ++counters.failed;
                lines.push_back(std::move(line));
            }
        }
    }

    return wrote_url;
}

void print_resolve_results(const RepoResolveOptions& options, const std::vector<ResolveLine>& lines) {
    for (const ResolveLine& line : lines) {
        const bool show =
            (line.kind == ResolveLine::Kind::Success && options.show_success) ||
            (line.kind == ResolveLine::Kind::Skip && options.show_skip) ||
            (line.kind == ResolveLine::Kind::Fail && options.show_fail);
        if (show) {
            std::cout << line.text << "\n";
        }
    }
}

void print_resolve_summary(const ResolveCounters& counters) {
    std::cout << tr_format(
        "resolved: {resolved} skipped: {skipped} failed: {failed}\n",
        {{"{resolved}", std::to_string(counters.resolved)},
         {"{skipped}", std::to_string(counters.skipped)},
         {"{failed}", std::to_string(counters.failed)}});
}

} // namespace

int calculate_default_concurrency(bool aggressive) {
    const unsigned int hardware = std::thread::hardware_concurrency();
    const int cores = hardware == 0 ? 4 : static_cast<int>(hardware);

    if (aggressive) {
        // Aggressive mode: 2x CPU cores, clamped to 16 max
        return std::min(cores * 2, 16);
    } else {
        // Normal mode: CPU cores, clamped to 8 max
        return std::min(cores, 8);
    }
}

RepoResolveOptions parse_repo_resolve_options(int argc, char** argv) {
    RepoResolveOptions options;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output") {
            options.output = read_option_value(argc, argv, i, arg);
        } else if (arg == "--arch") {
            options.arches.push_back(read_option_value(argc, argv, i, arg));
        } else if (arg == "--type") {
            options.types.push_back(read_option_value(argc, argv, i, arg));
        } else if (arg == "--package") {
            options.packages.push_back(read_option_value(argc, argv, i, arg));
        } else if (arg == "--overwrite") {
            options.overwrite = true;
        } else if (arg == "--concurrency") {
            options.concurrency = parse_concurrency_value(read_option_value(argc, argv, i, arg));
        } else if (arg == "--aggressive") {
            options.aggressive = true;
        } else if (arg == "--show") {
            parse_show_mask(read_option_value(argc, argv, i, arg), options);
        } else if (arg == "--summary") {
            options.summary = true;
        } else if (arg == "--no-summary") {
            options.summary = false;
        } else {
            throw std::runtime_error(tr("unknown repo resolve option: ") + arg);
        }
    }

    // Calculate effective concurrency: if 0 (auto), compute based on CPU cores and aggressive flag
    if (options.concurrency <= 0) {
        options.concurrency = calculate_default_concurrency(options.aggressive);
    }

    return options;
}

void repo_resolve_app(int argc, char** argv) {
    const RepoResolveOptions options = parse_repo_resolve_options(argc, argv);
    const std::vector<std::string> arches = resolve_arches(options);

    std::vector<RepoPackage> packages = load_repo_packages();
    std::vector<std::size_t> selected;
    selected.reserve(packages.size());
    for (std::size_t i = 0; i < packages.size(); ++i) {
        if (!package_id_selected(options, packages[i].id)) {
            continue;
        }
        if (!package_type_selected(options, packages[i].source_type)) {
            continue;
        }
        selected.push_back(i);
    }

    ResolveCounters counters;
    std::vector<ResolveLine> lines;
    std::vector<std::size_t> updated_indices;

    if (options.concurrency <= 1 || selected.size() <= 1) {
        for (const std::size_t index : selected) {
            if (resolve_one_package(
                    packages[index],
                    arches,
                    options.overwrite,
                    counters,
                    lines,
                    nullptr)) {
                updated_indices.push_back(index);
            }
        }
    } else {
        std::mutex mutex;
        std::atomic<std::size_t> next{0};
        const int workers = std::min(options.concurrency, static_cast<int>(selected.size()));
        std::vector<std::future<void>> tasks;
        tasks.reserve(static_cast<std::size_t>(workers));
        for (int w = 0; w < workers; ++w) {
            tasks.push_back(std::async(std::launch::async, [&]() {
                while (true) {
                    const std::size_t pos = next.fetch_add(1);
                    if (pos >= selected.size()) {
                        return;
                    }
                    const std::size_t index = selected[pos];
                    if (resolve_one_package(
                            packages[index],
                            arches,
                            options.overwrite,
                            counters,
                            lines,
                            &mutex)) {
                        std::lock_guard<std::mutex> lock(mutex);
                        updated_indices.push_back(index);
                    }
                }
            }));
        }
        for (auto& task : tasks) {
            task.get();
        }
    }

    // Save the full combined index first so upsert can match ids, then patch
    // each named repo cache that contains a successfully updated package.
    if (repo_index_is_locally_writable()) {
        save_repo_packages_index(packages, repo_index_path());
        for (const std::size_t index : updated_indices) {
            upsert_repo_package_download_urls(packages[index]);
        }
    }
    if (options.output.has_value()) {
        save_repo_packages_index(packages, *options.output);
    }

    print_resolve_results(options, lines);
    if (options.summary) {
        print_resolve_summary(counters);
    }
    if (counters.failed > 0) {
        throw std::runtime_error(tr("repo resolve completed with failures"));
    }
}
