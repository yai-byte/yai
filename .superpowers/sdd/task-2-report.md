# Task 2 Report: Extract `src/process.cpp`

## Status

DONE

## Changes

- Created `src/process.cpp` and moved all child-process execution code into it.
- Kept the five declared process APIs at file scope:
  - `run_process`
  - `run_process_capture`
  - `run_process_capture_separate`
  - `run_process_capture_timeout`
  - `run_process_capture_download_progress`
- Wrapped all process-only helpers, including `ReadOutputResult`, in an anonymous namespace.
- Removed the moved process blocks from `src/core.cpp`.
- Left the complete download progress UI block, from `format_byte_count` through `clear_download_progress`, in `src/core.cpp`.
- Added `src/process.cpp` to the Makefile source list.

## Verification

- Marker check confirms `src/core.cpp` now transitions from `output_has_fuse_error` to `format_byte_count`, and `ensure_directory` follows the progress section.
- Symbol search confirms process helpers exist only in `src/process.cpp`.
- IDE diagnostics report no linter errors in `src/core.cpp`, `src/process.cpp`, or `Makefile`.
- `make clean && make` completed successfully with exit code 0.

## Commits

None. No git commit was created. The workspace directory is not currently recognized as a Git repository.

## Concerns

None.
