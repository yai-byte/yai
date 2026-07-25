#include "yai.hpp"

// Update preview workflow (yai update). Upgrade execution lives in
// commands_upgrade.cpp; upgrade --all calls collect_upgradable_package_ids().

namespace {
struct UpdateContext {
    InstallOptions options;
    std::string id;
    InstallPaths paths;
    std::string source_kind;
    std::string current_version;
    std::string name;
    std::string source_url;
    std::string github_owner;
    std::string github_repo;
    std::string installed_arch;
};

std::string unsupported_update_reason(const std::string& source_kind) {
    if (source_kind == "local_path") {
        return tr("local AppImage path has no queryable update source");
    }
    if (source_kind == "url") {
        return tr("plain URL install has no stable update source");
    }
    if (source_kind == "unavailable") {
        return tr("package source is unavailable");
    }
    return tr("source kind is not upgradable: ") + source_kind;
}

InstallOptions update_resolution_options(const UpdateContext& context) {
    InstallOptions options = context.options;
    options.target = context.id;
    options.id = context.id;
    options.name = context.name;
    options.target_arch = context.installed_arch;
    options.id_explicit = true;
    options.name_explicit = true;
    options.arch_explicit = true;
    return options;
}

bool update_source_identity_changed(const UpdateContext& context, const ResolvedSource& source) {
    // Index URL change is enough for upgradable (basename/version may match).
    if (source.source_url != context.source_url) {
        return true;
    }
    if (!source.version.empty() && !context.current_version.empty()) {
        return source.version != context.current_version;
    }
    return false;
}

ResolvedSource resolve_repo_update_source(const UpdateContext& context) {
    ResolvedSource source = resolve_install_source(update_resolution_options(context));
    source.id = context.id;
    source.name = context.name;
    source.arch = context.installed_arch;
    return source;
}

std::vector<std::string> installed_package_ids() {
    const fs::path apps_dir = expand_home_path(".local/share/yai/apps");
    std::vector<std::string> ids;
    if (!fs::exists(apps_dir)) {
        return ids;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(apps_dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        const InstallPaths paths = paths_for(entry.path().filename().string());
        const fs::path metadata = readable_metadata_path(paths);
        if (!fs::exists(metadata)) {
            continue;
        }
        const std::string id = metadata_value(metadata, "id").value_or(entry.path().filename().string());
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string preview_value_or_dash(const std::string& value) {
    return value.empty() ? "-" : value;
}

struct UpdatePreviewResult {
    std::string id;
    std::string current_version;
    std::string latest_version;
    std::string status;
    std::string reason;
};

bool preview_result_is_upgradable(const UpdatePreviewResult& result) {
    return result.status == "upgradable";
}

void print_update_preview_row(const UpdatePreviewResult& result) {
    std::cout << result.id << "\t"
              << preview_value_or_dash(result.current_version) << "\t"
              << preview_value_or_dash(result.latest_version) << "\t"
              << result.status << "\t"
              << result.reason << "\n";
}

UpdatePreviewResult preview_from_url_freshness(
    const std::string& id,
    const std::string& current_version,
    const std::string& latest_version,
    const std::string& candidate_url,
    const fs::path& metadata) {
    if (candidate_url.empty()) {
        return UpdatePreviewResult{id, current_version, "", "error", "missing update source URL"};
    }

    const UrlFreshnessResult probe =
        probe_url_freshness(candidate_url, validators_from_metadata(metadata));
    switch (probe.status) {
    case UrlFreshness::Unchanged:
        return UpdatePreviewResult{
            id,
            current_version,
            latest_version,
            "current",
            tr("already up to date")};
    case UrlFreshness::Changed:
        return UpdatePreviewResult{
            id,
            current_version,
            latest_version,
            "upgradable",
            tr("remote content changed")};
    case UrlFreshness::Unknown:
        return UpdatePreviewResult{
            id,
            current_version,
            latest_version,
            "upgradable",
            tr("download verification required")};
    case UrlFreshness::Error:
        return UpdatePreviewResult{id, current_version, "", "error", probe.detail};
    }
    return UpdatePreviewResult{id, current_version, "", "error", probe.detail};
}

UpdatePreviewResult build_update_preview(const std::string& id) {
    const InstallPaths paths = paths_for(id);
    if (!metadata_exists(paths)) {
        throw std::runtime_error(tr("package is not installed: ") + id);
    }

    const fs::path metadata = readable_metadata_path(paths);
    const std::string source_kind = metadata_value(metadata, "source_kind").value_or("url");
    const std::string current_version = metadata_value(metadata, "version").value_or("");
    const std::string name = metadata_value(metadata, "name").value_or(id);
    const std::string source_url = metadata_value(metadata, "source_url").value_or("");
    const std::string download_url = metadata_value(metadata, "download_url").value_or("");
    const std::string github_owner = metadata_value(metadata, "github_owner").value_or("");
    const std::string github_repo = metadata_value(metadata, "github_repo").value_or("");
    const std::string installed_arch = metadata_value(metadata, "arch").value_or(current_arch());

    if (source_kind == "url") {
        const std::string candidate = !source_url.empty() ? source_url : download_url;
        return preview_from_url_freshness(id, current_version, current_version, candidate, metadata);
    }

    if (source_kind == "repo_direct_url" || source_kind == "repo_website_page") {
        try {
            UpdateContext context{
                InstallOptions{},
                id,
                paths,
                source_kind,
                current_version,
                name,
                source_url,
                github_owner,
                github_repo,
                installed_arch};
            ResolvedSource source = resolve_repo_update_source(context);
            if (update_source_identity_changed(context, source)) {
                return UpdatePreviewResult{
                    id,
                    current_version,
                    source.version,
                    "upgradable",
                    tr("source: ") + source.source_url};
            }
            if (source_kind == "repo_direct_url") {
                const std::string candidate =
                    !source.source_url.empty() ? source.source_url : source_url;
                return preview_from_url_freshness(
                    id,
                    current_version,
                    source.version,
                    candidate,
                    metadata);
            }
            return UpdatePreviewResult{
                id,
                current_version,
                source.version,
                "current",
                tr("already up to date")};
        } catch (const std::exception& ex) {
            return UpdatePreviewResult{id, current_version, "", "error", ex.what()};
        }
    }

    if ((source_kind != "github_release" && source_kind != "repo_github_release") ||
        github_owner.empty() ||
        github_repo.empty()) {
        return UpdatePreviewResult{
            id,
            current_version,
            "",
            "unsupported",
            unsupported_update_reason(source_kind)};
    }

    try {
        const GitHubRelease release = resolve_github_latest(
            github_owner + "/" + github_repo,
            "",
            installed_arch);
        if (release.tag == current_version) {
            return UpdatePreviewResult{
                id,
                current_version,
                release.tag,
                "current",
                tr("already up to date")};
        }
        return UpdatePreviewResult{
            id,
            current_version,
            release.tag,
            "upgradable",
            tr("asset: ") + release.asset.name};
    } catch (const std::exception& ex) {
        return UpdatePreviewResult{id, current_version, "", "error", ex.what()};
    }
}

std::vector<UpdatePreviewResult> build_update_previews(const std::vector<std::string>& ids) {
    std::vector<UpdatePreviewResult> results;
    results.reserve(ids.size());
    for (const std::string& id : ids) {
        results.push_back(build_update_preview(id));
    }
    return results;
}

void print_update_previews(const std::vector<UpdatePreviewResult>& results) {
    for (const UpdatePreviewResult& result : results) {
        print_update_preview_row(result);
    }
}
} // namespace

std::vector<std::string> collect_upgradable_package_ids() {
    const std::vector<UpdatePreviewResult> previews = build_update_previews(installed_package_ids());
    print_update_previews(previews);

    std::vector<std::string> upgrade_ids;
    for (const UpdatePreviewResult& result : previews) {
        if (preview_result_is_upgradable(result)) {
            upgrade_ids.push_back(result.id);
        }
    }
    return upgrade_ids;
}

void update_app(int argc, char** argv) {
    if (argc > 3) {
        throw std::runtime_error(tr("update accepts zero or one package id"));
    }

    if (argc == 3) {
        print_update_previews(build_update_previews(resolve_installed_package_ids(argv[2])));
        return;
    }

    print_update_previews(build_update_previews(installed_package_ids()));
}
