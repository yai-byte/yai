# Split commands.cpp Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `src/commands.cpp` (~1180 lines) into `commands_lifecycle.cpp`, `commands_upgrade.cpp`, `commands_query.cpp`, `commands_repo.cpp`, and a thin shared `commands.cpp`, with no public API or behavior change.

**Architecture:** Mechanical contiguous moves along seams in `docs/superpowers/specs/2026-07-25-split-commands-cpp-design.md`. Keep declarations in `src/yai.hpp`. Put helpers/types not declared in `yai.hpp` into anonymous namespaces in their owning `.cpp`.

**Tech Stack:** C++17, Makefile, existing shell smoke tests.

## Global Constraints

- Do not change function signatures or runtime behavior
- Do not split or edit `src/yai.hpp` except if a new `.cpp` needs an include already required elsewhere (prefer fixing the new `.cpp` only)
- Do not commit unless the user explicitly asks
- Do not “clean up” logic while moving (no renames, no reformatting beyond what the move requires)
- Keep `src/commands_doctor.cpp` untouched
- Line numbers below refer to `src/commands.cpp` **before** any split; re-read markers if the file has drifted

## File map (after completion)

| File | Owns |
|------|------|
| `src/commands.cpp` | `resolve_installed_package_id`, `print_mode_line`, `print_fuse_fallback_line`; anon `command_match_list` |
| `src/commands_lifecycle.cpp` | download/install/repair/rollback (+ anon `download_output_name`) |
| `src/commands_upgrade.cpp` | upgrade/update pipeline (+ anon types/helpers) |
| `src/commands_query.cpp` | remove/list/search/info (+ anon `search_summary`, package-info printers) |
| `src/commands_repo.cpp` | repo_* + mirror_* (+ anon repo-update helpers) |
| `Makefile` | Lists all of the above plus existing sources |

**Public APIs that must remain file-scope** (declared in `yai.hpp`):  
`download_app`, `install_app`, `repair_installed_package`, `repair_app`, `previous_version_dir`, `save_previous_version`, `restore_previous_version`, `rollback_app`, `cleanup_update_candidate`, `upgrade_app`, `update_app`, `remove_if_exists`, `remove_app`, `list_apps`, `search_packages`, `info_package`, `repo_list_app`, `repo_add_app`, `repo_update_app`, `repo_app`, `mirror_list_app`, `mirror_use_app`, `mirror_custom_app`, `mirror_off_app`, `mirror_app`, `resolve_installed_package_id`, `print_mode_line`, `print_fuse_fallback_line`.

---

### Task 1: Extract `src/commands_lifecycle.cpp`

**Files:**
- Create: `src/commands_lifecycle.cpp`
- Modify: `src/commands.cpp` (remove moved block)
- Modify: `Makefile` (add `src/commands_lifecycle.cpp`)

**Interfaces:**
- Consumes via `yai.hpp`: shared helpers still in `commands.cpp` (`resolve_installed_package_id`, `print_mode_line`, `print_fuse_fallback_line`), plus resolver/appimage/core APIs
- Produces: lifecycle public APIs listed above; anon `download_output_name`

- [ ] **Step 1: Re-locate markers in current `commands.cpp`**

```bash
rg -n '^(std::string download_output_name|void download_app|void rollback_app|struct UpgradeCommandOptions)' src/commands.cpp
```

Record:
- `L0` = `download_output_name`
- `L1` = last line of `rollback_app`
- `U0` = `struct UpgradeCommandOptions` (must stay until Task 2)

- [ ] **Step 2: Create `src/commands_lifecycle.cpp`**

```cpp
#include "yai.hpp"

// Download, install, repair, and rollback command workflows.

namespace {
// download_output_name ...
} // namespace

void download_app(...) { ... }
// ... through rollback_app
```

Move contiguous `[L0, L1]`. Wrap only `download_output_name` in `namespace { }`. Keep all other moved symbols that are in `yai.hpp` at file scope.

