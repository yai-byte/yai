# Task 2 Report: Install resolve uses disk cache

## Status

**Complete.** Install path for `repo_website_page` packages uses the disk cache for hit/miss/upsert; invalid cache entries fall back to crawl.

## Commits

- (this commit) Wire install resolve to website disk cache with integration smoke

Base: `d139592` (Task 1 helpers)

## Changes

### `src/resolver.cpp`

- `repo_website_page_source` loads cache keyed by `package.id`, effective arch, and **`RepoPackage.source_url`** (listing URL).
- Valid hit (fresh TTL + usable `download_url`) skips `resolve_website_appimage_download`.
- Miss or unusable entry crawls, then upserts cache.
- Cache is **install-only**: skipped when `id/name/arch` are all explicit (update/upgrade resolution path) until Task 3 freshness short-circuit.

### `tests/website_resolve_cache_smoke.sh`

- Integration fixture under `$TMP/download/cache-site/`.
- First install crawls; second install skips listing `index.html` fetch (fake-curl log).
- Poisoned `download_url` triggers crawl recovery.
- Clears unit-test cache pollution before integration; poisons by `package_id`.

## Test summary

```
make -j$(nproc)                                    # OK
bash tests/website_resolve_cache_smoke.sh         # unit + install integration OK
YAI_LANG=en bash tests/appimage_feed_smoke.sh     # OK (no regressions)
```

## Concerns / follow-ups (Task 3)

- Update/upgrade still always crawl today (cache bypass via explicit id/name/arch heuristic).
- Task 3 should replace the heuristic with `probe_url_freshness` short-circuit and cache upsert on re-resolve.
- `ResolvedSource.source_url` still stores AppImage URL at install (metadata unchanged); cache key correctly uses listing URL.

## Verification commands

```bash
cd /home/fsx/yai/.worktrees/website-resolve-cache
make -j"$(nproc)"
bash tests/website_resolve_cache_smoke.sh
YAI_LANG=en bash tests/appimage_feed_smoke.sh
```
