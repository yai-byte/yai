# Task 3 Report: Extract `src/commands_query.cpp`

## Status
Complete.

## Commits
None (per instructions).

## Changes
- Created `src/commands_query.cpp` with remove/list/search/info workflows.
- Anonymous namespace: `search_summary`, `print_package_source_reason`, `print_package_source_info`.
- Public file-scope: `remove_if_exists`, `remove_app`, `list_apps`, `search_packages`, `info_package`.
- Removed moved code and `search_summary` from `src/commands.cpp`; repo/mirror block and shared helpers remain.
- Added `src/commands_query.cpp` to `Makefile` `SRC` list.

## Build
```
make clean && make
```
Exit code: 0. No warnings reported.

## Verification
- `commands.cpp` no longer defines `search_summary`, `remove_if_exists`, or `info_package`; `repo_list_app` is first query-adjacent symbol removed from the query slice.
- Declarations unchanged in `yai.hpp`; `main.cpp` dispatch unchanged.

## Concerns
None.
