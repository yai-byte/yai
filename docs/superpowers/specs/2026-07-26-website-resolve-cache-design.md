# Website resolve cache (install + update)

**Date:** 2026-07-26  
**Status:** Approved  
**Goal:** Avoid full `website_page` crawls when a previously resolved AppImage URL is still valid—on **update/upgrade** via installed metadata + `probe_url_freshness`, and on **install** via a disk resolve cache with TTL and probe validation.

## Problem

`resolve_website_appimage_download` is still invoked on every `repo_website_page` install and on every update/upgrade resolve (`resolve_repo_update_source` → full crawl). Installed packages already persist `download_url` and HTTP validators in metadata; `repo_direct_url` / `url` already short-circuit with `probe_url_freshness`, but `repo_website_page` does not. Fresh installs also re-crawl even when the same package/source/arch was resolved recently on this machine.

## Decisions (locked)

| Topic | Choice |
|-------|--------|
| Approach | Split paths: update uses metadata + freshness; install uses disk resolve cache |
| Update short-circuit | **Amended:** update/upgrade always re-crawls (old AppImage Unchanged must not hide deeper landing/listing moves). Skip **download** when resolved identity matches. Install still uses disk cache. |
| Install cache key | `package_id` + `normalize_arch(arch)` + repo `source.url` (normalized) |
| Cache location | `~/.local/share/yai/website-resolve-cache.json` (single JSON map/list) |
| TTL | Default **7 days** from `resolved_at`; expired = miss |
| Cache miss / invalid | Fall back to existing full crawl; on success rewrite cache entry |
| Source of truth for installed apps | Metadata only; disk cache is an acceleration hint |
| Remove package | Does **not** require clearing disk cache |
| GitHub / direct_url | Unchanged |

## Scope

**In scope**

- Change `repo_website_page` update/upgrade (and update preview) to try freshness on stored `download_url` before full website resolve.
- Add read/write helpers for a per-user website resolve disk cache.
- On install resolve: lookup → validate cached URL → use or crawl; write cache after successful crawl (and after update re-crawl).
- Smoke tests with `file://` fixtures and optional fake-curl logs proving crawl skip.
- Allow updating hard-coded smoke counts only if progress wording changes.

**Out of scope**

- libcurl multi / raising website fetch concurrency
- Semver-based newest selection
- Cross-machine cache sync
- Clearing cache on `yai remove` (may add doctor cleanup later)
- Changing GitHub release resolution or `direct_url` freshness (already correct)
- Treating disk cache as authoritative for installed packages

## Architecture

Stay close to existing modules:

| Piece | Role |
|-------|------|
| `url_freshness` / `probe_url_freshness` | Validate cached or metadata URLs (HEAD + validators) |
| New small helpers (e.g. in `resolver_website.cpp` or `website_resolve_cache.cpp`) | Load/store cache JSON; key build; TTL check |
| `repo_website_page_source` / `resolve_website_appimage_download` callers | Install: cache lookup before crawl |
| `commands_update.cpp` / `commands_upgrade.cpp` | Update: freshness before `resolve_repo_update_source` for `repo_website_page` |

**Data flow — install**

1. Build cache key from package id, target arch, package `source_url`.
2. If cache hit and not past TTL: probe/validate `download_url` (`file://` → existence / readable; remote → `probe_url_freshness` or HEAD that the URL still looks like a reachable AppImage).
3. If validation OK → return that URL (skip crawl).
4. Else → `resolve_website_appimage_download` as today → on success upsert cache entry with `resolved_at=now`.

**Data flow — update/upgrade (`repo_website_page`)**

1. Always call `resolve_website_appimage_download` (via existing resolve helpers). Do **not** skip crawl based solely on AppImage `probe_url_freshness` Unchanged: catalog/landing hops may point at a newer asset while the previous AppImage file still exists.
2. Compare resolved identity to installed; if unchanged → already up to date (no re-download).
3. If changed → existing download/commit path.
4. On successful re-resolve, **upsert disk cache** for that package key so later installs see the new URL.

