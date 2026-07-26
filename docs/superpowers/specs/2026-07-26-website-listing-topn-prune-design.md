# Website listing top-N follow prune

**Date:** 2026-07-26  
**Status:** Approved  
**Goal:** Speed up multi-version `website_page` AppImage discovery on Apache-style directory indexes by enqueueing only the newest few non-stale version directories (plus a single stale fallback), without changing trust boundaries or final selection semantics.

## Problem

After mtime-aware queue ordering and early-stop, KDE-like autoindexes still enqueue **every** version directory from the listing. Concurrent batches (4) and soft caps can still fetch several version pages before stop. For packages with many release folders, that extra crawl is the main remaining latency.

## Decisions (locked)

| Topic | Choice |
|-------|--------|
| Approach | Listing-aware top-N enqueue prune only (not global per-page link caps) |
| Non-stale follow cap | **N = 3** |
| Stale follow cap | **1** (best by existing queue comparator) |
| AppImage direct links | Never pruned; always recorded as candidates when allowed |
| Freshness signal | Existing listing mtime + stale_penalty (no semver) |
| No-mtime / non-listing pages | No prune; current enqueue behavior |

## Scope

**In scope**

- When a crawled page is a directory listing **and** at least one non-stale follow link has a known mtime, prune which follow URLs are enqueued from that page.
- Keep AppImage URL candidates unpruned.
- Constants + small helper for sort/select; smoke/fixture coverage for prune vs no-prune paths.
- Allow updating hard-coded checked/queued counts in existing smokes if they change.

**Out of scope**

- Disk cache of `id → download_url`
- Raising fetch concurrency or changing GitHub release resolution
- Semver as a primary newest rule
- Changing soft-cap / HEAD fill-in / trust / depth / queue size limits (they continue to apply alongside prune)
- Hard-coding per-app version pins beyond existing hint URLs

## Architecture

Stay in the website resolver path (`resolver_website.cpp`, helpers in `resolver_url.cpp` / `yai.hpp` if unit-tested).

**Data flow (per fetched page)**

1. Obtain `WebsiteLinkMeta` list via existing `website_page_link_metas` (listing or generic HTML).
2. Split links:
   - Allowed AppImage candidates → always `record_appimage_candidate` (unchanged).
   - Follow candidates that pass `should_queue_website_link` → subject to prune when trigger fires.
3. If prune trigger is false → enqueue all follow candidates as today.
4. If prune trigger is true → select:
   - top **3** non-stale follows with known mtime (comparator: lower `stale_penalty`, newer `mtime`, higher `priority`, earlier `seq` — same spirit as `queue_item_better` / listing meta order);
   - drop non-stale follows with **unknown** mtime on this pruned page (avoids undoing early-stop);
   - at most **1** stale follow (best by same comparator);
   - then `queue_website_link` only those survivors.
5. Seeds, catalog handoff, speculative common paths, concurrency, early-stop, and final `best_website_appimage_url` selection are unchanged.

## Behavior

### 1. Prune trigger

All must hold for the **current page’s** follow enqueue step:

1. Page HTML was treated as a directory listing (same detection as today’s listing path / non-empty listing metas for that page). Implementation may pass a boolean from fetch/collect, or re-detect from the fact that metas came from listing parse with row mtimes — prefer an explicit “this page was a listing” flag if cheap.
2. Among links that would be queued as follows (not AppImage candidates), there exists ≥1 with `stale_penalty == 0` and `mtime.has_value()`.

Otherwise: do not prune.

### 2. Caps

| Class | Cap |
|-------|-----|
| Non-stale + known mtime follow | 3 |
| Non-stale + unknown mtime follow | 0 when prune active |
| Stale follow | 1 |
| AppImage download URLs | unlimited (still subject to existing allow/score rules) |

Constants:

- `kWebsiteListingFollowNonStaleMax = 3`
- `kWebsiteListingFollowStaleMax = 1`

### 3. Ordering for selection

Sort prune candidates with the same freshness rules already used for website queue/candidates:

1. Lower `stale_penalty` first when mixing is needed; when splitting buckets, sort within non-stale and within stale separately.
2. Newer known `mtime` first.
3. Higher path `priority` (`website_url_priority`) if available at prune time; if prune runs on metas before priority is assigned, apply `website_url_priority(url)` for tie-break.
4. Stable original order / seq for remaining ties.

### 4. Degradation

| Situation | Behavior |
|-----------|----------|
| Not a directory listing | No prune |
| Listing with no parseable mtimes on any non-stale follow | No prune |
| Newest dirs lack AppImage but 2nd/3rd have one | N=3 still covers; early-stop + selection unchanged |
| Only stale version dirs | At most one stale follow enqueued; stale-only AppImage fallback still works if candidates appear |
| Top-N dirs all fail / skip | Existing crawl continues with whatever remains queued; may still error with “no AppImage…” if nothing found |

### 5. Interaction with early-stop

Prune reduces how many version dirs enter the queue, so `queue_has_newer_non_stale_mtime` and soft-cap stop conditions fire with less wasted work. Do not weaken early-stop or require visiting all N before stop: if one non-stale AppImage exists and no remaining queued non-stale item has newer known mtime, stop as today (possibly after fewer than N version fetches).

## Testing

Local `file://` fixtures (extend `tests/website_mtime_smoke.sh` or add a focused smoke):

1. **Prune limits follows:** listing with ≥5 non-stale version dirs (distinct mtimes) + 1 attic dir; after resolve, assert selected AppImage is from the newest mtime dir, and that older dirs beyond top-3 were not fetched (e.g. via checked-URL progress, a fetch log hook, or by making only top-3 child pages contain AppImages while 4th/5th would also match if visited — prefer a clear assertion that 4th/5th AppImages are not selected and ideally not checked).
2. **AppImage row not pruned:** listing includes a direct `.AppImage` href plus many dirs → AppImage still becomes a candidate.
3. **No mtime → no prune:** listing rows without dates → still discovers AppImage under a non-top-lexicographic path if the crawl would have found it before (regression vs blind drop).
4. **Regression:** `website_mtime_smoke` + `appimage_feed_smoke` keep passing; update count strings only if needed.

## Files likely touched

- `src/resolver_website.cpp` — prune at follow enqueue
- `src/yai.hpp` / `src/resolver_url.cpp` — optional pure helper for selecting which metas survive
- `tests/website_mtime_smoke.sh` (and/or a small dedicated smoke) — prune cases
- `tests/appimage_feed_smoke.sh` — counts only if changed

## Non-goals reminder

No resolve cache, no libcurl multi, no semver-primary ranking, no change to GitHub asset selection.
