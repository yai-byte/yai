# Website AppImage candidate freshness (mtime + stale demotion)

**Date:** 2026-07-26  
**Status:** Approved  
**Goal:** Fix non-GitHub website AppImage sniffing so version directories are ordered by listing `Last-Modified` (not folder-name lexicographic tricks), and paths that look like archives (`old` / `older` / `attic`) are heavily demoted rather than preferred or blindly followed first.

## Problem

`resolve_website_appimage_download` discovers AppImages with a host-bounded crawl. For KDE-style Apache directory indexes it currently:

1. Blindly `std::reverse`s link order on `download.kde.org/stable/{krita,kdenlive}/` pages, treating HTML order as a proxy for “newer.”
2. Does not demote paths containing archive markers such as `older_versions_are_in_the_attic` or `/Attic/`.
3. Returns as soon as any allowed AppImage candidate appears, so crawl order dominates selection.
4. Final selection (`best_appimage_url_from_candidates`) only scores basename architecture fit, not freshness.

This fails for Krita on `download.kde.org/stable/krita/`: directory `Last-Modified` shows `5.3.2.1/` newer than `6.0.2/`, and krita.org’s primary download buttons point at `5.3.2.1`, while a naive “higher folder name / reverse sort” approach prefers `6.0.x`. Semantic version comparison alone is also wrong for this case; **listing mtime is the agreed freshness signal**.

## Decisions (locked)

| Topic | Choice |
|-------|--------|
| Freshness signal | Directory listing `Last-Modified` (and HEAD `Last-Modified` for a few top candidates when needed) |
| Archive paths | Demote heavily (`stale_penalty`); keep as fallback if no non-stale candidate exists |
| Scope of mtime | Both crawl ordering and final candidate selection |
| Semver | Out of scope as a primary comparator |
| GitHub releases | Unchanged |

## Scope

**In scope**

- Parse Apache/autoindex directory pages for `(href, last_modified)` pairs.
- Extend website queue items with optional mtime and stale penalty; sort accordingly.
- Remove the KDE-only `std::reverse` hack once mtime ordering exists.
- Soften early-exit: collect a bounded candidate pool before selecting.
- Extend final selection: arch score → non-stale → mtime → existing basename score.
- Optional HEAD (bounded, e.g. at most 4) for top candidates lacking inherited listing mtime.
- Local fixture tests covering Krita-like listings and stale-only fallback.

**Out of scope**

- Full semantic-version comparison as the primary “newest” rule.
- Changing GitHub release asset selection.
- Crawling `cdn.kde.org` nightly / Plus / Next CI builds.
- Disk cache of resolved download URLs.
- Hard-coding per-app “official” version pins beyond existing hint URLs.

## Architecture

Stay inside the existing website resolver path (`resolver_website.cpp`, URL helpers in `resolver_url.cpp`). Extract a small helper unit only if autoindex parsing + comparison grow unwieldy.

**Data flow**

1. Fetch page HTML.
2. If the page looks like an autoindex directory listing, parse each row’s name/href and `Last modified`.
3. When enqueueing follow links / version dirs, attach:
   - existing `priority` / `seq` / `speculative`
   - optional `mtime` (from listing when known)
   - `stale_penalty` (1 if path looks archival, else 0)
4. Queue selection order (best first):
   1. lower `stale_penalty`
   2. newer `mtime` (unknown mtime sorts after any known mtime)
   3. higher existing `priority`
   4. earlier `seq`
5. AppImage hits go into a candidate pool; do not return on the first hit.
6. When early-stop conditions are met (or crawl budget exhausted), pick the best candidate from the pool.

Trust boundaries, depth &lt; 2, queue ≤ 128, and ≤ 96 pages checked remain.

## Behavior

### 1. Stale path detection

Case-insensitive URL/path checks (avoid false positives on words like `download` / `threshold`):

- Path contains `/attic/` or a path segment `attic`
- Path contains `older` (covers `older_versions_are_in_the_attic`)
- A path segment equals `old` (not a substring inside unrelated tokens)