- [ ] **Step 3: Delete `[L0, L1]` from `src/commands.cpp`**

Afterward, shared helpers should be followed by `struct UpgradeCommandOptions` (aside from blank lines).

- [ ] **Step 4: Add to Makefile and build**

Insert `src/commands_lifecycle.cpp \` into `SRC` (alphabetically near other `commands_*` files).

```bash
make clean && make
```

Expected: exit 0.

---

### Task 2: Extract `src/commands_upgrade.cpp`

**Files:**
- Create: `src/commands_upgrade.cpp`
- Modify: `src/commands.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `resolve_installed_package_id`, `print_mode_line`, `print_fuse_fallback_line`, `previous_version_dir`, `save_previous_version`, `restore_previous_version`, etc. via `yai.hpp`
- Produces public: `cleanup_update_candidate`, `upgrade_app`, `update_app`
- Internal (anonymous namespace): `UpgradeCommandOptions`, `UpdateContext`, `UpdatePreviewResult`, and every upgrade helper not declared in `yai.hpp` (see symbol list in the design doc / pre-split inventory)

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(struct UpgradeCommandOptions|void update_app|void remove_if_exists)' src/commands.cpp
```

- `U0` = `struct UpgradeCommandOptions`
- `U1` = last line of `update_app`
- `Q0` = `remove_if_exists` (must remain until Task 3)

- [ ] **Step 2: Create `src/commands_upgrade.cpp`**

Skeleton:

```cpp
#include "yai.hpp"

// Upgrade / update preview and apply workflows.

namespace {
struct UpgradeCommandOptions { ... };
// ... all INTERNAL upgrade helpers and types ...
} // namespace

