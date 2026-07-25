# Split commands.cpp by command family (approach 2)

**Date:** 2026-07-25  
**Status:** Approved (scope A + approach 2 layout)  
**Goal:** Split `src/commands.cpp` (~1180 lines) into focused translation units by CLI workflow family, without changing public API or runtime behavior.

## Scope

**In scope**

- Create:
  - `src/commands_lifecycle.cpp`
  - `src/commands_upgrade.cpp`
  - `src/commands_query.cpp`
  - `src/commands_repo.cpp`
- Leave a thin `src/commands.cpp` for shared helpers used across families
- Keep existing `src/commands_doctor.cpp` unchanged
- Update `Makefile` `SRC`
- Keep all declarations in existing `src/yai.hpp` (no header split)

**Out of scope**

- Splitting `resolver.cpp`
- Changing function signatures, behavior, or error-handling policy
- Complexity refactors inside `parse_upgrade_command_options` (move only)
- Splitting `yai.hpp`
- New features

## Architecture

Same pattern as the `core.cpp` split: mechanical moves along existing seams; call sites keep `#include "yai.hpp"`.

```
yai.hpp (unchanged public surface)
   ├── commands.cpp              (shared helpers)
   ├── commands_lifecycle.cpp    (download/install/repair/rollback)
   ├── commands_upgrade.cpp      (upgrade/update)
   ├── commands_query.cpp        (remove/list/search/info)
   ├── commands_repo.cpp         (repo + mirror)
   └── commands_doctor.cpp       (already split; untouched)
```

## File boundaries

Line ranges refer to current `src/commands.cpp` before the split. Re-locate by markers if the file drifts.

### `src/commands.cpp` (remainder / shared)

Public (declared in `yai.hpp`):

- `resolve_installed_package_id`
- `print_mode_line`
- `print_fuse_fallback_line`

Internal (anonymous namespace):

- `command_match_list` (only used by `resolve_installed_package_id`)

Update the file banner to describe shared command helpers only.

### `src/commands_lifecycle.cpp`

- `download_output_name` → anonymous namespace (only used by `download_app`)
- `download_app`, `install_app`
- `repair_installed_package`, `repair_app`
- `previous_version_dir`, `save_previous_version`, `restore_previous_version`, `rollback_app`

Notes:

- Rollback helpers are declared in `yai.hpp` and also called from upgrade code (`commit_update_transaction`). Definitions live here; upgrade calls them via the header.
- Approximate current range: helpers/commands from `download_output_name` through `rollback_app` (~75–286), excluding the shared helpers listed under thin `commands.cpp`.

### `src/commands_upgrade.cpp`

From `struct UpgradeCommandOptions` through `update_app` (current ~288–859).

- File-local types not in `yai.hpp` go in an anonymous namespace:
  - `UpgradeCommandOptions`
  - `UpdateContext`
  - `UpdatePreviewResult`
- All upgrade/update helpers currently file-scope but undeclared in `yai.hpp` also go in that anonymous namespace
- Public file-scope APIs (in `yai.hpp`): `cleanup_update_candidate`, `upgrade_app`, `update_app`

### `src/commands_query.cpp`

From `remove_if_exists` through `info_package` (current ~861–959).

- Public: `remove_if_exists`, `remove_app`, `list_apps`, `search_packages`, `info_package`
- Internal (anonymous namespace): `search_summary`, `print_package_source_reason`, `print_package_source_info` (only used inside this family)

### `src/commands_repo.cpp`

From `repo_list_app` through `mirror_app` (current ~961–end).

- All `repo_*` and `mirror_*` handlers declared in `yai.hpp` stay at file scope
- Helpers used only by this file (e.g. `parse_repo_update_target`, `write_empty_repo_index`, `refresh_*`, `store_repo_index_updates`, `print_repo_update_result`) go in an anonymous namespace if they are not declared in `yai.hpp`

## Makefile

Add the four new sources alongside existing command files:

```make
SRC := \
	src/arch.cpp \
	src/appimage.cpp \
	src/cli_download.cpp \
	src/commands.cpp \
	src/commands_doctor.cpp \
	src/commands_lifecycle.cpp \
	src/commands_query.cpp \
	src/commands_repo.cpp \
	src/commands_upgrade.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/resolver.cpp
```

## Error handling / data flow

Unchanged. No new shared mutable state. Cross-TU calls use existing `yai.hpp` declarations only.

## Testing / verification

1. `make clean && make` succeeds
2. Run smokes that exercise command families:
   - Lifecycle: `tests/stage1_smoke.sh`, `tests/download_smoke.sh`
   - Upgrade/update paths: `tests/stage5_smoke.sh` (and any update/upgrade coverage present)
   - Repo/mirror: `tests/repo_smoke.sh`, `tests/mirror_policy_smoke.sh`
   - Query/remove: covered by stage smokes that list/remove
3. No unit-style smoke currently compiles `commands.cpp` alone (unlike the old `core.cpp` harnesses); if link failures appear, update only g++ source lists

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Upgrade helpers accidentally left with external linkage | Put undeclared helpers/types in anonymous namespace in `commands_upgrade.cpp` |
| Rollback helpers placed only in upgrade, breaking lifecycle ownership | Keep `previous_version_*` / `restore_*` in lifecycle; upgrade includes via `yai.hpp` |
| Cut leaves `search_summary` in shared file unused | Move it with query into anonymous namespace |
| Behavior change while moving | Contiguous block moves; no logic edits in the same change |
