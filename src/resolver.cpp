#include "yai.hpp"

#include <chrono>
#include <functional>
#include <future>

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

struct ParallelFallbackResult {
    bool success = false;
    ResolvedSource source;
    std::exception_ptr exception;
};

using ParallelFallbackFunction = std::function<ResolvedSource()>;

ParallelFallbackResult run_fallback_async(ParallelFallbackFunction fn) {
    try {
        ParallelFallbackResult result;
        result.source = fn();
        result.success = true;
        return result;
    } catch (...) {
        ParallelFallbackResult result;
        result.exception = std::current_exception();
        return result;
    }
}

// Runs multiple fallback resolvers in parallel and returns the first successful
// result. If all fail, rethrows the last exception.
ResolvedSource resolve_parallel_fallback(
    const std::string& label,
    std::vector<ParallelFallbackFunction> fallbacks) {
    const std::size_t N = fallbacks.size();
    std::vector<std::future<ParallelFallbackResult>> futures;
    futures.reserve(N);

    for (auto& fallback : fallbacks) {
        futures.push_back(std::async(
            std::launch::async,
            [fn = std::move(fallback)]() {
                return run_fallback_async(std::move(fn));
            }));
    }

    std::exception_ptr last_exception;
    std::vector<bool> consumed(N, false);
    for (std::size_t i = 0; i < N; ++i) {
        auto status = futures[i].wait_for(std::chrono::milliseconds(kFetchTextResolveParallelWaitMs));
        if (status == std::future_status::ready) {
            auto result = futures[i].get();
            consumed[i] = true;
            if (result.success) {
                return result.source;
            }
            last_exception = result.exception;
        }
    }

    for (std::size_t i = 0; i < N; ++i) {
        if (consumed[i]) {
            continue;
        }
        auto status = futures[i].wait_for(std::chrono::milliseconds(kFetchTextFallbackTimeoutMs));
        if (status == std::future_status::ready) {
            consumed[i] = true;
            auto result = futures[i].get();
            if (result.success) {
                return result.source;
            }
            last_exception = result.exception;
        }
    }

    // Final blocking wait: some fallbacks (e.g. catalog-driven website search)
    // may take much longer than the polling timeouts above. Block until all
    // remaining futures complete so their results are not lost.
    for (std::size_t i = 0; i < N; ++i) {
        if (consumed[i]) {
            continue;
        }
        auto result = futures[i].get();
        if (result.success) {
            return result.source;
        }
        last_exception = result.exception;
    }

    if (last_exception) {
        std::rethrow_exception(last_exception);
    }
    throw std::runtime_error(tr("all fallback resolvers failed for: ") + label);
}

}  // namespace

std::string stage_appimage_source(
    ResolvedSource& source,
    const InstallOptions& options,
    const fs::path& target) {
    // Staging is shared by install, update, and download. It copies local files
    // or downloads remote sources to the caller-supplied target; command code
    // decides whether to chmod, probe, or write desktop/metadata artifacts.
    // Landing-page redirects mutate a local copy of the source URL so the
    // caller's upstream identity stays intact; validators from the final
    // successful download are copied back onto source.
    check_interrupt();
    if (source.source_kind == "local_path") {
        copy_file_overwrite(source.source_url, target);
        return source.source_url;
    }

    ResolvedSource current_source = source;
    for (int redirects = 0; redirects < 3; ++redirects) {
        check_interrupt();
        const std::string downloaded_url = download_with_strategy(current_source, options, target);
        const std::string landing_appimage_url =
            appimage_url_from_download_landing_page(target, downloaded_url, options.target_arch);
        if (landing_appimage_url.empty()) {
            if (file_looks_like_html(target)) {
                const bool removed = remove_best_effort(target);
                if (!removed) {
                    std::cerr << tr("yai: warning: failed to clean downloaded HTML landing page\n");
                }
                throw std::runtime_error(tr("downloaded URL returned an HTML page instead of an AppImage: ") + downloaded_url);
            }
            source.http_etag = current_source.http_etag;
            source.http_last_modified = current_source.http_last_modified;
            source.http_content_length = current_source.http_content_length;
            return downloaded_url;
        }
        if (landing_appimage_url == downloaded_url) {
            source.http_etag = current_source.http_etag;
            source.http_last_modified = current_source.http_last_modified;
            source.http_content_length = current_source.http_content_length;
            return downloaded_url;
        }

        std::cerr << tr("yai: downloaded an AppImage landing page; following embedded AppImage link\n");
        const bool removed = remove_best_effort(target);
        if (!removed) {
            std::cerr << tr("yai: warning: failed to clean downloaded HTML landing page before following AppImage link\n");
        }
        current_source.source_url = landing_appimage_url;
        current_source.download_url = landing_appimage_url;
    }

    throw std::runtime_error(tr("too many nested AppImage landing pages: ") + current_source.source_url);
}

