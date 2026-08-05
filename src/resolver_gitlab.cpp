#include "yai.hpp"

#include <fstream>
#include <system_error>

// GitLab-specific resolution helpers, split from resolver.cpp to keep that
// file focused on install-source orchestration. Two public entry points
// (declared in yai.hpp) live here:
//   - resolve_gitlab_appimage_download: queries the GitLab releases API for
//     AppImage assets exposed via a project's release links.
//   - resolve_gitlab_ci_artifact: re-resolves an expired CI artifact URL by
//     walking the project's latest successful pipeline and re-downloading the
//     AppImage-bearing job's artifact zip.

// Resolve AppImage download URL from a GitLab project using the GitLab API.
// GitLab releases pages are JavaScript-rendered, so HTML scraping cannot find
// download links. The GitLab API returns structured data about release assets
// including external download URLs.
std::string resolve_gitlab_appimage_download(
    const std::string& gitlab_base,
    const std::string& project_path,
    const std::string& arch) {

    const std::string effective_arch = arch.empty() ? current_arch() : normalize_arch(arch);

    // URL-encode the project path for the API URL
    std::string encoded_path = url_encode(project_path);
    std::string api_url = gitlab_base + "/api/v4/projects/" + encoded_path + "/releases?per_page=10";

    std::cerr << "yai: querying GitLab API: " << api_url << "\n";

    std::string api_response;
    try {
        api_response = fetch_text(api_url, 15000);
    } catch (const std::exception& e) {
        std::cerr << "yai: GitLab API query failed: " << e.what() << "\n";
        return "";
    }

    if (api_response.empty()) {
        std::cerr << "yai: GitLab API returned empty response\n";
        return "";
    }

    // Parse the JSON array of releases
    auto releases = json_top_level_objects(api_response);
    if (releases.empty()) {
        std::cerr << "yai: GitLab API returned no releases\n";
        return "";
    }

    std::cerr << "yai: GitLab API returned " << releases.size() << " releases\n";

    std::string best_url;
    int best_score = -1;

    for (const auto& release_json : releases) {
        std::string release_name = json_find_string(release_json, "name").value_or("");
        std::string tag_name = json_find_string(release_json, "tag_name").value_or("");
        std::cerr << "yai: checking GitLab release: " << release_name
                  << " (tag: " << tag_name << ")\n";

        // Collect candidate URLs from the release:
        // 1. assets.links[].url / direct_asset_url
        // 2. assets.sources[].url (for archive sources)
        // 3. Description text (may contain markdown links to AppImages)

        auto links_array = json_find_array(release_json, "links");
        if (links_array.has_value()) {
            auto link_objects = json_top_level_objects(*links_array);
            for (const auto& link_json : link_objects) {
                std::string url = json_find_string(link_json, "url").value_or("");
                std::string direct_url = json_find_string(link_json, "direct_asset_url").value_or("");
                std::string name = json_find_string(link_json, "name").value_or("");

                std::string candidate_url = !direct_url.empty() ? direct_url : url;
                if (candidate_url.empty()) continue;

                // Check if this looks like an AppImage download
                if (is_appimage_download_url(candidate_url)) {
                    int score = appimage_asset_score(basename_from_url(candidate_url), effective_arch);
                    std::cerr << "yai:   found AppImage link: " << candidate_url
                              << " (score=" << score << ", name=" << name << ")\n";
                    if (score > best_score) {
                        best_score = score;
                        best_url = candidate_url;
                    }
                }
            }
        }

        // Also check the description for markdown-style AppImage links
        std::string description = json_find_string(release_json, "description").value_or("");
        if (!description.empty()) {
            // Look for markdown links: [text](url) where url contains .AppImage
            std::size_t pos = 0;
            while ((pos = description.find(".AppImage", pos)) != std::string::npos) {
                // Find the URL containing this - look for [text](url) patterns
                std::size_t url_start = description.rfind("](", pos);
                if (url_start == std::string::npos || url_start < pos - 200) {
                    pos++;
                    continue;
                }
                std::size_t paren_start = url_start + 2;
                std::size_t paren_end = description.find(')', paren_start);
                if (paren_end == std::string::npos) {
                    pos++;
                    continue;
                }
                std::string url = description.substr(paren_start, paren_end - paren_start);
                if (is_appimage_download_url(url)) {
                    int score = appimage_asset_score(basename_from_url(url), effective_arch);
                    std::cerr << "yai:   found AppImage in description: " << url
                              << " (score=" << score << ")\n";
                    if (score > best_score) {
                        best_score = score;
                        best_url = url;
                    }
                }
                pos = paren_end + 1;
            }
        }

        // If we found a good candidate, we can stop checking older releases
        if (best_score >= 100) {
            std::cerr << "yai: found high-confidence AppImage on GitLab: " << best_url << "\n";
            break;
        }
    }

    if (!best_url.empty()) {
        std::cerr << "yai: GitLab resolved AppImage: " << best_url << " (score=" << best_score << ")\n";
    } else {
        std::cerr << "yai: no AppImage found in GitLab releases\n";
    }

    return best_url;
}

