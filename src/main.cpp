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

struct BatchResult {
    std::string target;
    ProcessOutput output;
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
    std::cerr << tr("yai: running ")
              << batch.targets.size() << " " << batch.command
              << tr(" task(s) with ")
              << batch.jobs << tr(" job(s)\n");

    if (batch.expanded_multi) {
        for (std::size_t i = 0; i < batch.targets.size(); ++i) {
            const std::string& target = batch.targets[i];
            ProcessOutput output;
            try {
                output = run_process_capture_separate(
                    batch_child_args(batch, target),
                    std::nullopt,
                    {{"YAI_BATCH_CHILD", "1"}});
            } catch (const std::exception& ex) {
                output = ProcessOutput{1, "", ex.what()};
            }
            std::cerr << "[" << (i + 1) << "/" << batch.targets.size() << "] "
                      << batch.command << " " << target << "\n";
            if (!output.stderr_text.empty()) {
                std::cerr << output.stderr_text;
                if (output.stderr_text.back() != '\n') {
                    std::cerr << "\n";
                }
            }
            if (!output.stdout_text.empty()) {
                std::cout << output.stdout_text;
                if (output.stdout_text.back() != '\n') {
                    std::cout << "\n";
                }
            }
            if (output.exit_code != 0) {
                std::cerr << tr_format(
                    "yai: task failed: {command} {target} (exit {code})\n",
                    {{"{command}", batch.command},
                     {"{target}", target},
                     {"{code}", std::to_string(output.exit_code)}});
                throw std::runtime_error(tr("batch stopped after task failure"));
            }
        }
        return;
    }

    std::vector<BatchResult> results(batch.targets.size());
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    workers.reserve(batch.jobs);

    for (std::size_t worker = 0; worker < batch.jobs; ++worker) {
        workers.emplace_back([&]() {
            while (true) {
                const std::size_t index = next.fetch_add(1);
                if (index >= batch.targets.size()) {
                    return;
                }
                const std::string target = batch.targets[index];
                const std::vector<std::string> args = batch_child_args(batch, target);
                try {
                    const ProcessOutput output = run_process_capture_separate(
                        args,
                        std::nullopt,
                        {{"YAI_BATCH_CHILD", "1"}});
                    results[index] = BatchResult{target, output};
                } catch (const std::exception& ex) {
                    results[index] = BatchResult{target, ProcessOutput{1, "", ex.what()}};
                }
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    std::size_t failed = 0;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const BatchResult& result = results[i];
        std::cerr << "[" << (i + 1) << "/" << results.size() << "] "
                  << batch.command << " " << result.target << "\n";
        if (!result.output.stderr_text.empty()) {
            std::cerr << result.output.stderr_text;
            if (result.output.stderr_text.back() != '\n') {
                std::cerr << "\n";
            }
        }
        if (!result.output.stdout_text.empty()) {
            std::cout << result.output.stdout_text;
            if (result.output.stdout_text.back() != '\n') {
                std::cout << "\n";
            }
        }
        if (result.output.exit_code != 0) {
            ++failed;
            std::cerr << tr_format(
                "yai: task failed: {command} {target} (exit {code})\n",
                {{"{command}", batch.command},
                 {"{target}", result.target},
                 {"{code}", std::to_string(result.output.exit_code)}});
        }
    }

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
