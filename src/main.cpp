#include "yai.hpp"

namespace {

using CommandHandler = void (*)(int, char**);

struct BatchCommand {
    std::string program;
    std::string command;
    std::vector<std::string> targets;
    std::vector<std::string> option_args;
    std::size_t jobs = 0;
    bool requested = false;
    bool install_identity_explicit = false;
    bool yes = false;
    bool expanded_multi = false;
    bool cancelled = false;
};

struct CommandEntry {
    const char* name;
    CommandHandler handler;
};

void doctor_command(int argc, char**) {
    doctor_app(argc);
}

void list_command(int, char**) {
    list_apps();
}

void help_command(int, char**) {
    print_usage();
}

bool is_batch_command(const std::string& command) {
    return command == "install" || command == "download";
}

bool batch_option_takes_value(const std::string& command, const std::string& arg) {
    if (arg == "--arch" || arg == "--download" || arg == "--mirror-template" || arg == "--downloader") {
        return true;
    }
    return command == "install" && (arg == "--id" || arg == "--name");
}

std::size_t parse_jobs_value(const std::string& value) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        throw std::runtime_error(tr("--jobs must be a positive integer"));
    }
    const unsigned long parsed = std::stoul(value);
    if (parsed == 0 || parsed > 32) {
        throw std::runtime_error(tr("--jobs must be between 1 and 32"));
    }
    return static_cast<std::size_t>(parsed);
}

BatchCommand parse_batch_command(int argc, char** argv) {
    BatchCommand batch;
    batch.program = argv[0];
    batch.command = argv[1];
    if (!is_batch_command(batch.command)) {
        return batch;
    }

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--jobs") {
            if (++i >= argc) {
                throw std::runtime_error(tr("--jobs requires a value"));
            }
            batch.jobs = parse_jobs_value(argv[i]);
            batch.requested = true;
            continue;
        }
        if (arg == "--yes" || arg == "-y") {
            batch.yes = true;
            batch.option_args.push_back(arg);
            continue;
        }
        if (batch_option_takes_value(batch.command, arg)) {
            if (++i >= argc) {
                throw std::runtime_error(arg + tr(" requires a value"));
            }
            if (arg == "--id" || arg == "--name") {
                batch.install_identity_explicit = true;
            }
            batch.option_args.push_back(arg);
            batch.option_args.push_back(argv[i]);
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            throw std::runtime_error(tr_format(
                    "unknown {command} option: {arg}",
                    {{"{command}", batch.command}, {"{arg}", arg}}));
        }
        batch.targets.push_back(arg);
    }

    std::vector<std::string> expanded_targets;
    for (const std::string& target : batch.targets) {
        // Only repo-id style globs expand here. URLs (including ?query), GitHub
        // owner/repo, and local AppImage paths must stay as ordinary targets.
        if (!has_glob_wildcards(target) || !looks_like_repo_package_target(target)) {
            expanded_targets.push_back(target);
            continue;
        }
        const std::vector<RepoPackage> packages = find_repo_packages(target);
        if (packages.size() > 1) {
            batch.expanded_multi = true;
        }
        for (const RepoPackage& package : packages) {
            expanded_targets.push_back(package.id);
        }
    }
    batch.targets = std::move(expanded_targets);

    if (batch.targets.size() > 1) {
        batch.requested = true;
    }
    if (!batch.requested) {
        return batch;
    }
    if (batch.targets.empty()) {
        throw std::runtime_error(batch.command + tr(" requires at least one target"));
    }
    if (batch.command == "install" && batch.targets.size() > 1 && batch.install_identity_explicit) {
        throw std::runtime_error(tr("batch install cannot use --id or --name with multiple targets"));
    }
    if (batch.expanded_multi) {
        const std::string prompt = tr_format(
            batch.command == "download"
                ? "Download {count} package(s)? [y/N] "
                : "Install {count} package(s)? [y/N] ",
            {{"{count}", std::to_string(batch.targets.size())}});
        if (!confirm_multi_match(prompt, batch.targets, batch.yes)) {
            std::cout << (batch.command == "download"
                              ? tr("Download cancelled\n")
                              : tr("Install cancelled\n"));
            batch.cancelled = true;
            return batch;
        }
        batch.jobs = 1;
        return batch;
    }
    if (batch.jobs == 0) {
        const unsigned int hardware = std::thread::hardware_concurrency();
        const std::size_t default_jobs = hardware == 0 ? 4 : std::min<std::size_t>(hardware, 4);
        batch.jobs = std::min(batch.targets.size(), std::max<std::size_t>(1, default_jobs));
    } else {
        batch.jobs = std::min(batch.jobs, batch.targets.size());
    }
    return batch;
}

