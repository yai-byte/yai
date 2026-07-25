# Split core.cpp Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `src/core.cpp` (~1613 lines) into `i18n.cpp`, `process.cpp`, `download_progress.cpp`, and a thinner `core.cpp` with no public API or behavior change.

**Architecture:** Mechanical move of contiguous blocks along seams documented in `docs/superpowers/specs/2026-07-24-split-core-cpp-design.md`. Keep all declarations in `src/yai.hpp`. Put helpers that are not declared in `yai.hpp` into anonymous namespaces in their owning `.cpp`.

**Tech Stack:** C++17, Makefile, existing shell smoke tests.

## Global Constraints

- Do not change function signatures or runtime behavior
- Do not split or edit `src/yai.hpp` except if a compile error proves a missing include already needed elsewhere (prefer fixing the new `.cpp` includes only)
- Do not commit unless the user explicitly asks
- Do not “clean up” logic while moving (no renames, no reformatting beyond what the move requires)
- Line numbers below refer to `src/core.cpp` **before** any split; re-read markers if the file has drifted

## File map (after completion)

| File | Owns |
|------|------|
| `src/i18n.cpp` | PO catalog anon helpers + `current_language` / `use_chinese` / `tr` / `tr_format` |
| `src/process.cpp` | Process helpers + `run_process*` including `run_process_capture_download_progress` |
| `src/download_progress.cpp` | Progress formatting/UI + aria2/header progress parsing through `clear_download_progress` |
| `src/core.cpp` | `APPIMAGE_FEED_URL`, `env_string`/`ascii_lower`, path/string helpers, fs + mirror/network + `paths_for` |
| `Makefile` | Lists all four plus existing sources |

---

### Task 1: Extract `src/i18n.cpp`

**Files:**
- Create: `src/i18n.cpp`
- Modify: `src/core.cpp` (remove moved block)
- Modify: `Makefile` (add `src/i18n.cpp`)

**Interfaces:**
- Consumes (via `yai.hpp`, defined later in `core.cpp`): `env_string`, `ascii_lower`, `replace_all`
- Produces: `current_language`, `use_chinese`, `tr`, `tr_format` plus internal PO helpers

- [ ] **Step 1: Create `src/i18n.cpp` from current `core.cpp` slices**

Run from `/home/fsx/yai`:

```bash
python3 - <<'PY'
from pathlib import Path
core = Path('src/core.cpp').read_text().splitlines(True)
# Markers (1-based inclusive ranges from pre-split core.cpp):
# anon PO helpers: lines 13-166
# language + tr: lines 183-234
anon = ''.join(core[12:166])          # 13-166
lang = ''.join(core[182:234])         # 183-234
header = '''#include "yai.hpp"

#include <unordered_map>

// Locale selection and gettext-style po catalog loading for tr()/tr_format().

'''
Path('src/i18n.cpp').write_text(header + anon + '\n' + lang)
print('wrote src/i18n.cpp', len(header + anon + '\n' + lang), 'bytes')
PY
```

- [ ] **Step 2: Delete the same slices from `src/core.cpp`**

```bash
python3 - <<'PY'
from pathlib import Path
core = Path('src/core.cpp').read_text().splitlines(True)
# Keep file header lines 1-12, drop 13-166 and 183-234.
# After deleting 13-166, former 183-234 shifts: delete in one pass by index.
keep = core[0:12] + core[166:182] + core[234:]
# core[166:182] is blank line + env_string + ascii_lower (lines 167-182)
Path('src/core.cpp').write_text(''.join(keep))
print('core.cpp now', len(keep), 'lines')
PY
```

Expected: `core.cpp` still starts with `#include "yai.hpp"`, then `APPIMAGE_FEED_URL`, then `env_string` / `ascii_lower`, then `home_dir`, … and no `namespace {` PO helpers / no `tr(`.

- [ ] **Step 3: Add `src/i18n.cpp` to `Makefile` `SRC`**

