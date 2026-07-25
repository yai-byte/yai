# Website AppImage search speed

**Date:** 2026-07-25  
**Status:** Approved  
**Goal:** Make `website_page` AppImage discovery faster without changing trust boundaries or selection semantics, by fixing unsafe landing probes, adding timeouts, bounded parallel page fetches, and download-path queue priority.

## Problem

`resolve_website_appimage_download` discovers AppImage URLs with a host-bounded BFS. It is slow because:

1. Each page is fetched sequentially via a fresh `curl` process (`fetch_text`), with no timeout.
2. Candidate “landing” probing can `fetch_text` URLs that already end in `.AppImage`, which may download an entire binary into memory only to discover it is not HTML.
3. Speculative common download paths (`/download`, `/downloads`, …) share the same full timeout budget as real pages.
4. Newly queued links are FIFO; download-related pages are not preferred, so useful pages may be checked late.

## Scope

**In scope**

- Skip full-text fetch for URLs that already look like AppImage downloads (`.AppImage` suffix).
- Cap any remaining HTML landing probe size and time.
- Add timeouts to website (and shared) text fetches: default 15s; speculative common-path URLs 5s.
- Bounded parallel page fetching (concurrency 4) inside website search.
- Prioritize queued follow links whose paths contain `appimage`, then `download`, then `linux`.
- Update smoke tests / assertions if checked/queued counts or progress wording change.

**Out of scope**

- Disk cache of resolved `id → download_url`
- Replacing process `curl` with libcurl multi
- Expanding curated official hints or changing feed → `github_release` / `direct_url` classification
- Changing `stage_appimage_source` install-time landing follow (download then inspect local file)

## Behavior

### 1. Safe candidate resolution

`resolved_appimage_candidate` (or equivalent):

- If `is_appimage_download_url(link)` is true, return `link` unchanged. Do **not** call `fetch_text` on it during website search.
- Only non-`.AppImage` URLs that still need HTML landing probing (if any remain under the existing `should_probe_*` rules after the above) may be fetched, and then only with:
  - `curl --max-filesize` of at most 2 MiB
  - the same timeout class as a normal page fetch (15s)

Install-time landing handling in `stage_appimage_source` stays as today: download to a local path, detect HTML, follow embedded AppImage link.

### 2. Timed `fetch_text`

Extend text fetching so callers can pass a timeout (milliseconds). Defaults:

| Kind | Timeout |
|------|---------|
| Normal page / API / repo text | 15 000 ms |
| Speculative `common_project_download_urls` pages | 5 000 ms |

Implementation notes:

- Prefer `run_process_capture_timeout` (already in `process.cpp`) plus curl `--max-time` when appropriate so hung TCP and slow bodies both fail.
- On timeout or curl failure during website search, treat the page as skipped (existing `fetch_website_links` catch path), not as a hard abort of the whole search.
- Other `fetch_text` callers (GitHub API, repo index) get the 15s default unless they already need unbounded behavior; they must not hang forever after this change.

Mark queue items that came from `common_project_download_urls` so the shorter timeout applies only to those speculative GETs.

### 3. Bounded parallel crawl

Keep host/depth/queue-size limits (depth &lt; 2, queue ≤ 128, at most 96 pages checked).

Change the main loop from strict one-page-at-a-time to:

1. Select up to **4** not-yet-seen queue items (highest priority first; see §4).
2. Fetch them concurrently (separate `curl` processes).
3. Process results in a deterministic order (the order selected for that batch).
4. After each page’s links are collected into `appimage_urls`, if `best_appimage_url_from_candidates` is non-empty, stop and return that URL (same early-exit rule as today).
5. Otherwise enqueue newly discovered follow links and continue.

Progress reporting still aggregates checked / queued / skipped / candidates. Exact counts may change vs serial crawl when speculative pages run in parallel; smoke tests that hard-code counts must be updated to match the new deterministic batch behavior.

`file://` fixtures in tests must keep working (local reads are fast; parallelism must not break ordering or selection).

### 4. Queue priority

Assign an integer priority to each queued URL path (fragment/query stripped, case-insensitive):

| Path contains | Priority (higher first) |
|---------------|-------------------------|
| `appimage` | 3 |
| `download` (and not already scored higher) | 2 |
| `linux` (and not already scored higher) | 1 |
| otherwise | 0 |

Rules:

- Initial seeds (official hints, then `source_url`, then `homepage`) keep their current enqueue order and are not re-sorted away from that seed order among themselves.
- Links discovered from pages, and common-path expansions from catalog handoff, are inserted according to priority (stable within the same priority: FIFO).
- When selecting the next batch, always take the highest-priority pending items first.

Trust filters (`should_follow_download_page`, allowed hosts, catalog bridge) are unchanged.

### 5. Selection semantics

Unchanged:

- Domain-bounded crawl and AppImageHub/catalog one-hop bridge
- `appimage_asset_score` / `best_appimage_url_from_candidates`
- Early return once any positively scored candidate exists after processing a page in the batch

## Testing

- `tests/appimage_feed_smoke.sh` continues to pass for `no-github-app` and `musescore` website install/update/upgrade.
- Add or adjust coverage so a `.AppImage` candidate is never full-text-fetched during website search (e.g. assert via progress/side effects or a fake `curl` log when practical).
- Update any hard-coded `after checking N page(s), queued M, skipped K` assertions to the new deterministic counts.
- No curl progress meter or raw `curl:` errors on stderr during successful website search (existing checks).

## Files likely touched

- `src/resolver_website.cpp` — parallel loop, priority queue, speculative timeout tagging, safe candidate resolution
- `src/cli_download.cpp` / `src/yai.hpp` — timed `fetch_text` (and optional max-filesize helper for HTML probes)
- `tests/appimage_feed_smoke.sh` — assertions
- `README.md` / Chinese developer doc — brief note that website search may fetch a few pages in parallel with timeouts (only if user-facing behavior is worth documenting)

## Non-goals reminder

No resolve-result cache and no libcurl in this change set.
