# Task 3 Report: Extract `src/download_progress.cpp`

## Status

Completed.

## Changes

- Created `src/download_progress.cpp` with the progress formatting/UI and aria2/curl progress helpers formerly located in `src/core.cpp`.
- Kept all `yai.hpp`-declared progress APIs at file scope:
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
- Moved non-API helpers and types into anonymous namespaces, including the endian/bitfield helpers, `aria2_control_downloaded_bytes`, `DownloadProgressSnapshot`, snapshot/format/render helpers, and `write_progress_line`.
- Removed the complete progress slice from `src/core.cpp`; `output_has_fuse_error` is now followed by `ensure_directory`.
- Replaced the `src/core.cpp` banner with the brief's focused responsibility description.
- Updated the Makefile `SRC` list to the exact ordered list in the brief, including `src/download_progress.cpp`.
- Confirmed `run_process_capture_download_progress` remains in `src/process.cpp`.

## Verification

- IDE diagnostics for `src/core.cpp`, `src/download_progress.cpp`, and `Makefile`: no errors.
- Command: `make clean && make`
- Result: exit code 0.
- Compiler invocation included all 13 sources from the required Makefile list and produced `yai` without warnings.

## Commits

None. `/home/fsx/yai` has no Git repository metadata, so Git status/diff checks are unavailable and no commit was created.

## Concerns

- No functional concern identified.
- Repository history and diff could not be inspected because the supplied working directory is not a Git repository.