void cleanup_update_candidate(...) { ... }
void upgrade_app(...) { ... }
void update_app(...) { ... }
```

Important: `cleanup_update_candidate`, `upgrade_app`, and `update_app` must be **outside** the anonymous namespace. Helpers they call that are not in `yai.hpp` must be **inside** it (same TU).

Practical approach: copy `[U0, U1]` into the new file, open `namespace {` before `struct UpgradeCommandOptions`, close it immediately before `void cleanup_update_candidate`, and also ensure `upgrade_app` / `update_app` remain outside. If other PUBLIC symbols appear mid-block, keep those outside too (inventory shows only those three public APIs in this slice).

- [ ] **Step 3: Remove `[U0, U1]` from `commands.cpp`**

- [ ] **Step 4: Makefile + build**

```bash
make clean && make
```

Expected: exit 0. Undefined-reference failures mean a helper was left outside/inside the wrong namespace or still referenced from another TU without a header declaration.

---

### Task 3: Extract `src/commands_query.cpp`

**Files:**
- Create: `src/commands_query.cpp`
- Modify: `src/commands.cpp` (move query block **and** `search_summary`)
- Modify: `Makefile`

**Interfaces:**
- Produces public: `remove_if_exists`, `remove_app`, `list_apps`, `search_packages`, `info_package`
- Internal: `search_summary`, `print_package_source_reason`, `print_package_source_info`

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(std::string search_summary|void remove_if_exists|void info_package|void repo_list_app)' src/commands.cpp
```

- [ ] **Step 2: Create `src/commands_query.cpp`**

```cpp
#include "yai.hpp"

// Remove, list, search, and info command workflows.

namespace {
std::string search_summary(...) { ... }
void print_package_source_reason(...) { ... }
void print_package_source_info(...) { ... }
} // namespace

void remove_if_exists(...) { ... }
void remove_app(...) { ... }
void list_apps() { ... }
void search_packages(...) { ... }
void info_package(...) { ... }
```

Move `search_summary` from the top of `commands.cpp` into this file’s anonymous namespace (it is only used by `search_packages`).

- [ ] **Step 3: Remove `search_summary` and the query slice from `commands.cpp`**

`commands.cpp` should now contain: banner, anon `command_match_list` (if already wrapped) or still file-scope pending Task 4, `resolve_installed_package_id`, `print_mode_line`, `print_fuse_fallback_line`, then repo/mirror block.

- [ ] **Step 4: Makefile + build**

```bash
make clean && make
```

Expected: exit 0.

---

### Task 4: Extract `src/commands_repo.cpp` and thin `commands.cpp`

**Files:**
- Create: `src/commands_repo.cpp`
- Modify: `src/commands.cpp` (leave only shared helpers; wrap `command_match_list`; update banner)
- Modify: `Makefile` (finalize full `SRC` list)

**Interfaces:**
- Repo file produces all `repo_*` / `mirror_*` public APIs; anon helpers: `parse_repo_update_target`, `write_empty_repo_index`, `refresh_repo_index`, `refresh_selected_repo_indexes`, `store_repo_index_updates`, `print_repo_update_result`
- Thin `commands.cpp` produces shared public helpers only

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(void repo_list_app|void mirror_app|std::string command_match_list|std::string resolve_installed_package_id)' src/commands.cpp
```

- [ ] **Step 2: Create `src/commands_repo.cpp`**

Move from `repo_list_app` through end of file. Put INTERNAL helpers in `namespace { }`; keep PUBLIC `repo_*` / `mirror_*` at file scope.

- [ ] **Step 3: Thin `src/commands.cpp`**

Final content should be approximately:

```cpp
#include "yai.hpp"

// Shared helpers used by multiple command-family translation units.

namespace {
std::string command_match_list(const std::vector<std::string>& values) {
    ...
}
} // namespace

std::string resolve_installed_package_id(const std::string& pattern) {
    ...
}

void print_mode_line(const std::string& mode) {
    ...
}

void print_fuse_fallback_line() {
    ...
}
```

- [ ] **Step 4: Finalize Makefile `SRC`**

Exact list:

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

- [ ] **Step 5: Build**

```bash
make clean && make
```

Expected: exit 0; `./yai` produced.

---

### Task 5: Regression verification

**Files:** none unless a smoke hardcodes a stale `g++` source list (then update link lines only).

- [ ] **Step 1: Build**

```bash
make clean && make
```

- [ ] **Step 2: Lifecycle / download smokes**

```bash
YAI_LANG=en tests/download_smoke.sh
YAI_LANG=en tests/stage1_smoke.sh
```

Expected: both pass.

- [ ] **Step 3: Upgrade / later-stage smoke**

```bash
YAI_LANG=en tests/stage5_smoke.sh
```

Expected: pass (covers update/upgrade-related flows if present in suite).

- [ ] **Step 4: Repo / mirror / query-adjacent smokes**

```bash
YAI_LANG=en tests/repo_smoke.sh
YAI_LANG=en tests/mirror_policy_smoke.sh
YAI_LANG=en tests/stage2_smoke.sh
```

Expected: all pass.

- [ ] **Step 5: Line-count spot check**

```bash
wc -l src/commands.cpp src/commands_lifecycle.cpp src/commands_upgrade.cpp \
  src/commands_query.cpp src/commands_repo.cpp src/commands_doctor.cpp
```

Expected (approximate):
- `commands.cpp` ≲ 100
- `commands_lifecycle.cpp` ~280–320
- `commands_upgrade.cpp` ~550–620
- `commands_query.cpp` ~110–140
- `commands_repo.cpp` ~220–260
- Sum of command family files ≈ previous `commands.cpp` + `commands_doctor.cpp` (± banners)

- [ ] **Step 6: Commit only if the user asks**

---

## Self-review checklist

1. **Spec coverage:** lifecycle / upgrade / query / repo / thin shared / Makefile / doctor untouched / verification — each has a task.
2. **Placeholders:** none intentionally left.
3. **Type consistency:** file-local upgrade/repo/query helpers confined to anonymous namespaces; public symbols unchanged and match `yai.hpp`.