Insert alphabetically among sources:

```make
	src/i18n.cpp \
```

after `src/core.cpp \` (or keep alpha order with other files as in the design doc).

- [ ] **Step 4: Build**

```bash
make clean && make
```

Expected: link succeeds. If undefined `replace_all` / `env_string` / `ascii_lower`, confirm they remain defined in `core.cpp` and declared in `yai.hpp`.

---

### Task 2: Extract `src/process.cpp`

**Files:**
- Create: `src/process.cpp`
- Modify: `src/core.cpp` (remove process block + `run_process_capture_download_progress`)

**Interfaces:**
- Consumes: `tr`, progress APIs (`render_download_progress`, `clear_download_progress`, …) via `yai.hpp`
- Produces: `run_process`, `run_process_capture`, `run_process_capture_separate`, `run_process_capture_timeout`, `run_process_capture_download_progress`
- Internal (anonymous namespace): `process_argv`, fd/wait/kill helpers, `exec_child_process`, `start_captured_process`, `set_nonblocking`, `kill_and_reap`, `ReadOutputResult`, read/append output helpers, `terminate_for_timeout`

- [ ] **Step 1: Re-locate markers in the post–Task-1 `core.cpp`**

```bash
rg -n '^(std::vector<char\*> process_argv|ProcessResult run_process_capture_timeout|std::string format_byte_count|ProcessResult run_process_capture_download_progress|void ensure_directory)' src/core.cpp
```

Record:
- `P0` = line of `process_argv`
- `P1` = last line of `run_process_capture_timeout` (closing `}` of that function)
- `D0` = line of `format_byte_count` (start of progress section; must remain in core until Task 3)
- `R0` = line of `run_process_capture_download_progress`
- `R1` = last line of that function
- `E0` = line of `ensure_directory`

- [ ] **Step 2: Create `src/process.cpp`**

Extract `[P0, P1]` and `[R0, R1]` into one file. Wrap every extracted symbol that is **not** declared in `yai.hpp` inside `namespace { ... }`. Public functions stay at file scope:

Public (outside anon):
- `run_process`
- `run_process_capture`
- `run_process_capture_separate`
- `run_process_capture_timeout`
- `run_process_capture_download_progress`

Everything else from the process slice goes in the anonymous namespace (including `enum class ReadOutputResult` and helpers used only by the public runners).

File skeleton:

```cpp
#include "yai.hpp"

// Child-process execution and captured downloads (including progress-aware capture).

namespace {
// ... former file-scope process helpers ...
} // namespace

