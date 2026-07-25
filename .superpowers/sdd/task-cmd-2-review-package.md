# Task 2 review — commands_upgrade
  394 src/commands.cpp
  580 src/commands_upgrade.cpp
  220 src/commands_lifecycle.cpp
 1194 总计
## Makefile
	src/commands_upgrade.cpp \
	src/commands_doctor.cpp \
	src/commands_lifecycle.cpp \
## Public vs anon in upgrade
5:namespace {
6:struct UpgradeCommandOptions {
54:struct UpdateContext {
384:struct UpdatePreviewResult {
541:} // namespace
543:void cleanup_update_candidate(const InstallPaths& candidate_paths) {
558:void upgrade_app(int argc, char** argv) {
568:void update_app(int argc, char** argv) {
## Still in commands?
19:std::string search_summary(const std::string& summary) {
75:void remove_if_exists(const fs::path& path) {
89:    remove_if_exists(paths.wrapper);
90:    remove_if_exists(paths.desktop);
128:                      << search_summary(package.summary) << "\n";
272:void repo_update_app(int argc, char** argv) {
300:        repo_update_app(argc, argv);
## upgrade file structure (first 40 + around cleanup + end)
#include "yai.hpp"

// Upgrade / update preview and apply workflows.

namespace {
struct UpgradeCommandOptions {
    InstallOptions options;
    bool all = false;
    bool yes = false;
};

UpgradeCommandOptions parse_upgrade_command_options(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(tr("upgrade requires a package id or --all"));
    }

    UpgradeCommandOptions command;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") {
            command.all = true;
        } else if (arg == "--yes" || arg == "-y") {
            command.yes = true;
        } else if (arg == "--download") {
            parse_download_strategy_option(command.options, read_option_value(argc, argv, i, arg));
        } else if (arg == "--mirror-template") {
            parse_mirror_template_option(command.options, read_option_value(argc, argv, i, arg));
        } else if (arg == "--downloader") {
            parse_downloader_option(command.options, read_option_value(argc, argv, i, arg));
        } else if (arg.rfind("--", 0) == 0) {
            throw std::runtime_error(tr("unknown upgrade option: ") + arg);
        } else if (command.options.target.empty()) {
            command.options.target = arg;
        } else {
            throw std::runtime_error(tr("upgrade accepts one package id or --all"));
        }
    }

    if (command.all && !command.options.target.empty()) {
        throw std::runtime_error(tr("upgrade --all does not accept a package id"));
...
5:namespace {
10:};
52:}
65:};
78:}
117:}
124:}
139:}
151:}
158:}
166:}
173:}
191:}
200:        cleanup_update_candidate(candidate_paths);
204:}
206:void activate_update_appimage(const InstallPaths& paths, const InstallPaths& candidate_paths) {
213:}
231:}
241:}
248:    activate_update_appimage(paths, candidate_paths);
251:}
268:        cleanup_update_candidate(candidate_paths);
280:}
295:}
324:    cleanup_update_candidate(candidate_paths);
330:    cleanup_update_candidate(candidate_paths);
332:}
338:}
353:}
378:}
382:}
390:};
394:}
402:}
485:}
494:}
500:}
539:}
541:} // namespace
543:void cleanup_update_candidate(const InstallPaths& candidate_paths) {
556:}
558:void upgrade_app(int argc, char** argv) {
566:}
568:void update_app(int argc, char** argv) {
579:}
anon blocks [(5, 541)]
cleanup_update_candidate @543 inside_anon=False: void cleanup_update_candidate(const InstallPaths& candidate_paths) {
upgrade_app @558 inside_anon=False: void upgrade_app(int argc, char** argv) {
update_app @206 inside_anon=True: void activate_update_appimage(const InstallPaths& paths, const InstallPaths& can
parse_upgrade_command_options @12 inside_anon=True: UpgradeCommandOptions parse_upgrade_command_options(int argc, char** argv) {
UpgradeCommandOptions @6 inside_anon=True: struct UpgradeCommandOptions {
