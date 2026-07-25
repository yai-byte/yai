#include "yai.hpp"

// AppImage metadata helpers: key/value reads, checksum, and metadata emission.

std::optional<std::string> metadata_value(const fs::path& file, const std::string& key) {
    if (file.extension() == ".json") {
        if (!fs::exists(file)) {
            return std::nullopt;
        }
        return json_find_string(read_text_file(file), key);
    }

    std::ifstream in(file);
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
    }
    return std::nullopt;
}

fs::path readable_metadata_path(const InstallPaths& paths) {
    if (fs::exists(paths.metadata)) {
        return paths.metadata;
    }
    if (fs::exists(paths.legacy_metadata)) {
        return paths.legacy_metadata;
    }
    return paths.metadata;
}

bool metadata_exists(const InstallPaths& paths) {
    return fs::exists(paths.metadata) || fs::exists(paths.legacy_metadata);
}

std::string sha256_file(const fs::path& path) {
    const ProcessResult result = run_process_capture({"sha256sum", path.string()});
    if (result.exit_code != 0) {
        return "";
    }

    std::istringstream in(result.output);
    std::string digest;
    in >> digest;
    if (digest.size() != 64) {
        return "";
    }
    for (char ch : digest) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return "";
        }
    }
    return to_lower(digest);
}

void write_metadata(
    const InstallPaths& paths,
    const ResolvedSource& source,
    const std::string& mode) {
    // Metadata is the contract between install, repair, upgrade, and rollback.
    // Keep upstream identity, selected arch, and install_mode together so later
    // flows can reconstruct the same launch behavior.
    const std::string sha256 = sha256_file(paths.appimage);
    const std::string metadata_arch = source.arch.empty() ? current_arch() : normalize_arch(source.arch);
    const std::string metadata =
        "{\n"
        "  \"id\": \"" + json_escape_string(source.id) + "\",\n"
        "  \"name\": \"" + json_escape_string(source.name) + "\",\n"
        "  \"version\": \"" + json_escape_string(source.version) + "\",\n"
        "  \"arch\": \"" + json_escape_string(metadata_arch) + "\",\n"
        "  \"install_mode\": \"" + json_escape_string(mode) + "\",\n"
        "  \"installed_at\": \"" + json_escape_string(current_utc_timestamp()) + "\",\n"
        "  \"source_kind\": \"" + json_escape_string(source.source_kind) + "\",\n"
        "  \"source_url\": \"" + json_escape_string(source.source_url) + "\",\n"
        "  \"download_url\": \"" + json_escape_string(source.download_url) + "\",\n"
        "  \"github_owner\": \"" + json_escape_string(source.github_owner) + "\",\n"
        "  \"github_repo\": \"" + json_escape_string(source.github_repo) + "\",\n"
        "  \"github_asset\": \"" + json_escape_string(source.github_asset) + "\",\n"
        "  \"sha256\": \"" + json_escape_string(sha256) + "\",\n"
        "  \"checksum_status\": \"unknown\",\n"
        "  \"files\": {\n"
        "    \"appimage\": \"" + json_escape_string(paths.appimage.string()) + "\",\n"
        "    \"wrapper\": \"" + json_escape_string(paths.wrapper.string()) + "\",\n"
        "    \"desktop\": \"" + json_escape_string(paths.desktop.string()) + "\",\n"
        "    \"extracted_dir\": \"" + json_escape_string(paths.extracted_dir.string()) + "\"\n"
        "  }\n"
        "}\n";
    write_text_file(paths.metadata, metadata);

    const bool legacy_removed = remove_best_effort(paths.legacy_metadata);
    (void)legacy_removed; // Legacy metadata is a compatibility cleanup only.
}