## Behavior

### 1. Cache key and file

- Path: `expand_home_path(".local/share/yai/website-resolve-cache.json")`.
- Key string (implementation may hash or use a composite object): normalized `package_id`, `arch`, `source_url` (strip fragment/query consistently with other URL helpers).
- Entry minimum fields: `package_id`, `arch`, `source_url`, `download_url`, `resolved_at` (UTC ISO or epoch seconds—pick one and use everywhere).
- Optional: store last `http_etag` / `http_last_modified` / `http_content_length` when known from probe/download to improve the next probe.

Corrupt or missing file → treat as empty cache (no hard fail).

### 2. TTL

- Default TTL = **7 × 24 hours** from `resolved_at`.
- Expired entry = cache miss (may leave stale JSON until overwritten).
- No background janitor required in this change set.

### 3. Install validation of a hit

Before skipping crawl:

- URL must still look like an AppImage download URL (`is_appimage_download_url`).
- `file://`: path must exist (and ideally be readable).
- `http(s)://`: use `probe_url_freshness` with any stored validators from the cache entry if present; **Unchanged** or successful reachability that does not indicate a missing resource → accept. **Error** that indicates missing/gone, or clear failure to use the URL → miss and crawl.
- Do not download the full AppImage body solely to validate cache on install resolve.

### 4. Update short-circuit

For `source_kind == "repo_website_page"`:

- Prefer metadata validators + `download_url` (already written at install).
- **Unchanged** → do not call `resolve_website_appimage_download`.
- Otherwise resolve via crawl; then proceed with existing identity/download logic.
- Update preview (`yai update`) must use the same short-circuit so preview stays fast and consistent with upgrade.

### 5. When to write the disk cache

| Event | Write disk cache? |
|-------|-------------------|
| Install crawl success | Yes |
| Install cache hit used | Optional touch of `resolved_at` — **no** (keep TTL honest) |
| Update Unchanged | Optional best-effort — **not required** |
| Update re-crawl success | Yes (upsert) |
| `yai remove` | No |

### 6. Degradation

| Situation | Behavior |
|-----------|----------|
| No cache file | Crawl (install); freshness-only on update if metadata has URL |
| TTL expired | Crawl + rewrite |
| Cached URL dead | Crawl + rewrite |
| Freshness Unchanged on update | Skip crawl |
| Crawl finds nothing | Existing error; do not write a successful cache entry |
| `file://` fixtures in tests | Must exercise both hit and miss without network |

## Testing

Local fixtures (`file://`) plus fake-curl log where useful:

1. **Update Unchanged skips crawl:** Install website package; second `update`/`upgrade` with validators that probe Unchanged → assert no listing/page fetches for the site (fake-curl or progress).
2. **Update after URL change:** Old AppImage gone or probe Changed → re-resolve finds new fixture AppImage and upgrades (or reports upgradable).
3. **Install cache hit:** Resolve once (or install once), remove app but keep cache file, install again → second resolve skips crawl.
4. **TTL miss:** Entry with old `resolved_at` → crawl runs again.
5. **Invalid cached URL:** Cache points at missing file → crawl recovery.
6. **Regression:** `website_mtime_smoke.sh`, `appimage_feed_smoke.sh`, and relevant update/url freshness smokes still pass.

## Files likely touched

- `src/yai.hpp` — cache API declarations if shared
- New or existing: `src/website_resolve_cache.cpp` (or helpers inside resolver/update)
- `src/resolver.cpp` / `src/resolver_website.cpp` — install resolve cache wrap
- `src/commands_update.cpp` / `src/commands_upgrade.cpp` — website update short-circuit
- `src/url_freshness.cpp` — only if shared validation helpers need extension
- `Makefile` — new source if added
- `tests/` — new smoke and/or extensions to website/update smokes

## Non-goals reminder

Disk cache is not a substitute for metadata; no libcurl multi; no semver-primary ranking; GitHub paths unchanged.
