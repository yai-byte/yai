# Task 1 Report: Extract `src/commands_lifecycle.cpp`

## Status

Completed.

## Changes

- Added `src/commands_lifecycle.cpp` with the download, install, repair, and rollback workflows.
- Moved `download_output_name` into an anonymous namespace in the new translation unit.
- Kept these lifecycle APIs at file scope:
  - `download_app`
  - `install_app`
  - `repair_installed_package`
  - `repair_app`
  - `previous_version_dir`
  - `save_previous_version`
  - `restore_previous_version`
  - `rollback_app`
- Removed the contiguous lifecycle block from `src/commands.cpp`.
- Left `UpgradeCommandOptions` and the upgrade/query/repo code in `src/commands.cpp`; it now directly follows the shared output helpers.
- Added `src/commands_lifecycle.cpp` to `Makefile` next to the other `commands_*` sources.
- Did not modify `commands_doctor`.

## Verification

Command:

```text
make clean && make
```

Result: exit code 0.

Build summary:

```text
rm -f yai
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -o yai src/arch.cpp src/appimage.cpp src/cli_download.cpp src/commands.cpp src/commands_doctor.cpp src/commands_lifecycle.cpp src/core.cpp src/download_progress.cpp src/i18n.cpp src/json.cpp src/main.cpp src/process.cpp src/repo.cpp src/resolver.cpp
```

No compiler warnings or errors were emitted.

## Commits

None. No git commit was created.

## Concerns

None identified.