std::vector<std::string> batch_child_args(const BatchCommand& batch, const std::string& target) {
    std::vector<std::string> args;
    args.reserve(batch.option_args.size() + 3);
    args.push_back(batch.program);
    args.push_back(batch.command);
    args.push_back(target);
    args.insert(args.end(), batch.option_args.begin(), batch.option_args.end());
    return args;
}

void run_batch_command(const BatchCommand& batch) {
    BatchTerminalUi ui(batch.targets.size());
    ui.log_parent(
        tr("yai: running ") + std::to_string(batch.targets.size()) + " " + batch.command +
        tr(" task(s) with ") + std::to_string(batch.jobs) + tr(" job(s)\n"));

    if (batch.expanded_multi) {
        for (std::size_t i = 0; i < batch.targets.size(); ++i) {
            const std::string& target = batch.targets[i];
            StreamingBatchResult result;
            try {
                result = run_batch_task_streaming(
                    batch_child_args(batch, target),
                    std::nullopt,
                    {{"YAI_BATCH_CHILD", "1"}},
                    i,
                    batch.targets.size(),
                    target,
                    ui);
            } catch (const std::exception& ex) {
                ui.log_line(i, target, ex.what());
                result.exit_code = 1;
            }
            if (result.exit_code != 0) {
                ui.log_parent(tr_format(
                    "yai: task failed: {command} {target} (exit {code})\n",
                    {{"{command}", batch.command},
                     {"{target}", target},
                     {"{code}", std::to_string(result.exit_code)}}));
                ui.shutdown();
                throw std::runtime_error(tr("batch stopped after task failure"));
            }
        }
        ui.shutdown();
        return;
    }

    struct ParallelSlot {
        std::string target;
        int exit_code = 1;
    };
    std::vector<ParallelSlot> results(batch.targets.size());
    for (std::size_t i = 0; i < batch.targets.size(); ++i) {
        results[i].target = batch.targets[i];
    }

    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    workers.reserve(batch.jobs);
    for (std::size_t w = 0; w < batch.jobs; ++w) {
        workers.emplace_back([&]() {
            while (true) {
                const std::size_t index = next.fetch_add(1);
                if (index >= batch.targets.size()) {
                    return;
                }
                try {
                    const StreamingBatchResult result = run_batch_task_streaming(
                        batch_child_args(batch, results[index].target),
                        std::nullopt,
                        {{"YAI_BATCH_CHILD", "1"}},
                        index,
                        batch.targets.size(),
                        results[index].target,
                        ui);
                    results[index].exit_code = result.exit_code;
                } catch (const std::exception& ex) {
                    ui.log_line(index, results[index].target, ex.what());
                    results[index].exit_code = 1;
                }
                if (results[index].exit_code != 0) {
                    ui.log_parent(tr_format(
                        "yai: task failed: {command} {target} (exit {code})\n",
                        {{"{command}", batch.command},
                         {"{target}", results[index].target},
                         {"{code}", std::to_string(results[index].exit_code)}}));
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    std::size_t failed = 0;
    for (const auto& result : results) {
        if (result.exit_code != 0) {
            ++failed;
        }
    }
    ui.shutdown();
    if (failed > 0) {
        throw std::runtime_error(tr_format(
            "{failed} of {total} batch task(s) failed",
            {{"{failed}", std::to_string(failed)},
             {"{total}", std::to_string(results.size())}}));
    }
}

const CommandEntry COMMANDS[] = {
    {"search", search_packages},
    {"info", info_package},
    {"repo", repo_app},
    {"mirror", mirror_app},
    {"download", download_app},
    {"install", install_app},
    {"update", update_app},
    {"upgrade", upgrade_app},
    {"rollback", rollback_app},
    {"repair", repair_app},
    {"doctor", doctor_command},
    {"remove", remove_app},
    {"list", list_command},
    {"help", help_command},
    {"--help", help_command},
    {"-h", help_command},
};

void dispatch_command(int argc, char** argv) {
    const std::string command = argv[1];
    const BatchCommand batch = parse_batch_command(argc, argv);
    if (batch.requested) {
        if (batch.cancelled) {
            return;
        }
        run_batch_command(batch);
        return;
    }

    for (const CommandEntry& entry : COMMANDS) {
        if (command == entry.name) {
            entry.handler(argc, argv);
            return;
        }
    }
    throw std::runtime_error(tr("unknown command: ") + command);
}

}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 1;
        }

        dispatch_command(argc, argv);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << tr("yai: ") << ex.what() << "\n";
        return 1;
    }
}