namespace {

// Parsed components of a GitLab CI artifact URL.
// Format: https://host/group/project/-/jobs/JOB_ID/artifacts/raw/FILENAME
struct GitLabCiUrl {
    std::string base;          // e.g. "https://gitlab.com"
    std::string project_path;  // e.g. "GNOME/inkscape"
    std::string job_id;        // JOB_ID segment (kept for logging only)
    std::string filename;      // requested filename (kept for logging only)
};

std::optional<GitLabCiUrl> parse_gitlab_ci_url(const std::string& ci_url) {
    const std::string lower = to_lower(ci_url);
    const std::size_t jobs_pos = lower.find("/-/jobs/");
    if (jobs_pos == std::string::npos) {
        return std::nullopt;
    }

    // Extract the host (base URL).
    std::string base;
    if (lower.find("gitlab.com/") != std::string::npos) {
        base = "https://gitlab.com";
    } else {
        const std::size_t scheme_end = ci_url.find("://");
        if (scheme_end == std::string::npos) {
            return std::nullopt;
        }
        const std::size_t host_start = scheme_end + 3;
        const std::size_t host_end = ci_url.find('/', host_start);
        if (host_end == std::string::npos) {
            return std::nullopt;
        }
        base = ci_url.substr(0, host_end);
    }
    if (base.empty()) {
        return std::nullopt;
    }

    // Extract project path between host and "/-/jobs/".
    const std::size_t path_start = ci_url.find('/', base.size());
    if (path_start == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t path_end = ci_url.find("/-/jobs/", path_start);
    if (path_end == std::string::npos) {
        return std::nullopt;
    }
    const std::string project_path = ci_url.substr(path_start + 1, path_end - path_start - 1);

    // Extract job ID and filename after "/-/jobs/".
    const std::string jobs_part = ci_url.substr(jobs_pos + 8);  // skip "/-/jobs/"
    const std::size_t artifacts_pos = jobs_part.find("/artifacts/raw/");
    if (artifacts_pos == std::string::npos) {
        return std::nullopt;
    }
    const std::string job_id = jobs_part.substr(0, artifacts_pos);
    std::string filename = jobs_part.substr(artifacts_pos + 14);  // skip "/artifacts/raw/"
    // Remove leading '/' if present (URL parsing edge cases).
    while (!filename.empty() && filename.front() == '/') {
        filename.erase(filename.begin());
    }

    GitLabCiUrl result;
    result.base = base;
    result.project_path = project_path;
    result.job_id = job_id;
    result.filename = filename;
    return result;
}

// Fetches the default branch of a GitLab project via the API.
// Returns empty string on any failure.
std::string fetch_gitlab_default_branch(const std::string& base, const std::string& encoded_path) {
    const std::string project_api = base + "/api/v4/projects/" + encoded_path;
    std::string project_json;
    try {
        project_json = fetch_text(project_api, 10000);
    } catch (...) {
        return "";
    }
    if (project_json.empty()) return "";

    std::string default_branch = json_find_string(project_json, "default_branch").value_or("main");
    if (default_branch.empty()) default_branch = "main";
    std::cerr << "yai: GitLab project default branch: " << default_branch << "\n";
    return default_branch;
}

// Fetches the latest successful pipeline ID for a branch via the GitLab API.
// Returns empty string on any failure.
std::string fetch_gitlab_latest_pipeline_id(
    const std::string& base, const std::string& encoded_path, const std::string& branch) {
    const std::string pipelines_api = base + "/api/v4/projects/" + encoded_path +
        "/pipelines?ref=" + url_encode(branch) + "&status=success&per_page=1";
    std::string pipelines_json;
    try {
        pipelines_json = fetch_text(pipelines_api, 10000);
    } catch (...) {
        std::cerr << "yai: failed to fetch pipelines from " << pipelines_api << "\n";
        return "";
    }
    if (pipelines_json.empty()) {
        std::cerr << "yai: pipelines API returned empty response\n";
        return "";
    }

    const auto pipelines = json_top_level_objects(pipelines_json);
    if (pipelines.empty()) {
        std::cerr << "yai: no successful pipelines found\n";
        return "";
    }

    std::string pipeline_id = json_find_string(pipelines[0], "id").value_or("");
    if (pipeline_id.empty()) {
        // GitLab API returns IDs as numbers; json_find_string only accepts quoted strings.
        pipeline_id = json_find_number_as_string(pipelines[0], "id").value_or("");
    }
    std::cerr << "yai: latest successful pipeline: " << pipeline_id << "\n";
    return pipeline_id;
}

struct GitLabJob {
    std::string id;
    std::string name;
};

// Fetches all AppImage-named, successful jobs from a pipeline.
// Returns empty vector on API failure or no matching jobs.
std::vector<GitLabJob> fetch_gitlab_appimage_jobs(
    const std::string& base, const std::string& encoded_path, const std::string& pipeline_id) {
    std::vector<GitLabJob> result;
    const std::string jobs_api = base + "/api/v4/projects/" + encoded_path +
        "/pipelines/" + pipeline_id + "/jobs?per_page=100";
    std::string jobs_json;
    try {
        jobs_json = fetch_text(jobs_api, 10000);
    } catch (...) {
        std::cerr << "yai: failed to fetch jobs from " << jobs_api << "\n";
        return result;
    }
    if (jobs_json.empty()) {
        std::cerr << "yai: jobs API returned empty response\n";
        return result;
    }

    for (const auto& job_json : json_top_level_objects(jobs_json)) {
        const std::string job_status = json_find_string(job_json, "status").value_or("");
        if (job_status != "success" && job_status != "completed") {
            continue;
        }
        const std::string job_name = json_find_string(job_json, "name").value_or("");
        if (to_lower(job_name).find("appimage") == std::string::npos) {
            continue;
        }
        std::string job_id = json_find_string(job_json, "id").value_or("");
        if (job_id.empty()) {
            // GitLab API returns IDs as numbers; json_find_string only accepts quoted strings.
            job_id = json_find_number_as_string(job_json, "id").value_or("");
        }
        if (job_id.empty()) {
            continue;
        }
        std::cerr << "yai: found AppImage CI job: " << job_name << " (id=" << job_id << ")\n";
        result.push_back(GitLabJob{job_id, job_name});
    }
    return result;
}

// Returns true if the file at `path` starts with the ZIP magic bytes ("PK").
bool file_has_zip_magic(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    char magic[2] = {0, 0};
    if (!f.read(magic, 2)) {
        return false;
    }
    return magic[0] == 'P' && magic[1] == 'K';
}

// Finds the first regular file with an .AppImage extension under `root`.
// Returns an empty path if none is found.
fs::path find_appimage_under(const fs::path& root) {
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(
            root, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const fs::path& p = entry.path();
        if (p.extension() == ".AppImage") {
            return p;
        }
    }
    return {};
}

// Downloads a single job's artifact ZIP and extracts any AppImage from it.
// Returns the local path to the AppImage (or the ZIP if no AppImage is inside)
// on success, or an empty string on failure so the caller can try the next job.
std::string download_and_extract_gitlab_artifact(
    const std::string& base, const std::string& project_path, const GitLabJob& job) {
    const std::string download_url = base + "/" + project_path +
        "/-/jobs/" + job.id + "/artifacts/download?job=" + url_encode(job.name);
    std::cerr << "yai: downloading artifact from job " << job.name << ": " << download_url << "\n";

    const fs::path temp_dir = fs::path("/tmp") / ("yai_gitlab_artifact_" + job.id);
    const fs::path artifact_path = temp_dir / "artifact.zip";

    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
        std::cerr << "yai: failed to create temp directory: " << temp_dir
                  << " (" << ec.message() << ")\n";
        return "";
    }

    try {
        download_file(download_url, artifact_path, /*downloader=*/"");
    } catch (const std::exception& ex) {
        std::cerr << "yai: download failed for job " << job.name << ": " << ex.what() << "\n";
        return "";
    }

    // Verify the download is non-empty.
    std::error_code size_ec;
    const auto file_size = fs::file_size(artifact_path, size_ec);
    if (size_ec || file_size == 0) {
        std::cerr << "yai: downloaded artifact is missing or empty: " << artifact_path << "\n";
        return "";
    }
    std::cerr << "yai: downloaded artifact from job " << job.name
              << " (" << file_size << " bytes)\n";

    // If the artifact isn't a ZIP, it's likely a direct AppImage.
    if (!file_has_zip_magic(artifact_path)) {
        std::cerr << "yai: artifact is not a zip, returning as direct download: " << artifact_path << "\n";
        return artifact_path.string();
    }

    // Extract the ZIP via the system unzip tool.
    const ProcessResult unzip_result = run_process_capture(
        {"unzip", "-o", artifact_path.string(), "-d", temp_dir.string()});
    if (unzip_result.exit_code != 0) {
        std::cerr << "yai: unzip failed for job " << job.name << ": " << unzip_result.output << "\n";
        return "";
    }

    // Locate the extracted AppImage.
    const fs::path appimage_path = find_appimage_under(temp_dir);
    if (!appimage_path.empty()) {
        std::cerr << "yai: extracted AppImage: " << appimage_path << "\n";
        return appimage_path.string();
    }

    // No AppImage inside the zip; return the zip path as a fallback.
    std::cerr << "yai: no AppImage found in zip, returning zip path: " << artifact_path << "\n";
    return artifact_path.string();
}

}  // namespace

