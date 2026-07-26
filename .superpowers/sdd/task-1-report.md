# Task 1 Report: Stale path + listing mtime helpers

## Status

**DONE** — `bash tests/website_mtime_smoke.sh` prints `website mtime unit smoke passed`.

## What Was Implemented

Pure URL/mtime helpers for website AppImage freshness (Task 1 scope only).

### Modified: `src/yai.hpp`

Added near other URL helpers:

- `struct WebsiteLinkMeta { url, mtime, stale_penalty }`
- `bool website_url_looks_stale(const std::string& url)`
- `int website_link_stale_penalty(const std::string& url)`
- `std::optional<std::int64_t> parse_directory_listing_mtime(const std::string& text)`
- `std::optional<std::int64_t> parse_http_last_modified_mtime(const std::string& text)`

### Modified: `src/resolver_url.cpp`

- `website_url_looks_stale` — case-insensitive path scan after `strip_url_fragment_query`; matches `older` substring, `/attic/` or `attic` segment, `old` segment (not substrings in `download`/`threshold`)
- `website_link_stale_penalty` — returns `1` if stale, else `0`
- `parse_directory_listing_mtime` — `YYYY-MM-DD HH:MM` or `YYYY-MM-DD HH:MM:SS` via `sscanf` + `timegm`; rejects trailing junk
- `parse_http_last_modified_mtime` — minimal RFC 1123 parser (`Sun, 02 Jun 2026 09:22:00 GMT`); GMT/UTC only
- Anonymous helpers: `tm_to_utc_epoch`, `trailing_whitespace_only`

### Created: `tests/website_mtime_smoke.sh`

Unit smoke (TDD):

1. RED — compile failed with undeclared symbols (verified)
2. GREEN — all stale-path and listing-mtime assertions pass

Link line expanded beyond brief minimum (resolver_url transitive deps): `repo_feed`, `repo`, `json`, `cli_download`, `url_freshness`, `appimage`, `batch_progress_event`, `batch_ui`, `download_progress`.

## Test Verification

```bash
bash tests/website_mtime_smoke.sh
# website mtime unit smoke passed
```

TDD cycle observed: failing compile (missing declarations) → implement → passing smoke.

## Files Changed

| File | Action |
|------|--------|
| `src/yai.hpp` | Modified (declarations + `WebsiteLinkMeta`) |
| `src/resolver_url.cpp` | Modified (implementations) |
| `tests/website_mtime_smoke.sh` | Created |

## Self-Review

1. **Stale rules** — Matches brief verbatim; segment split avoids `download`/`threshold` false positives.
2. **Listing mtime** — UTC via `timegm`; order test (`2026-06-02` > `2026-05-26`) passes; invalid input returns `nullopt`.
3. **HTTP Last-Modified** — Declared and implemented for later tasks; not covered by unit smoke yet.
4. **Scope** — No crawler/HTML wiring (later tasks).
5. **Link deps** — Smoke links many TUs because `resolver_url.cpp` references `allowed_website_hosts` → `strip_unexpanded_url_placeholder` and `process.cpp` batch/download symbols. Acceptable for now; a dedicated small TU could shrink this in a follow-up.

## Concerns

1. **`parse_http_last_modified_mtime` untested** — Implemented per interface list; Task 2+ should add smoke coverage.
2. **Heavy smoke link set** — 13 source files for a unit test; inherited from monolithic `resolver_url.cpp` / `process.cpp` coupling.

## Commits

One commit on `feature/website-mtime-candidate` (see git log after commit).
