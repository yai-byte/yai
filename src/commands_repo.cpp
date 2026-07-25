#include "yai.hpp"

// Repository and mirror command handlers.

void repo_list_app(int argc) {
    if (argc != 3) {
        throw std::runtime_error(tr("repo list does not accept arguments"));
    }

    for (const RepoEntry& entry : load_repo_entries()) {
        const std::string status = fs::exists(named_repo_index_path(entry.name)) ? "cached" : "missing";
        std::cout << entry.name << "\t" << entry.location << "\t" << status << "\n";
    }
}

void repo_add_app(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        throw std::runtime_error(tr("repo add requires a name and optional URL/path"));
    }

    const std::string name = validate_repo_name(argv[3]);
    if (argc == 4 && name != "appimage") {
        throw std::runtime_error(tr("repo add without URL/path is only supported for the built-in appimage feed"));
    }
    const std::string location = validate_repo_location(argc == 5 ? argv[4] : APPIMAGE_FEED_URL);
    const std::string index_text = normalize_repo_source_index(load_repo_source_text(location));

    ensure_directory(repos_dir_path());
    write_text_file(named_repo_index_path(name), index_text);

    std::vector<RepoEntry> entries = load_repo_entries();
    bool replaced = false;
    for (RepoEntry& entry : entries) {
        if (entry.name == name) {
            entry.location = location;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        entries.push_back(RepoEntry{name, location});
    }

    write_repo_entries(entries);
    rebuild_repo_index_from_cached_files(entries);
    std::cout << tr("Added repo ") << name << "\n";
    std::cout << tr("Location: ") << location << "\n";
}

namespace {

std::optional<std::string> parse_repo_update_target(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        throw std::runtime_error(tr("repo update accepts at most one repo name"));
    }
    if (argc == 4) {
        return validate_repo_name(argv[3]);
    }
    return std::nullopt;
}

void write_empty_repo_index() {
    ensure_directory(repos_dir_path());
    write_text_file(
        repos_dir_path() / "index.json",
        "{\n  \"schema_version\": 1,\n  \"updated_at\": \"" +
            json_escape_string(current_utc_timestamp()) +
            "\",\n  \"packages\": []\n}\n");
}

std::pair<RepoEntry, std::string> refresh_repo_index(const RepoEntry& entry) {
    return {entry, normalize_repo_source_index(load_repo_source_text(entry.location))};
}

std::vector<std::pair<RepoEntry, std::string>> refresh_selected_repo_indexes(
    const std::vector<RepoEntry>& entries,
    const std::optional<std::string>& selected_name) {
    std::vector<std::pair<RepoEntry, std::string>> fetched;
    for (const RepoEntry& entry : entries) {
        if (selected_name.has_value() && entry.name != *selected_name) {
            continue;
        }
        fetched.push_back(refresh_repo_index(entry));
    }
    if (selected_name.has_value() && fetched.empty()) {
        throw std::runtime_error(tr("repo not configured: ") + *selected_name);
    }
    return fetched;
}

void store_repo_index_updates(const std::vector<std::pair<RepoEntry, std::string>>& fetched) {
    ensure_directory(repos_dir_path());
    for (const auto& item : fetched) {
        write_text_file(named_repo_index_path(item.first.name), item.second);
    }
}

void print_repo_update_result(std::size_t count) {
    std::cout << tr_format(
        "Updated {count} repo(s)\n",
        {{"{count}", std::to_string(count)}});
}

} // namespace

void repo_update_app(int argc, char** argv) {
    const std::optional<std::string> selected_name = parse_repo_update_target(argc, argv);

    const std::vector<RepoEntry> entries = load_repo_entries();
    if (entries.empty()) {
        write_empty_repo_index();
        std::cout << tr("No repos configured\n");
        return;
    }

    const std::vector<std::pair<RepoEntry, std::string>> fetched =
        refresh_selected_repo_indexes(entries, selected_name);
    store_repo_index_updates(fetched);
    rebuild_repo_index_from_cached_files(entries);
    print_repo_update_result(fetched.size());
}

void repo_app(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(tr("repo requires a subcommand: list, add, or update"));
    }

    const std::string subcommand = argv[2];
    if (subcommand == "list") {
        repo_list_app(argc);
    } else if (subcommand == "add") {
        repo_add_app(argc, argv);
    } else if (subcommand == "update") {
        repo_update_app(argc, argv);
    } else {
        throw std::runtime_error(tr("unknown repo subcommand: ") + subcommand);
    }
}

void mirror_list_app(int argc) {
    if (argc != 3) {
        throw std::runtime_error(tr("mirror list does not accept arguments"));
    }

    const NetworkConfig config = load_network_config();
    std::cout << tr("Current: ") << (config.exists ? config.provider : "direct") << "\n";
    std::cout << tr("Strategy: ") << (config.exists ? config.download_strategy : "direct") << "\n";
    if (config.exists && !config.mirror_template.empty()) {
        std::cout << tr("Template: ") << config.mirror_template << "\n";
    }
    std::cout << tr("\nAvailable providers:\n");
    for (const MirrorProvider& provider : built_in_mirror_providers()) {
        std::cout << provider.name << "\t" << provider.mirror_template << "\t" << tr(provider.description) << "\n";
    }
}

void mirror_use_app(int argc, char** argv) {
    if (argc != 4) {
        throw std::runtime_error(tr("mirror use requires a provider name"));
    }

    const std::string provider_name = to_lower(argv[3]);
    const std::optional<MirrorProvider> provider = mirror_provider_by_name(provider_name);
    if (!provider.has_value()) {
        throw std::runtime_error(tr("unknown mirror provider: ") + provider_name);
    }

    NetworkConfig config;
    config.exists = true;
    config.prompted = true;
    config.provider = provider->name;
    config.download_strategy = "mirror_first";
    config.mirror_template = provider->mirror_template;
    write_network_config(config);
    std::cout << china_network_disclaimer() << "\n";
    std::cout << tr("Enabled GitHub Release proxy provider: ")
              << provider->name << "\n";
}

void mirror_custom_app(int argc, char** argv) {
    if (argc != 4) {
        throw std::runtime_error(tr("mirror custom requires an Xget domain or template"));
    }

    NetworkConfig config;
    config.exists = true;
    config.prompted = true;
    config.provider = "custom";
    config.download_strategy = "mirror_first";
    config.mirror_template = normalize_custom_mirror_template(argv[3]);
    write_network_config(config);
    std::cout << china_network_disclaimer() << "\n";
    std::cout << tr("Enabled custom GitHub Release proxy template: ")
              << config.mirror_template << "\n";
}

void mirror_off_app(int argc) {
    if (argc != 3) {
        throw std::runtime_error(tr("mirror off does not accept arguments"));
    }

    NetworkConfig config;
    config.exists = true;
    config.prompted = true;
    config.provider = "direct";
    config.download_strategy = "direct";
    write_network_config(config);
    std::cout << tr("GitHub Release proxy disabled; yai will use direct GitHub downloads by default\n");
}

void mirror_app(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(tr("mirror requires a subcommand: list, use, custom, or off"));
    }

    const std::string subcommand = argv[2];
    if (subcommand == "list") {
        mirror_list_app(argc);
    } else if (subcommand == "use") {
        mirror_use_app(argc, argv);
    } else if (subcommand == "custom") {
        mirror_custom_app(argc, argv);
    } else if (subcommand == "off") {
        mirror_off_app(argc);
    } else {
        throw std::runtime_error(tr("unknown mirror subcommand: ") + subcommand);
    }
}