namespace {

std::optional<RepoPackage> match_repo_package_for_local_stem(const std::string& stem) {
    std::vector<RepoPackage> packages;
    try {
        packages = load_repo_packages();
    } catch (const std::exception&) {
        return std::nullopt;
    }

    std::vector<const RepoPackage*> candidates;
    for (const RepoPackage& package : packages) {
        if (package.id.empty()) {
            continue;
        }
        if (stem == package.id || stem.rfind(package.id + "-", 0) == 0) {
            candidates.push_back(&package);
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }

    std::size_t best_len = 0;
    for (const RepoPackage* package : candidates) {
        best_len = std::max(best_len, package->id.size());
    }

    std::vector<const RepoPackage*> best;
    for (const RepoPackage* package : candidates) {
        if (package->id.size() == best_len) {
            best.push_back(package);
        }
    }
    if (best.size() != 1) {
        return std::nullopt;
    }
    return *best.front();
}

ResolvedSource resolve_local_install_source(const InstallOptions& options) {
    const fs::path local_path = options.target;
    if (!fs::exists(local_path)) {
        throw std::runtime_error(tr("local AppImage file does not exist: ") + options.target);
    }
    if (!fs::is_regular_file(local_path)) {
        throw std::runtime_error(tr("local AppImage target is not a file: ") + options.target);
    }

    const std::string filename = local_path.filename().string();
    const std::string full_stem_id = sanitize_id(strip_appimage_suffix(filename));
    const std::string stripped = base_name_from_appimage_filename(filename);
    const std::optional<RepoPackage> matched =
        options.id_explicit ? std::nullopt : match_repo_package_for_local_stem(full_stem_id);

    ResolvedSource source;
    source.source_kind = "local_path";
    if (options.id_explicit) {
        source.id = options.id;
    } else if (matched.has_value()) {
        source.id = sanitize_id(matched->id);
    } else {
        source.id = sanitize_id(stripped);
    }

    if (options.name_explicit) {
        source.name = options.name;
    } else if (matched.has_value()) {
        source.name = matched->name;
    } else {
        source.name = stripped.empty() ? strip_appimage_suffix(filename) : stripped;
    }

    source.source_url = fs::absolute(local_path).lexically_normal().string();
    source.download_url = source.source_url;
    return with_install_arch(source, options);
}

ResolvedSource source_from_github_release(
    const InstallOptions& options,
    const GitHubRelease& release,
    const std::string& source_kind,
    const std::string& id,
    const std::string& name) {
    // Keep source_url pointed at the upstream release asset. download_url may
    // later differ if a mirror succeeds, but update decisions still need the
    // original owner/repo/tag/asset identity.
    ResolvedSource source;
    source.source_kind = source_kind;
    source.id = id;
    source.name = name;
    source.version = release.tag;
    source.source_url = release.asset.url;
    source.download_url = release.asset.url;
    source.github_owner = release.owner;
    source.github_repo = release.repo;
    source.github_asset = release.asset.name;
    return with_install_arch(source, options);
}

ResolvedSource resolve_github_install_source(const InstallOptions& options) {
    const GitHubRelease release = resolve_github_latest(options.target, "", options.target_arch);
    return source_from_github_release(
        options,
        release,
        "github_release",
        options.id.empty() ? sanitize_id(release.repo) : options.id,
        options.name.empty() ? release.repo : options.name);
}

std::string repo_source_id(const InstallOptions& options, const RepoPackage& package) {
    return options.id_explicit ? options.id : sanitize_id(package.id);
}

std::string repo_source_name(const InstallOptions& options, const RepoPackage& package) {
    return options.name_explicit ? options.name : package.name;
}

ResolvedSource repo_direct_url_source(const InstallOptions& options, const RepoPackage& package) {
    ResolvedSource source;
    source.source_kind = "repo_direct_url";
    source.id = repo_source_id(options, package);
    source.name = repo_source_name(options, package);
    source.version = basename_from_url(package.source_url);
    source.source_url = package.source_url;
    source.download_url = package.source_url;
    return with_install_arch(source, options);
}

// Builds a "repo_website_page" resolved source from a discovered download URL.
// The download URL is used for both source_url (identity for future updates)
// and download_url (actual fetch). Consolidates the repeated construction that
// previously appeared at every website/catalog/data/apps fallback branch.
ResolvedSource make_repo_website_source(
    const InstallOptions& options,
    const RepoPackage& package,
    const std::string& download_url) {
    ResolvedSource source;
    source.source_kind = "repo_website_page";
    source.id = repo_source_id(options, package);
    source.name = repo_source_name(options, package);
    source.version = basename_from_url(download_url);
    source.source_url = download_url;
    source.download_url = download_url;
    return with_install_arch(source, options);
}

void throw_unavailable_repo_source(const RepoPackage& package) {
    throw std::runtime_error(
        tr("package is listed but has no installable AppImage source: ") +
        package.id +
        (package.source_reason.empty() ? "" : tr(" (") + package.source_reason + tr(")")));
}

ResolvedSource repo_github_release_source(const InstallOptions& options, const RepoPackage& package);

ResolvedSource repo_website_page_source(const InstallOptions& options, const RepoPackage& package) {
    const std::string arch = install_arch_for_options(options);
    const std::string source_page = package.source_url;
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    const bool use_website_resolve_cache =
        !(options.id_explicit && options.name_explicit && options.arch_explicit);

    if (use_website_resolve_cache) {
        const auto entries = load_website_resolve_cache();
        const auto hit = find_website_resolve_cache_entry(entries, package.id, arch, source_page);
        if (hit.has_value() &&
            !website_resolve_cache_entry_expired(*hit, now)) {
            // Fast path: if the listing page hasn't changed, we can trust the
            // cached download URL check. If the listing changed, skip the
            // download URL probe and go straight to re-resolving.
            const bool listing_fresh =
                website_cached_listing_fresh(source_page, hit->listing_validators);
            if (listing_fresh) {
                if (website_cached_download_url_usable(hit->download_url, hit->validators)) {
                    return make_repo_website_source(options, package, hit->download_url);
                }
            }
        }
    }

    try {
        const std::string download_url = resolve_website_appimage_download(package, options.target_arch);
        if (use_website_resolve_cache) {
            WebsiteResolveCacheEntry fresh;
            fresh.package_id = package.id;
            fresh.arch = normalize_arch(arch);
            fresh.source_url = strip_url_fragment_query(package.source_url);
            fresh.download_url = download_url;
            fresh.resolved_at = now;
            fresh.listing_validators = capture_url_validators(package.source_url);
            upsert_website_resolve_cache_entry(fresh);
        }

        return make_repo_website_source(options, package, download_url);
    } catch (const std::exception&) {
        const bool may_be_appimage_catalog =
            is_appimage_catalog_url(package.source_url) ||
            package.source_type == "website_page";
        if (!may_be_appimage_catalog) {
            throw;
        }

        std::cerr << tr("yai: website search failed; trying parallel fallbacks for ")
                  << package.name << "\n";

        // Check cached intermediate results before making network requests
        if (use_website_resolve_cache) {
            const auto entries = load_website_resolve_cache();
            const auto hit = find_website_resolve_cache_entry(entries, package.id, arch, source_page);
            if (hit.has_value() && website_intermediate_cache_valid(*hit, now)) {
                // Try to use cached catalog result
                if (hit->catalog_github_repo.has_value()) {
                    std::cerr << tr("yai: using cached AppImageHub catalog for ") << package.name << "\n";
                    RepoPackage github_package = package;
                    const std::size_t slash = hit->catalog_github_repo->find('/');
                    github_package.source_owner = hit->catalog_github_repo->substr(0, slash);
                    github_package.source_repo = hit->catalog_github_repo->substr(slash + 1);
                    github_package.source_type = "github_release";
                    github_package.asset_pattern = ".*\\.AppImage$";
                    return repo_github_release_source(options, github_package);
                }
                if (hit->catalog_direct_url.has_value()) {
                    std::cerr << tr("yai: using cached direct download URL from AppImageHub\n");
                    return make_repo_website_source(options, package, *hit->catalog_direct_url);
                }
            }
        }

        auto make_catalog_fallback = [&]() -> ResolvedSource {
            std::cerr << tr("yai: fetching AppImageHub catalog for ") << package.name << "\n";
            const AppImageCatalogSources catalog = fetch_appimage_catalog_sources(package.name);

            // Cache intermediate results
            if (use_website_resolve_cache) {
                store_website_intermediate_results(
                    package.id, arch, source_page, &catalog, nullptr, nullptr);
            }

            if (catalog.github_repo.has_value()) {
                std::cerr << tr("yai: found GitHub repo on AppImageHub: ") << *catalog.github_repo << "\n";
                RepoPackage github_package = package;
                const std::size_t slash = catalog.github_repo->find('/');
                github_package.source_owner = catalog.github_repo->substr(0, slash);
                github_package.source_repo = catalog.github_repo->substr(slash + 1);
                github_package.source_type = "github_release";
                github_package.asset_pattern = ".*\\.AppImage$";
                return repo_github_release_source(options, github_package);
            }

            if (catalog.direct_url.has_value()) {
                std::cerr << tr("yai: found direct download URL on AppImageHub\n");
                return make_repo_website_source(options, package, *catalog.direct_url);
            }

            if (catalog.homepage.has_value()) {
                std::cerr << tr("yai: found homepage on AppImageHub: ") << *catalog.homepage << "\n";
                RepoPackage homepage_package = package;
                homepage_package.source_url = *catalog.homepage;
                const std::string download_url =
                    resolve_website_appimage_download(homepage_package, options.target_arch);
                return make_repo_website_source(options, package, download_url);
            }

            throw std::runtime_error(tr("AppImageHub catalog has no usable source for ") + package.name);
        };

        auto make_data_fallback = [&, use_website_resolve_cache, package, arch, source_page]() -> ResolvedSource {
            std::cerr << tr("yai: trying AppImage GitHub data/ lookup for download fallback...\n");
            auto data_entry = lookup_appimage_data_entry(package.name);
            if (!data_entry.has_value()) {
                throw std::runtime_error(tr("no data/ entry for ") + package.name);
            }

            // Cache intermediate results
            if (use_website_resolve_cache) {
                store_website_intermediate_results(
                    package.id, arch, source_page, nullptr, &*data_entry, nullptr);
            }

            if (!data_entry->direct_url.empty()) {
                if (!is_url_accessible(data_entry->direct_url)) {
                    std::cerr << tr("yai: data/ URL is not accessible, skipping: ")
                              << data_entry->direct_url << "\n";
                    // Try to resolve GitLab CI artifact if it's a CI URL
                    if (looks_like_gitlab_url(data_entry->direct_url) &&
                        data_entry->direct_url.find("/-/jobs/") != std::string::npos) {
                        std::cerr << tr("yai: attempting GitLab CI artifact resolution...\n");
                        std::string resolved_url = resolve_gitlab_ci_artifact(data_entry->direct_url);
                        if (!resolved_url.empty()) {
                            std::cerr << tr("yai: resolved GitLab CI artifact: ") << resolved_url << "\n";

                            // Check if the resolved URL is a local file path
                            bool is_local_path = (resolved_url.find("http://") != 0 &&
                                                  resolved_url.find("https://") != 0);

                            if (is_local_path) {
                                // It's a local file path (e.g., extracted from a zip)
                                // Check if the file exists directly (not using is_url_accessible which expects URLs)
                                if (fs::exists(resolved_url)) {
                                    std::cerr << tr("yai: found working download URL from CI artifact resolution (local file)\n");
                                    ResolvedSource source;
                                    source.source_kind = "local_path";
                                    source.id = repo_source_id(options, package);
                                    source.name = repo_source_name(options, package);
                                    source.version = basename_from_url(resolved_url);
                                    source.source_url = resolved_url;
                                    source.download_url = resolved_url;
                                    return with_install_arch(source, options);
                                } else {
                                    std::cerr << tr("yai: local file does not exist: ") << resolved_url << "\n";
                                }
                            } else {
                                // It's a URL
                                if (is_url_accessible(resolved_url)) {
                                    std::cerr << tr("yai: found working download URL from CI artifact resolution\n");
                                    data_entry->direct_url = resolved_url;
                                }
                            }
                        }
                    }
                    if (!is_url_accessible(data_entry->direct_url)) {
                        // Still not accessible, fall through to try other resolution methods
                    } else {
                        std::cerr << tr("yai: found working download URL from CI artifact resolution\n");
                        return make_repo_website_source(options, package, data_entry->direct_url);
                    }
                } else {
                    std::cerr << tr("yai: found direct download URL in data/")
                              << " for " << package.name << "\n";
                    return make_repo_website_source(options, package, data_entry->direct_url);
                }
            }

            if (!data_entry->github_repo.empty()) {
                std::cerr << tr("yai: found GitHub repo in data/: ")
                          << data_entry->github_repo << "\n";
                RepoPackage github_package = package;
                const std::size_t slash = data_entry->github_repo.find('/');
                if (slash != std::string::npos) {
                    github_package.source_owner = data_entry->github_repo.substr(0, slash);
                    github_package.source_repo = data_entry->github_repo.substr(slash + 1);
                    github_package.source_type = "github_release";
                    github_package.asset_pattern = ".*\\.AppImage$";
                    return repo_github_release_source(options, github_package);
                }
            }

            if (!data_entry->gitlab_project.empty()) {
                std::cerr << tr("yai: found GitLab project in data/: ")
                          << data_entry->gitlab_project << "\n";
                // Extract the GitLab host and project path
                std::string gitlab_base = "https://gitlab.com";
                std::string project_path = data_entry->gitlab_project;
                const std::string& pp = data_entry->gitlab_project;
                std::size_t first_slash = pp.find('/');
                if (first_slash != std::string::npos && first_slash > 0 && pp.find('.') != std::string::npos && first_slash < pp.find('/')) {
                    // Looks like host/group/project (e.g. gitlab.gnome.org/GNOME/inkscape)
                    gitlab_base = "https://" + pp.substr(0, first_slash);
                    project_path = pp.substr(first_slash + 1);
                } else if (pp.find('/') != std::string::npos && pp.find('.') == std::string::npos) {
                    // Standard group/project path
                    project_path = pp;
                }

                // Use GitLab API to find AppImage downloads (reliable, unlike HTML scraping)
                std::string download_url = resolve_gitlab_appimage_download(gitlab_base, project_path, arch);

                // If GitLab API didn't find AppImages, try crawling the releases page as fallback
                if (download_url.empty()) {
                    std::cerr << tr("yai: GitLab API found no AppImages, falling back to HTML crawl\n");
                    const std::string releases_url = gitlab_base + "/" + project_path + "/-/releases";
                    RepoPackage gitlab_package = package;
                    gitlab_package.source_url = releases_url;
                    gitlab_package.homepage = releases_url;
                    gitlab_package.source_type = "website_page";
                    download_url = resolve_website_appimage_download(gitlab_package, arch);
                }

                if (download_url.empty()) {
                    throw std::runtime_error(tr("no AppImage found on GitLab for ") + package.name);
                }
                ResolvedSource source;
                source.source_kind = "repo_website_page";
                source.id = repo_source_id(options, package);
                source.name = repo_source_name(options, package);
                source.version = basename_from_url(download_url);
                source.source_url = gitlab_base + "/" + project_path + "/-/releases";
                source.download_url = download_url;
                return with_install_arch(source, options);
            }

            throw std::runtime_error(tr("data/ entry has no usable source for ") + package.name);
        };

        auto make_apps_fallback = [&, use_website_resolve_cache, package, arch, source_page]() -> ResolvedSource {
            std::cerr << tr("yai: trying AppImage GitHub apps/ lookup for GitHub repo fallback...\n");
            auto apps_entry = lookup_appimage_apps_entry(package.name);
            if (!apps_entry.has_value()) {
                throw std::runtime_error(tr("no apps/ entry for ") + package.name);
            }

            // Cache intermediate results
            if (use_website_resolve_cache) {
                store_website_intermediate_results(
                    package.id, arch, source_page, nullptr, nullptr, &*apps_entry);
            }

            if (!apps_entry->github_repo.empty()) {
                std::cerr << tr("yai: found GitHub repo in apps/: ")
                          << apps_entry->github_repo << "\n";
                RepoPackage github_package = package;
                const std::size_t slash = apps_entry->github_repo.find('/');
                if (slash != std::string::npos) {
                    github_package.source_owner = apps_entry->github_repo.substr(0, slash);
                    github_package.source_repo = apps_entry->github_repo.substr(slash + 1);
                    github_package.source_type = "github_release";
                    github_package.asset_pattern = ".*\\.AppImage$";
                    return repo_github_release_source(options, github_package);
                }
            }

            if (!apps_entry->direct_url.empty()) {
                std::cerr << tr("yai: found direct download URL in apps/")
                          << " for " << package.name << "\n";
                return make_repo_website_source(options, package, apps_entry->direct_url);
            }

            throw std::runtime_error(tr("apps/ entry has no usable source for ") + package.name);
        };

        return resolve_parallel_fallback(
            package.name,
            {make_catalog_fallback, make_data_fallback, make_apps_fallback});
    }
}

ResolvedSource repo_github_release_source(const InstallOptions& options, const RepoPackage& package) {
    const GitHubRelease release = resolve_github_latest(
        package.source_owner + "/" + package.source_repo,
        package.asset_pattern,
        options.target_arch);
    return source_from_github_release(
        options,
        release,
        "repo_github_release",
        repo_source_id(options, package),
        repo_source_name(options, package));
}

std::string repo_source_kind_for_type(const std::string& source_type) {
    if (source_type == "direct_url") {
        return "repo_direct_url";
    }
    if (source_type == "website_page") {
        return "repo_website_page";
    }
    if (source_type == "github_release") {
        return "repo_github_release";
    }
    return "repo_" + source_type;
}

ResolvedSource resolve_url_install_source(const InstallOptions& options) {
    const std::string base = basename_from_url(options.target);
    const std::string stripped = base_name_from_appimage_filename(base);
    ResolvedSource source;
    source.source_kind = "url";
    source.id = options.id.empty() ? sanitize_id(stripped) : options.id;
    source.name = options.name.empty()
        ? (stripped.empty() ? strip_appimage_suffix(base) : stripped)
        : options.name;
    source.source_url = options.target;
    source.download_url = options.target;
    return with_install_arch(source, options);
}

ResolvedSource resolve_repo_package_install_source_impl(
    const InstallOptions& options,
    const RepoPackage& package) {
    const std::string arch = install_arch_for_options(options);
    // Update/upgrade sets id/name/arch explicit (same signal that skips website
    // disk cache) and must re-resolve via original source, not a stale index URL.
    const bool prefer_index_url =
        !options.recrawl &&
        !(options.id_explicit && options.name_explicit && options.arch_explicit);
    if (prefer_index_url) {
        if (const auto url = repo_package_download_url_for_arch(package, arch)) {
            ResolvedSource source;
            source.source_kind = repo_source_kind_for_type(package.source_type);
            source.id = repo_source_id(options, package);
            source.name = repo_source_name(options, package);
            source.version = basename_from_url(*url);
            source.source_url = *url;
            source.download_url = *url;
            // Keep github_* identity for upgradeable metadata while still
            // downloading the preferred index URL directly. Mirror transport is
            // gated by source_uses_github_release_download (URL / live resolve).
            if (package.source_type == "github_release") {
                source.github_owner = package.source_owner;
                source.github_repo = package.source_repo;
                const std::string asset = basename_from_url(*url);
                if (!asset.empty()) {
                    source.github_asset = asset;
                }
            }
            return with_install_arch(source, options);
        }
    }
    if (package.source_type == "direct_url") {
        return repo_direct_url_source(options, package);
    }
    if (package.source_type == "unavailable") {
        // Before giving up, try AppImage GitHub data/ and apps/ lookups.
        // These are the last-resort fallback sources for packages that have
        // no primary download URL recorded in the local index.
        std::cerr << tr("yai: trying parallel fallbacks for unavailable package: ")
                  << package.name << "\n";

        auto make_data_fallback = [&]() -> ResolvedSource {
            std::cerr << tr("yai: trying AppImage GitHub data/ lookup for download fallback...\n");
            auto data_entry = lookup_appimage_data_entry(package.name);
            if (!data_entry.has_value()) {
                throw std::runtime_error(tr("no data/ entry for ") + package.name);
            }

            if (!data_entry->direct_url.empty()) {
                if (!is_url_accessible(data_entry->direct_url)) {
                    std::cerr << tr("yai: data/ URL is not accessible, skipping: ")
                              << data_entry->direct_url << "\n";
                    throw std::runtime_error(tr("data/ URL not accessible for ") + package.name);
                }
                std::cerr << tr("yai: found direct download URL in data/")
                          << " for " << package.name << "\n";
                ResolvedSource source;
                source.source_kind = "repo_github_release";
                source.id = repo_source_id(options, package);
                source.name = repo_source_name(options, package);
                source.version = basename_from_url(data_entry->direct_url);
                source.source_url = data_entry->direct_url;
                source.download_url = data_entry->direct_url;
                return with_install_arch(source, options);
            }

            if (!data_entry->github_repo.empty()) {
                std::cerr << tr("yai: found GitHub repo in data/: ")
                          << data_entry->github_repo << "\n";
                RepoPackage github_package = package;
                const std::size_t slash = data_entry->github_repo.find('/');
                if (slash != std::string::npos) {
                    github_package.source_owner = data_entry->github_repo.substr(0, slash);
                    github_package.source_repo = data_entry->github_repo.substr(slash + 1);
                    github_package.source_type = "github_release";
                    github_package.asset_pattern = ".*\\.AppImage$";
                    return repo_github_release_source(options, github_package);
                }
            }

            throw std::runtime_error(tr("data/ entry has no usable source for ") + package.name);
        };

        auto make_apps_fallback = [&]() -> ResolvedSource {
            std::cerr << tr("yai: trying AppImage GitHub apps/ lookup for GitHub repo fallback...\n");
            auto apps_entry = lookup_appimage_apps_entry(package.name);
            if (!apps_entry.has_value()) {
                throw std::runtime_error(tr("no apps/ entry for ") + package.name);
            }

            if (!apps_entry->github_repo.empty()) {
                std::cerr << tr("yai: found GitHub repo in apps/: ")
                          << apps_entry->github_repo << "\n";
                RepoPackage github_package = package;
                const std::size_t slash = apps_entry->github_repo.find('/');
                if (slash != std::string::npos) {
                    github_package.source_owner = apps_entry->github_repo.substr(0, slash);
                    github_package.source_repo = apps_entry->github_repo.substr(slash + 1);
                    github_package.source_type = "github_release";
                    github_package.asset_pattern = ".*\\.AppImage$";
                    return repo_github_release_source(options, github_package);
                }
            }

            if (!apps_entry->direct_url.empty()) {
                std::cerr << tr("yai: found direct download URL in apps/")
                          << " for " << package.name << "\n";
                ResolvedSource source;
                source.source_kind = "repo_github_release";
                source.id = repo_source_id(options, package);
                source.name = repo_source_name(options, package);
                source.version = basename_from_url(apps_entry->direct_url);
                source.source_url = apps_entry->direct_url;
                source.download_url = apps_entry->direct_url;
                return with_install_arch(source, options);
            }

            throw std::runtime_error(tr("apps/ entry has no usable source for ") + package.name);
        };

        try {
            return resolve_parallel_fallback(
                package.name,
                {make_data_fallback, make_apps_fallback});
        } catch (const std::exception&) {
            throw_unavailable_repo_source(package);
        }
    }
    if (package.source_type == "website_page") {
        return repo_website_page_source(options, package);
    }
    try {
        return repo_github_release_source(options, package);
    } catch (const std::exception& github_ex) {
        if (package.source_type != "github_release") {
            throw;
        }
        std::cerr << tr("yai: GitHub release resolution failed for ")
                  << package.name << tr(". Trying parallel fallbacks...\n");

        auto make_catalog_fallback = [&]() -> ResolvedSource {
            std::cerr << tr("yai: fetching AppImageHub catalog for ") << package.name << "\n";
            const AppImageCatalogSources catalog = fetch_appimage_catalog_sources(package.name);

            if (catalog.github_repo.has_value()) {
                std::cerr << tr("yai: found GitHub repo on AppImageHub: ") << *catalog.github_repo << "\n";
                RepoPackage github_package = package;
                const std::size_t slash = catalog.github_repo->find('/');
                github_package.source_owner = catalog.github_repo->substr(0, slash);
                github_package.source_repo = catalog.github_repo->substr(slash + 1);
                github_package.source_type = "github_release";
                github_package.asset_pattern = ".*\\.AppImage$";
                return repo_github_release_source(options, github_package);
            }

            if (catalog.direct_url.has_value()) {
                std::cerr << tr("yai: found direct download URL on AppImageHub\n");
                ResolvedSource source;
                source.source_kind = "repo_github_release";
                source.id = repo_source_id(options, package);
                source.name = repo_source_name(options, package);
                source.version = basename_from_url(*catalog.direct_url);
                source.source_url = *catalog.direct_url;
                source.download_url = *catalog.direct_url;
                return with_install_arch(source, options);
            }

            if (catalog.homepage.has_value()) {
                std::cerr << tr("yai: found homepage on AppImageHub: ") << *catalog.homepage << "\n";
                RepoPackage homepage_package = package;
                homepage_package.source_url = *catalog.homepage;
                const std::string download_url =
                    resolve_website_appimage_download(homepage_package, options.target_arch);
                ResolvedSource source;
                source.source_kind = "repo_github_release";
                source.id = repo_source_id(options, package);
                source.name = repo_source_name(options, package);
                source.version = basename_from_url(download_url);
                source.source_url = download_url;
                source.download_url = download_url;
                return with_install_arch(source, options);
            }

            throw std::runtime_error(tr("AppImageHub catalog has no usable source for ") + package.name);
        };

        auto make_data_fallback = [&]() -> ResolvedSource {
            std::cerr << tr("yai: trying AppImage GitHub data/ lookup for download fallback...\n");
            auto data_entry = lookup_appimage_data_entry(package.name);
            if (!data_entry.has_value()) {
                throw std::runtime_error(tr("no data/ entry for ") + package.name);
            }

            if (!data_entry->direct_url.empty()) {
                std::cerr << tr("yai: found direct download URL in data/")
                          << " for " << package.name << "\n";
                ResolvedSource source;
                source.source_kind = "repo_github_release";
                source.id = repo_source_id(options, package);
                source.name = repo_source_name(options, package);
                source.version = basename_from_url(data_entry->direct_url);
                source.source_url = data_entry->direct_url;
                source.download_url = data_entry->direct_url;
                return with_install_arch(source, options);
            }

            if (!data_entry->github_repo.empty()) {
                std::cerr << tr("yai: found GitHub repo in data/: ")
                          << data_entry->github_repo << "\n";
                RepoPackage github_package = package;
                const std::size_t slash = data_entry->github_repo.find('/');
                if (slash != std::string::npos) {
                    github_package.source_owner = data_entry->github_repo.substr(0, slash);
                    github_package.source_repo = data_entry->github_repo.substr(slash + 1);
                    github_package.source_type = "github_release";
                    github_package.asset_pattern = ".*\\.AppImage$";
                    return repo_github_release_source(options, github_package);
                }
            }

            throw std::runtime_error(tr("data/ entry has no usable source for ") + package.name);
        };

        auto make_apps_fallback = [&]() -> ResolvedSource {
            std::cerr << tr("yai: trying AppImage GitHub apps/ lookup for GitHub repo fallback...\n");
            auto apps_entry = lookup_appimage_apps_entry(package.name);
            if (!apps_entry.has_value()) {
                throw std::runtime_error(tr("no apps/ entry for ") + package.name);
            }

            if (!apps_entry->github_repo.empty()) {
                std::cerr << tr("yai: found GitHub repo in apps/: ")
                          << apps_entry->github_repo << "\n";
                RepoPackage github_package = package;
                const std::size_t slash = apps_entry->github_repo.find('/');
                if (slash != std::string::npos) {
                    github_package.source_owner = apps_entry->github_repo.substr(0, slash);
                    github_package.source_repo = apps_entry->github_repo.substr(slash + 1);
                    github_package.source_type = "github_release";
                    github_package.asset_pattern = ".*\\.AppImage$";
                    return repo_github_release_source(options, github_package);
                }
            }

            if (!apps_entry->direct_url.empty()) {
                std::cerr << tr("yai: found direct download URL in apps/")
                          << " for " << package.name << "\n";
                ResolvedSource source;
                source.source_kind = "repo_github_release";
                source.id = repo_source_id(options, package);
                source.name = repo_source_name(options, package);
                source.version = basename_from_url(apps_entry->direct_url);
                source.source_url = apps_entry->direct_url;
                source.download_url = apps_entry->direct_url;
                return with_install_arch(source, options);
            }

            throw std::runtime_error(tr("apps/ entry has no usable source for ") + package.name);
        };

        try {
            return resolve_parallel_fallback(
                package.name,
                {make_catalog_fallback, make_data_fallback, make_apps_fallback});
        } catch (const std::exception&) {
            throw std::runtime_error(
                tr("GitHub release resolution failed for '") + package.name +
                tr("' — GitHub API error: ") + github_ex.what() +
                tr(". AppImageHub, data/, and apps/ fallbacks all failed. Set YAI_GITHUB_TOKEN to avoid rate limits."));
        }
    }
}

}  // namespace

ResolvedSource resolve_repo_package_install_source(const InstallOptions& options, const RepoPackage& package) {
    return resolve_repo_package_install_source_impl(options, package);
}

ResolvedSource resolve_repo_package_install_source(const InstallOptions& options) {
    const std::optional<RepoPackage> package = find_repo_package(options.target);
    if (!package.has_value()) {
        throw std::runtime_error(tr("package not found in repo index: ") + options.target);
    }
    return resolve_repo_package_install_source(options, *package);
}

ResolvedSource resolve_install_source(const InstallOptions& options) {
    // Resolution order matters: a local AppImage named owner/repo-like or
    // package-like should still be installed as a file path before network or
    // repo lookups are attempted.
    if (looks_like_local_appimage_target(options.target)) {
        return resolve_local_install_source(options);
    }
    if (looks_like_github_repo(options.target)) {
        return resolve_github_install_source(options);
    }
    if (looks_like_repo_package_target(options.target)) {
        return resolve_repo_package_install_source(options);
    }
    return resolve_url_install_source(options);
}

bool source_uses_github_release_download(const ResolvedSource& source) {
    // Prefer-index may attach github_* for upgrade metadata while downloading a
    // direct non-GitHub index URL. Only treat as GitHub Release transport when
    // the URL itself targets GitHub, or when this is a live owner/repo resolve
    // (source_kind github_release) that may use non-GitHub asset URLs in tests.
    const std::string lower = to_lower(source.source_url);
    if (lower.find("github.com/") != std::string::npos ||
        lower.find("githubusercontent.com/") != std::string::npos) {
        return true;
    }
    return source.source_kind == "github_release" && !source.github_owner.empty();
}

NetworkConfig prompt_china_network_config() {
    // The prompt configures a convenience proxy for public GitHub Release
    // downloads. It does not rewrite repository ownership, source_kind, or other
    // metadata fields used for future update decisions.
    NetworkConfig config;
    config.exists = true;
    config.prompted = true;
    config.provider = "direct";
    config.download_strategy = "direct";

    std::cout << tr("China network optimization\n");
    std::cout << china_network_disclaimer() << "\n\n";
    std::cout << tr("GitHub direct download is the default. Enable a third-party proxy for public GitHub Release downloads? [y/N] ");

    std::string answer;
    std::getline(std::cin, answer);
    answer = to_lower(trim(answer));
    if (answer != "y" && answer != "yes") {
        write_network_config(config);
        return config;
    }

    std::cout << tr("Choose a proxy provider:\n");
    const std::vector<MirrorProvider> providers = built_in_mirror_providers();
    for (std::size_t i = 0; i < providers.size(); ++i) {
        std::cout << tr_format(
            "  {number}. {name} - {description}\n",
            {{"{number}", std::to_string(i + 1)},
             {"{name}", providers[i].name},
             {"{description}", tr(providers[i].description)}});
    }
    std::cout << tr_format(
        "  {number}. custom - enter an Xget domain or template\n",
        {{"{number}", std::to_string(providers.size() + 1)}});
    std::cout << tr_format(
        "Choice [1-{max}]: ",
        {{"{max}", std::to_string(providers.size() + 1)}});

    std::string choice_text;
    std::getline(std::cin, choice_text);
    const int choice = std::atoi(choice_text.c_str());
    if (choice >= 1 && static_cast<std::size_t>(choice) <= providers.size()) {
        const MirrorProvider provider = providers[static_cast<std::size_t>(choice - 1)];
        config.provider = provider.name;
        config.download_strategy = "mirror_first";
        config.mirror_template = provider.mirror_template;
    } else if (choice == static_cast<int>(providers.size() + 1)) {
        std::cout << tr("Enter an Xget domain or mirror template (supports {raw_url}, {raw_url_noscheme}, {url}, {owner}, {repo}, {tag}, {asset}): ");
        std::string custom;
        std::getline(std::cin, custom);
        config.provider = "custom";
        config.download_strategy = "mirror_first";
        config.mirror_template = normalize_custom_mirror_template(custom);
    } else {
        config.provider = "direct";
        config.download_strategy = "direct";
    }

    write_network_config(config);
    return config;
}

NetworkConfig prompt_github_release_proxy_for_this_download(NetworkConfig config) {
    if (!config.exists || config.provider == "direct" || config.mirror_template.empty()) {
        return prompt_china_network_config();
    }

    std::cout << tr_format(
        "Use proxy {provider} for this GitHub Release download? [Y/n] ",
        {{"{provider}", config.provider}});
    std::string answer;
    std::getline(std::cin, answer);
    answer = to_lower(trim(answer));
    if (answer == "n" || answer == "no") {
        NetworkConfig direct;
        direct.exists = true;
        direct.prompted = true;
        direct.provider = "direct";
        direct.download_strategy = "direct";
        return direct;
    }
    config.download_strategy = "mirror_first";
    return config;
}

InstallOptions apply_network_config_to_options(
    const InstallOptions& options,
    const ResolvedSource& source) {
    InstallOptions effective = options;
    if (!source_uses_github_release_download(source) ||
        options.download_explicit ||
        options.mirror_template_explicit) {
        return effective;
    }

    NetworkConfig config = load_network_config();
    if (!env_string("YAI_BATCH_CHILD").has_value() && isatty(STDIN_FILENO)) {
        config = prompt_github_release_proxy_for_this_download(config);
    }
    if (config.download_strategy != "direct" && !config.mirror_template.empty()) {
        effective.download_strategy = "mirror_first";
        effective.mirror_template = config.mirror_template;
    }
    return effective;
}
