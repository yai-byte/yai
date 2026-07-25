# Task 1 Report: Extract `src/i18n.cpp`

## Status

**DONE** — `make clean && make` succeeded with no warnings.

## What Was Implemented

Mechanical extraction of PO/i18n code from `src/core.cpp` into new `src/i18n.cpp`, per task brief scripts and design doc i18n section.

### Created: `src/i18n.cpp` (213 lines)

- File header: `#include "yai.hpp"`, `#include <unordered_map>`, locale comment
- Anonymous-namespace PO helpers (lines 13–166 of pre-split `core.cpp`):
  - `po_unquote`, `executable_dir_path`, `translation_dirs`, `language_po_file`
  - `parse_po_file`, `load_translation_catalog`, `translation_catalog`
- Public i18n API (lines 183–234 of pre-split `core.cpp`):
  - `current_language()`, `use_chinese()`, `tr()`, `tr_format()`
- `tr_format` calls `replace_all` via `yai.hpp` declaration; definition remains in `core.cpp`

### Modified: `src/core.cpp` (1407 lines, was 1614)

Removed the two slices above. Retained structure per brief:

1. Includes + banner comment (lines 1–12)
2. `APPIMAGE_FEED_URL`
3. `env_string`, `ascii_lower` (lines 167–182 of pre-split)
4. `home_dir` and all downstream helpers unchanged

Confirmed absent from `core.cpp`: `namespace {` PO block, `current_language`, `use_chinese`, `tr`, `tr_format`.

Confirmed present in `core.cpp`: `replace_all` at line 247.

### Modified: `Makefile`

Added `src/i18n.cpp \` after `src/core.cpp \` in `SRC`.

## Build Verification

```bash
make clean && make
```

**Output summary:**

```
rm -f yai
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -o yai src/arch.cpp src/appimage.cpp src/cli_download.cpp src/commands.cpp src/commands_doctor.cpp src/core.cpp src/i18n.cpp src/json.cpp src/main.cpp src/repo.cpp src/resolver.cpp
```

- Exit code: 0
- Warnings: none
- Binary: `yai` (891304 bytes)

## Files Changed

| File | Action |
|------|--------|
| `src/i18n.cpp` | Created |
| `src/core.cpp` | Modified (removed i18n slices) |
| `Makefile` | Modified (added `src/i18n.cpp` to `SRC`) |

No changes to `yai.hpp`, commands, resolver, or other headers.

## Self-Review

1. **Slice boundaries** — Re-located by markers; pre-split line numbers matched brief exactly (13–166 anon, 183–234 lang/tr, 167–182 env_string/ascii_lower kept).
2. **Linkage** — `tr_format` → `replace_all` cross-TU link resolves via existing `yai.hpp` declaration + `core.cpp` definition.
3. **ODR** — PO helpers in anonymous namespace in `i18n.cpp`; no duplicate symbols.
4. **Scope discipline** — Only the three files above touched; no behavioral or signature changes.
5. **Commits** — None (per plan).

## Concerns

1. **Stale include in `core.cpp`** — `#include <unordered_map>` remains but is no longer used after i18n extraction. Harmless; can be removed in a later cleanup pass (out of Task 1 scope).
2. **Stale banner in `core.cpp`** — Top comment still mentions "locale selection"; design doc notes banner updates for remainder files may come in later tasks.

## Commits

None.
