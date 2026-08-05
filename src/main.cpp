// main.cpp - Entry point for the yai CLI.
//
// Responsibilities:
//   * Parse argv and dispatch to the matching sub-command handler.
//   * For install/download, expand target globs and run tasks either
//     sequentially (after a multi-match confirmation) or in a parallel
//     worker pool bounded by --jobs.
#include "yai.hpp"

namespace {

using CommandHandler = void (*)(int, char**);

// Aggregated state for a batch install/download run: the resolved targets,
// forwarded options, parallelism, and flags derived during parsing.
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

// Maps a command name (as typed on the CLI) to its handler function.
struct CommandEntry {
    const char* name;
    CommandHandler handler;
};

// Thin adapter: forwards to the doctor sub-app with the original argc.
void doctor_command(int argc, char**) {
    doctor_app(argc);
}

// Thin adapter: lists locally installed apps.
void list_command(int, char**) {
    list_apps();
}

// Thin adapter: prints top-level usage/help text.
void help_command(int, char**) {
    print_usage();
}

// Returns true only for commands that support batch (multi-target) execution.
bool is_batch_command(const std::string& command) {
    return command == "install" || command == "download";
}

// Validates and parses a --jobs value; rejects empty/non-numeric input and
// clamps the result to the supported 1..32 range.
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

// Parses argv for an install/download invocation: classifies options vs
// targets, expands repo-id globs into concrete package ids, and decides
// whether the run is a single-target dispatch or a batch (setting jobs,
// prompting on multi-match expansion). Returns a BatchCommand with
// `requested` false for non-batch commands.
BatchCommand parse_batch_command(int argc, char** argv) {
    enum class OptKind { flag_forward, value_forward, jobs };
    struct OptDef {
        const char* name;
        OptKind kind;
        bool install_only = false;
        bool sets_yes = false;
        bool sets_install_identity = false;
    };
    static const OptDef opts[] = {
        {"--yes",             OptKind::flag_forward,  false, true,  false},
        {"-y",                OptKind::flag_forward,  false, true,  false},
        {"--recrawl",         OptKind::flag_forward,  false, false, false},
        {"--arch",            OptKind::value_forward, false, false, false},
        {"--download",        OptKind::value_forward, false, false, false},
        {"--mirror-template", OptKind::value_forward, false, false, false},
        {"--downloader",      OptKind::value_forward, false, false, false},
        {"--id",              OptKind::value_forward, true,  false, true},
        {"--name",            OptKind::value_forward, true,  false, true},
        {"--jobs",            OptKind::jobs,          false, false, false},
    };

    BatchCommand batch;
    batch.program = argv[0];
    batch.command = argv[1];
    if (!is_batch_command(batch.command)) {
        return batch;
    }

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        bool matched = false;
        for (const auto& opt : opts) {
            if (arg != opt.name) continue;
            if (opt.install_only && batch.command != "install") continue;
            matched = true;
            switch (opt.kind) {
                case OptKind::flag_forward:
                    if (opt.sets_yes) batch.yes = true;
                    batch.option_args.push_back(arg);
                    break;
                case OptKind::value_forward:
                    if (++i >= argc) throw std::runtime_error(arg + tr(" requires a value"));
                    if (opt.sets_install_identity) batch.install_identity_explicit = true;
                    batch.option_args.push_back(arg);
                    batch.option_args.push_back(argv[i]);
                    break;
                case OptKind::jobs:
                    if (++i >= argc) throw std::runtime_error(tr("--jobs requires a value"));
                    batch.jobs = parse_jobs_value(argv[i]);
                    batch.requested = true;
                    break;
            }
            break;
        }
        if (!matched) {
            if (arg.rfind("--", 0) == 0) {
                throw std::runtime_error(tr_format(
                        "unknown {command} option: {arg}",
                        {{"{command}", batch.command}, {"{arg}", arg}}));
            }
            batch.targets.push_back(arg);
        }
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

    // Multi-target mode: force parallel execution even if --jobs was not given.
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

// Builds the argv vector for one batch child process: program + command +
// single target, followed by the original forwarded options.
std::vector<std::string> batch_child_args(const BatchCommand& batch, const std::string& target) {
    std::vector<std::string> args;
    args.reserve(batch.option_args.size() + 3);
    args.push_back(batch.program);
    args.push_back(batch.command);
    args.push_back(target);
    args.insert(args.end(), batch.option_args.begin(), batch.option_args.end());
    return args;
}

// Executes a batch run. When `expanded_multi` is set (a glob expansion that
// the user confirmed), tasks run sequentially and the first failure aborts.
// Otherwise a pool of `batch.jobs` worker threads pulls targets from a shared
// atomic index; failures are recorded but do not stop sibling tasks. Reports
// aggregate results via the BatchTerminalUi and throws on any failure or
// user interrupt.
void run_batch_command(const BatchCommand& batch) {
    BatchTerminalUi ui(batch.targets.size());
    ui.log_parent(
        tr("yai: running ") + std::to_string(batch.targets.size()) + " " + batch.command +
        tr(" task(s) with ") + std::to_string(batch.jobs) + tr(" job(s)\n"));

    if (batch.expanded_multi) {
        try {
            for (std::size_t i = 0; i < batch.targets.size(); ++i) {
                check_interrupt();
                const std::string& target = batch.targets[i];
                StreamingBatchResult result;
                try {
                    result = run_batch_task_streaming(
                        BatchTaskRequest{
                            batch_child_args(batch, target),
                            std::nullopt,
                            {{"YAI_BATCH_CHILD", "1"}},
                            i,
                            target,
                        },
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
        } catch (...) {
            ui.shutdown();
            throw;
        }
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
                        BatchTaskRequest{
                            batch_child_args(batch, results[index].target),
                            std::nullopt,
                            {{"YAI_BATCH_CHILD", "1"}},
                            index,
                            results[index].target,
                        },
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
                if (was_interrupted()) {
                    return;
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

    if (was_interrupted()) {
        std::cerr << tr("yai: operation interrupted by user\n");
        if (failed > 0) {
            std::cerr << tr_format("yai: {failed} of {total} batch task(s) did not complete\n",
                {{"{failed}", std::to_string(failed)},
                 {"{total}", std::to_string(results.size())}});
        }
        throw std::runtime_error(tr("batch interrupted"));
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

// Top-level command routing: first parses a potential batch request; if it
// is a batch run, executes it. Otherwise looks up the command name in
// COMMANDS and invokes the matching handler. Throws on unknown commands.
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

// Program entry point: installs signal handlers, cleans up orphaned
// downloads from prior runs, dispatches the requested command, and
// translates any thrown exception into a user-facing error message.
int main(int argc, char** argv) {
    // Install signal handler so the program can gracefully handle SIGINT/SIGTERM.
    install_signal_handler();
    // Clean up stale temporary files from previous interrupted runs.
    cleanup_orphan_downloads();

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