// Resolve a GitLab CI artifact URL that may have expired.
// CI job artifacts expire after a certain time (typically 30 days on GitLab.com).
// This function finds the latest pipeline for the project and tries to locate
// the same artifact file in the latest successful job.
std::string resolve_gitlab_ci_artifact(const std::string& ci_url) {
    const auto parsed = parse_gitlab_ci_url(ci_url);
    if (!parsed.has_value()) {
        return "";
    }
    const GitLabCiUrl& url = *parsed;

    std::cerr << "yai: resolving GitLab CI artifact: job=" << url.job_id
              << ", file=" << url.filename << ", project=" << url.project_path << "\n";

    const std::string encoded_path = url_encode(url.project_path);

    const std::string branch = fetch_gitlab_default_branch(url.base, encoded_path);
    if (branch.empty()) {
        return "";
    }

    const std::string pipeline_id = fetch_gitlab_latest_pipeline_id(url.base, encoded_path, branch);
    if (pipeline_id.empty()) {
        return "";
    }

    const std::vector<GitLabJob> jobs = fetch_gitlab_appimage_jobs(url.base, encoded_path, pipeline_id);
    if (jobs.empty()) {
        std::cerr << "yai: could not find AppImage CI job in latest pipeline for "
                  << url.project_path << "\n";
        return "";
    }

    for (const auto& job : jobs) {
        const std::string local_path = download_and_extract_gitlab_artifact(url.base, url.project_path, job);
        if (!local_path.empty()) {
            return local_path;
        }
    }

    std::cerr << "yai: could not download CI artifact from any AppImage job for "
              << url.project_path << "\n";
    return "";
}
