# Batch progress UI design

**Date:** 2026-07-26  
**Status:** Approved (approach: structured event protocol + parent-owned sticky footer)

## Goal

When `install` / `download` run as a **batch** (multiple targets), show live what each task is doing instead of capturing child output and replaying it only after each task finishes. Download progress must remain visible during the batch, rendered by the parent as a sticky footer while task logs scroll above.

## Requirements (agreed)

1. **Display model:** interleaved log stream + sticky bottom activity region (not a full multi-line status board, and not `\r` progress lines fighting each other in the scrollback).
2. **Progress in the UI:** download progress bars are part of the sticky footer; stage/status text from children is part of the scrolling log.
3. **Scope:** all batch paths for `install` and `download` (explicit parallel multi-argv, and wildcard-expanded sequential batches). Single-package path stays unchanged.
4. **Log content:** stream the child’s full stdout and stderr in real time (prefixed). Sticky footer holds download/active progress only.
5. **Non-TTY:** stream prefixed log lines in real time; do **not** draw the sticky footer or `\r` animation (aligned with single-package: text yes, progress bar no).
6. **Scheduling semantics unchanged:** wildcard-expanded batches stay sequential and stop on first failure; explicit multi-argv stays parallel and aggregates failures at the end.

## Problem (current behavior)

- Batch re-execs one child per target with `YAI_BATCH_CHILD=1` and `run_process_capture_separate()`, which redirects child stdout/stderr to temp files.
- Child stderr is not a TTY, so `render_download_progress()` no-ops.
- Downloaders are invoked with `--silent` / `--quiet` so tool-native progress is off (by design).
- Parent only prints `[i/n] …` and replays captured output after the task ends → feels silent while work runs.

## Architecture: parent-owned UI + event fd

Keep process-isolated batch children. The **parent** is the only process that paints the interactive terminal.

### Channels per child task

| Channel | Content | Parent handling |
|---------|---------|-----------------|
| stdout pipe | Child status lines (`Downloading…`, `Installed…`, …) | Read line-wise; print to scrolling region with prefix |
| stderr pipe | Child diagnostics / warnings / errors | Same as stdout (prefixed scroll log) |
| event fd (`YAI_BATCH_EVENT_FD`) | Structured progress events | Update / clear sticky footer rows (TTY only) |

### Progress event protocol (one line per event)

Each child gets its **own** event pipe, so the parent keys footer rows by **task index** (which fd delivered the line). Events do not need to carry a target id (avoids mismatch between argv target, resolved package id, and URL installs).

| Event | Meaning |
|-------|---------|
| `PROGRESS done=<bytes> total=<bytes\|-> rate=<Bps\|->` | Refresh this task’s footer line; `total=-` means unknown total (keep animated-bar behavior) |
| `PROGRESS_CLEAR` | Remove this task from the activity region |

- Emitted only when the child has `YAI_BATCH_EVENT_FD` set.
- Single-package path does not set the fd → existing direct stderr `\r` progress unchanged.
- Malformed event lines are ignored (forward-compatible).

### Components

1. **`BatchTerminalUi`** (new): thread-safe `log_line` / `update_progress` / `clear_progress`.
   - **TTY stderr:** sticky footer of active download rows; before writing a log line, clear footer → write log → redraw footer.
   - **Non-TTY:** `log_line` only; ignore progress rendering.
2. **`run_batch_task_streaming(...)`** (new): replace batch use of `run_process_capture_separate` with three pipes + `poll` multiplexing; return exit code (no post-hoc full replay).
3. **`render_download_progress` / `clear_download_progress`:** if event fd is present, emit `PROGRESS` / `PROGRESS_CLEAR` instead of painting stderr locally.
4. **`run_batch_command`:** both sequential (`expanded_multi`) and parallel worker paths use streaming + shared UI. Workers must not write the terminal directly; all output goes through the locked UI.

## Terminal layout

### TTY (stderr)

