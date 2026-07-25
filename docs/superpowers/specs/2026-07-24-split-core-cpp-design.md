# Split core.cpp by responsibility (approach 2)

**Date:** 2026-07-24  
**Status:** Approved (approach B scope + approach 2 layout)  
**Goal:** Reduce `src/core.cpp` (~1613 lines) by moving code into focused translation units without changing public API or runtime behavior.

## Scope

**In scope**

- Create `src/i18n.cpp`, `src/process.cpp`, `src/download_progress.cpp`
- Leave a thinner `src/core.cpp` for path/string helpers, filesystem helpers, mirror/network config, and `paths_for`
- Update `Makefile` `SRC` list
- Keep all declarations in existing `src/yai.hpp` (no header split)

**Out of scope**

- Splitting `commands.cpp` / `resolver.cpp`
- Changing function signatures, behavior, or error-handling policy
- Splitting `yai.hpp` into module headers
- New features or refactors beyond pure move + linkage hygiene

## Architecture

Pure mechanical split along existing seams. Call sites keep `#include "yai.hpp"`. New `.cpp` files each `#include "yai.hpp"` (and any extra standard headers they already need, e.g. `<unordered_map>` for i18n).

```
yai.hpp (unchanged public surface)
   ├── i18n.cpp
   ├── process.cpp
   ├── download_progress.cpp
   └── core.cpp (remainder)
```

## File boundaries

Exact move ranges refer to current `src/core.cpp` before the split.

### `src/i18n.cpp`

- Anonymous-namespace PO helpers and catalogs (current lines ~13–166)
- `current_language`, `use_chinese`, `tr`, `tr_format` (~183–234)
- Needs `#include <unordered_map>`
- May call `env_string`, `ascii_lower`, `replace_all` via declarations in `yai.hpp` (definitions stay in `core.cpp`)

### `src/process.cpp`

- Child-process helpers and APIs from `process_argv` through `run_process_capture_timeout` (~476–855)
- Also `run_process_capture_download_progress` (~1359–1418), because it uses process-only helpers (`start_captured_process`, fd/wait helpers, etc.)
- Helpers that are currently file-scope but **not** declared in `yai.hpp` move into an anonymous namespace in this file (linkage hygiene; no API change)
- Calls progress APIs (`render_download_progress`, `clear_download_progress`) via `yai.hpp`

### `src/download_progress.cpp`

- Progress formatting, terminal width helpers, header/aria2 progress parsing, and UI render helpers from `format_byte_count` through `clear_download_progress` (~857–1357)
- Includes file-local types such as `DownloadProgressSnapshot` and aria2 bitfield helpers
- Does **not** own `run_process_capture_download_progress` (stays in `process.cpp`)

### `src/core.cpp` (remainder)

- File banner comment updated to match remaining responsibilities
- `APPIMAGE_FEED_URL`
- `env_string`, `ascii_lower`
- Path/string/target helpers from `home_dir` through `output_has_fuse_error` (~236–474)
- Filesystem helpers + mirror/network config + `paths_for` (~1420–end)

## Makefile

Add the three new sources to `SRC` (order may match logical grouping; link order does not matter for this codebase):

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

## Error handling / data flow

Unchanged. No new error paths; no new shared mutable state beyond what already exists (static translation catalogs remain in `i18n.cpp`).

## Testing / verification

1. `make clean && make` succeeds with no new warnings beyond pre-existing ones
2. Run smoke tests that exercise moved code:
   - `tests/progress_smoke.sh` (download progress + process capture)
   - At least one broader stage smoke (e.g. `tests/stage1_smoke.sh` or project’s usual `make`-adjacent test entry if documented)
3. Success criteria: binary builds; smokes that passed before still pass; no public header churn

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| ODR / duplicate symbols from former file-scope helpers | Put non-API helpers in anonymous namespace in the owning `.cpp` |
| Cut point leaves a helper on the wrong side | Prefer compiling after each file extraction; fix by moving the helper with its only callers |
| `tr_format` ↔ `replace_all` cross-TU | Already declared in `yai.hpp`; keep definition in `core.cpp` |
| Accidentally changing behavior while moving | Move contiguous blocks; do not reformat or “clean up” logic in the same change |