int run_process(...) { ... }
ProcessResult run_process_capture(...) { ... }
ProcessOutput run_process_capture_separate(...) { ... }
ProcessResult run_process_capture_timeout(...) { ... }
ProcessResult run_process_capture_download_progress(...) { ... }
```

Implementation tip: after copying the contiguous `P0–P1` block, open `namespace {` before `process_argv` and close it immediately before `int run_process(`. Keep `run_process_capture_download_progress` after the timeout function (still file scope).

- [ ] **Step 3: Remove `[P0, P1]` and `[R0, R1]` from `src/core.cpp`**

Leave the progress block (`format_byte_count` … `clear_download_progress`) in place for Task 3. After removal, `output_has_fuse_error` should be followed by `format_byte_count` (temporarily), then later `ensure_directory`.

- [ ] **Step 4: Add `src/process.cpp` to `Makefile` and build**

```bash
# edit Makefile SRC to include src/process.cpp
make clean && make
```

Expected: success. Failures about missing helpers mean a helper was left outside anon in the wrong file or still referenced from `core.cpp`.

---

### Task 3: Extract `src/download_progress.cpp`

**Files:**
- Create: `src/download_progress.cpp`
- Modify: `src/core.cpp` (remove progress block)
- Modify: `Makefile` (add `src/download_progress.cpp`)

**Interfaces:**
- Consumes: `tr`, `tr_format`, filesystem helpers as already used
- Produces: all progress-related APIs declared in `yai.hpp` from `format_byte_count` through `clear_download_progress`
- Internal (anonymous namespace): endian/bitfield/aria2 helpers, `DownloadProgressSnapshot`, `download_progress_snapshot`, `download_progress_knows_total`, `format_download_progress_stats`, `progress_bar_width`, `render_progress_line`, `write_progress_line`, and any other helper in that slice not listed in `yai.hpp`

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(std::string format_byte_count|void clear_download_progress|void ensure_directory)' src/core.cpp
```

- [ ] **Step 2: Create `src/download_progress.cpp` with the progress slice**

```cpp
#include "yai.hpp"

// Terminal download progress rendering and aria2/curl progress probes.

```

Move `format_byte_count` through `clear_download_progress` inclusive. Put non-API helpers/types in `namespace { }`. Keep these at file scope (they are in `yai.hpp`):

- `format_byte_count`
- `format_duration_seconds`
- `display_width`
- `truncate_display_width`
- `terminal_width`
- `download_total_from_headers`
- `download_progress_downloaded_bytes`
- `download_progress_recent_speed`
- `progress_bar`
- `render_download_progress`
- `clear_download_progress`

Note: `aria2_control_downloaded_bytes` is **not** in `yai.hpp` → anonymous namespace. Same for `DownloadProgressSnapshot` and friends.

- [ ] **Step 3: Remove that slice from `src/core.cpp`**

Afterward, `output_has_fuse_error` should be immediately followed by `ensure_directory` (aside from blank lines).

- [ ] **Step 4: Update `core.cpp` banner comment**

Replace the old multi-responsibility comment with something accurate, e.g.:

```cpp
// Shared path/string helpers, filesystem utilities, mirror/network config, and
// install path derivation. Process execution, download progress UI, and i18n
// live in process.cpp, download_progress.cpp, and i18n.cpp.
```

- [ ] **Step 5: Finalize `Makefile` `SRC`**

Exact list:

```make
SRC := \
	src/arch.cpp \
	src/appimage.cpp \
	src/cli_download.cpp \
	src/commands.cpp \
	src/commands_doctor.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/resolver.cpp
```

- [ ] **Step 6: Build**

```bash
make clean && make
```

Expected: clean build of `yai`.

---

### Task 4: Regression verification

**Files:** none (run only)

- [ ] **Step 1: Build once more**

```bash
make clean && make
```

Expected: exit 0; produces `./yai`.

- [ ] **Step 2: Run progress + download smokes (moved code paths)**

```bash
YAI_LANG=en tests/progress_smoke.sh
YAI_LANG=en tests/download_smoke.sh
```

Expected: each prints a `* smoke test passed` line and exits 0.

- [ ] **Step 3: Run baseline stage/repo smokes**

```bash
YAI_LANG=en tests/stage1_smoke.sh
YAI_LANG=en tests/repo_smoke.sh
YAI_LANG=en tests/arch_smoke.sh
YAI_LANG=en tests/json_smoke.sh
```

Expected: all pass.

- [ ] **Step 4: Spot-check line counts**

```bash
wc -l src/core.cpp src/i18n.cpp src/process.cpp src/download_progress.cpp
```

Expected (approximate):
- `core.cpp` ≲ 500 lines
- `i18n.cpp` ~220–250
- `process.cpp` ~450–550 (helpers + download progress capture)
- `download_progress.cpp` ~500–600
- Sum ≈ previous `core.cpp` size (± headers/comments)

- [ ] **Step 5: Commit only if the user asks**

Do not run `git commit` unless explicitly requested.

---

## Self-review checklist

1. **Spec coverage:** i18n / process / download_progress / thin core / Makefile / no header split / verification — each has a task.
2. **Placeholders:** none intentionally left.
3. **Type consistency:** public symbols unchanged; internals confined to anonymous namespaces per owning TU.