```
yai: running 3 install task(s) with 2 job(s)
[1/3 foo] Downloading https://example/foo.AppImage
[2/3 bar] Downloading https://example/bar.AppImage
[1/3 foo] Installed foo
[1/3 foo] Wrapper: ~/.local/bin/foo
[1/3 foo] ████████░░  80%  12MB/15MB  2.1MB/s   ← sticky footer (redrawn; not scrollback)
[2/3 bar] ███░░░░░░░  30%   4MB/14MB  1.0MB/s
```

- **Scroll region:** every complete line from the child, plus parent summary/failure lines.
- **Activity region (sticky footer):** one line per in-progress task that has a progress snapshot; removed on `PROGRESS_CLEAR` / task end; always pinned to the bottom of the TTY view.
- Parent banner lines (`yai: running…`, `yai: task failed…`) stay unprefixed.

### Prefix format

`[{index}/{total} {target}] {message}`

- `index` is 1-based in batch target order (not completion order).
- Parallel completion order must not renumber prefixes.

### stdout policy in batch mode

Child stdout content is forwarded into the **prefixed scroll stream on stderr** so parallel tasks do not interleave an unprefixed global stdout. Batch mode does not replay child stdout onto the parent’s stdout after the fact.

### Non-TTY

- No sticky footer, no `\r`.
- Prefixed log lines still stream in real time.
- No end-of-task full replay (avoids duplicate output).

## Failure behavior (display timing only)

- **Wildcard sequential:** on failure, print `yai: task failed…` immediately and stop; prior successes remain.
- **Explicit parallel:** print per-task failure lines as they finish; after all workers join, if any failed, throw the existing aggregated error (`{failed} of {total} batch task(s) failed`).

Exit codes and stop/aggregate semantics stay as today.

## Edge cases

- **Narrow terminal:** reuse existing `render_progress_line` truncation.
- **Many concurrent jobs:** activity region shows at most `min(jobs, target_count, 8)` rows (prefer tasks with live download progress); scroll log always preferred over growing the footer without bound.
- **Ctrl-C:** keep existing process-group cleanup; clear the activity region on the way out so no orphan `\r` residue remains.
- **No download phase** (local AppImage install): scroll logs only; footer empty for that task.

## Out of scope

- Changing single-package install/download UX.
- Changing downloader selection or enabling tool-native progress bars.
- In-process threaded install (rejected in favor of keeping re-exec isolation).
- Applying this UI to non-install/download commands (`upgrade --all`, etc.) in this change.

## Testing

1. **Adapt existing smokes** (`stage1_smoke`, `download_smoke`, `wildcard_multi_smoke`):
   - Accept `[i/n target]` prefixes.
   - Do not assume parallel completion order; assert substrings.
   - Stop relying on post-task full capture replay shape where it changes.
2. **Batch streaming coverage** (new assertions or small dedicated smoke):
   - Parallel two-target run shows prefixed logs for both targets before/during work.
   - Non-TTY: no `\r` progress residue in captured stderr.
   - Wildcard sequential failure: first success kept, later targets not run, failure line present.
   - Optional: slow/fake download fixture proves `Downloading` (or equivalent) appears before the child exits.
3. **Protocol unit coverage** (if lightweight tests exist for progress helpers): parse `PROGRESS` / `PROGRESS_CLEAR` (no target field); ignore malformed lines.

## Implementation touchpoints (expected)

| Area | Files (indicative) |
|------|--------------------|
| Batch orchestration | `src/main.cpp` (`run_batch_command`, child env/args) |
| Process streaming | `src/process.cpp`, `src/yai.hpp` |
| Progress emit/render | `src/download_progress.cpp` |
| Tests / i18n | `tests/*smoke.sh`, `po/en.po`, `po/zh.po` if new user-facing strings |

## Success criteria

- `yai install a b --jobs 2` (TTY): live prefixed logs for both tasks; sticky footer shows per-task download progress while downloads run.
- Same command with stderr redirected: live prefixed logs, no progress bar / `\r`.
- `yai install 'pack-*' --yes`: sequential streaming logs; stop-on-first-failure preserved.
- Single-target `yai install pack-a`: identical UX to today.