Effect: `stale_penalty = 1`. Candidates and queue items are **not** dropped; they only lose ranking. If every surviving candidate is stale, still select the best stale one so install does not fail when only archives remain.

### 2. Autoindex mtime parsing

Detect directory index HTML (existing Apache-style tables with Name / Last modified columns is enough for KDE mirrors).

Parse timestamps such as `2026-06-02 09:22` into a comparable integer (UTC assumed if no zone). Failed parses → mtime unknown.

When enqueueing a child of an autoindex page, inherit that row’s mtime when available. AppImage candidates discovered under that directory inherit the same listing mtime when possible.

Non-autoindex pages keep today’s link extraction; missing mtime is fine.

### 3. Queue ordering

Replace KDE `std::reverse` with the global queue comparator above. Seeds (official hints, source, homepage) keep high seed priority; among equal priority, mtime and stale penalty still apply when known (hints usually have unknown mtime and zero penalty).

### 4. Candidate pool and early-stop

Stop returning immediately when the first AppImage is found.

Early-stop when **either**:

1. Page budget or candidate soft cap is hit, **or**
2. At least one **non-stale** AppImage candidate exists, **and** no queued, not-yet-seen, non-stale page has a **newer known** mtime than the best non-stale candidate’s mtime (pages with unknown mtime do not block stop once a non-stale candidate exists).

Practical caps (implementation may tune slightly if smoke needs it):

- Keep existing `kWebsiteMaxPages` (96)
- Candidate pool soft cap ≈ 8 AppImage URLs before preferring to finalize

Batch concurrency (4) and deterministic per-batch processing order stay; merge candidates in batch selection order so selection is stable.

### 5. Final selection

Among candidates with `appimage_asset_score` ≥ 0:

1. Prefer `stale_penalty == 0` over `1`
2. Prefer newer known mtime; unknown mtime loses to any known mtime
3. If top contenders still lack mtime, HEAD up to **4** of them for `Last-Modified` (failures → remain unknown)
4. Tie-break with existing basename / arch score, then first-seen order

`best_appimage_url_from_candidates` (or a website-specific wrapper) must gain access to per-URL metadata (stale + mtime), not only the bare URL list.

### 6. Failure / degradation

| Situation | Behavior |
|-----------|----------|
| Page is not autoindex | Existing crawl + priority; no error |
| Listing has no parseable dates | Unknown mtime; priority/seq still work |
| HEAD missing `Last-Modified` | Candidate stays unknown mtime |
| All candidates stale | Select best stale candidate |
| No AppImage at all | Existing error: no AppImage download link found |

## Expected example (Krita)

Fixture or live shape of `…/stable/krita/`:

| Entry | Last modified | Role |
|-------|---------------|------|
| `5.3.2.1/` | newer | Preferred version dir |
| `6.0.2.1/` | slightly older | Secondary |
| `6.0.2/` | older | Deprioritized |
| `older_versions_are_in_the_attic` | n/a | `stale_penalty = 1` |

Selection should resolve to `krita-5.3.2.1-x86_64.AppImage` (matching arch), not `6.0.2` and not attic content.

## Testing

Local `file://` fixtures (no required network):

1. **Mtime wins over folder name:** listing with newer `5.3.2.1/` and older `6.0.2/`, each with an arch-matching AppImage → selects 5.3.2.1.
2. **Stale demotion:** same as above plus an attic/older AppImage that would otherwise match → non-stale wins.
3. **Stale-only fallback:** only attic/older AppImages → still selects one (no hard failure).
4. **Non-directory regression:** ordinary project download HTML without autoindex → still finds AppImage; existing `appimage_feed_smoke` website assertions keep passing (update counts only if early-stop/pool changes them).

Unit-style checks (smoke compile harness, same pattern as other resolver smokes):

- mtime string parse
- stale path classification (positive + false-positive guards)
- candidate comparator ordering

## Non-goals reminder

Do not use semver as the primary newest rule; do not change GitHub asset scoring; do not follow Krita Plus/Next nightlies as stable website results.
