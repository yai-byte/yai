# Direct URL and repo_direct_url update via HTTP freshness

**Date:** 2026-07-25  
**Status:** Approved  
**Goal:** Let `yai update` / `yai upgrade` work for plain URL installs, and detect same-URL content changes for `repo_direct_url` packages, using HTTP validators first and sha256 as fallback.

## Problem

- Plain `yai install <url>` already persists `source_url` / `download_url` in `metadata.json`, but `update` / `upgrade` mark `source_kind=url` as unsupported (“no stable update source”).
- `repo_direct_url` upgrades only compare repo-index identity (version string or `source_url`). When the index URL is unchanged but the remote file at that URL is replaced, yai reports “already up to date”.
- Users want recorded download URLs to drive fast later upgrades without requiring GitHub Releases.

## Scope

**In scope**

- Enable `update` / `upgrade` for `source_kind=url`
- Enhance `repo_direct_url`: index URL still wins when it changes; when URL is unchanged, probe remote freshness (HTTP headers, then sha256 on download)
- Persist optional HTTP validator fields on install/upgrade success
- Shared URL freshness helper used by preview and upgrade paths
- README / developer-doc notes for the new upgradable kinds

**Out of scope**

- `local_path` upgrades
- Changing `github_release` / `repo_github_release` / `repo_website_page` update semantics
- Discovering new asset URLs from a “release page” (that remains `website_page`)
- Sidecar metadata for `yai download`
- Disk cache of `id → download_url` outside install metadata

## Approach

Shared **URL freshness probe** (preferred over duplicating logic in command files):

1. Resolve candidate URL
2. HEAD (reuse existing header prefetch where possible) and compare ETag / Last-Modified / Content-Length to metadata
3. If headers are missing or inconclusive, require a full download and compare sha256
4. On successful install/upgrade (and when a same-sha256 download still obtained headers), write validators back to metadata for the next cheap probe

## Data model

Existing fields continue to apply: `source_url`, `download_url`, `sha256`, `version`, `source_kind`.

New optional metadata fields (omit or empty when unknown):

| Field | Purpose |
|-------|---------|
| `http_etag` | Strong validator from last successful fetch |
| `http_last_modified` | Last-Modified from last successful fetch |
| `http_content_length` | Content-Length from last successful fetch (string or decimal text consistent with other metadata scalars) |

Semantics:

- `source_url` remains the upstream identity URL (for `url` and for repo packages, the package’s direct URL from the index at install/last upgrade).
- `download_url` remains the URL actually fetched (mirrors, redirects as today).
- Freshness comparison for direct-url kinds prefers validators + sha256; **version string equality alone must not decide** upgradability.

## Behavior

### 1. Candidate URL resolution

| `source_kind` | Candidate URL |
|---------------|---------------|
| `url` | `source_url`, else `download_url` |
| `repo_direct_url` | Re-resolve package by installed id via repo index; use the package’s current `direct_url` / resolved `source_url` |

For `repo_direct_url`:

- If resolved index URL ≠ installed `source_url` → treat as **upgradable** (new URL), without requiring header mismatch.
- If URLs are equal → run freshness probe against that URL.

### 2. Freshness probe outcomes

Compare HEAD (or equivalent dumped response headers) to stored `http_*` fields:

| Condition | Outcome |
|-----------|---------|
| ETag or Last-Modified present and differs from stored | `changed` |
| At least one comparable validator exists and all comparable fields match | `unchanged` |
| No usable validators to compare | `unknown` (needs download + sha256) |
| HEAD/network hard failure (not merely “no validators”) | `error` for `update`; `upgrade` may fall through to GET download then sha256 |

`Content-Length` alone may participate when both sides have it; it must not override a conflicting ETag / Last-Modified signal. Prefer ETag, then Last-Modified, then length as weaker signal when defining “all comparable fields match”.

### 3. `yai update` (preview only)

- `url`: no longer `unsupported`; run candidate URL + freshness probe.
- `repo_direct_url`: index URL change → `upgradable`; else freshness probe.
- Map probe results:
  - `unchanged` → `current` (“already up to date”)
  - `changed` → `upgradable` (reason mentions remote validators / URL)
  - `unknown` → `upgradable` with reason that download verification is required (**do not download the AppImage body during `update`**)
  - `error` → `error`
- Version column: basename-derived `version` may be unchanged when only content changes; status + reason carry the signal (`remote content changed` / `download verification required`).

`upgrade --all` continues to upgrade only ids whose preview status is `upgradable`.

### 4. `yai upgrade`

1. Resolve candidate as above; skip work if a freshness probe already proves `unchanged` (optional optimization; must still be correct if skipped probe would have been `unknown`).
2. Download candidate into the existing upgrade staging path; probe AppImage; keep current transaction (`previous` → activate).
3. Compare candidate sha256 to installed `sha256`:
   - **Equal:** do not activate a new generation; best-effort write refreshed `http_*` from this fetch; report already up to date.
   - **Different:** commit upgrade; write new `sha256`, `source_url` / `download_url`, and `http_*`; keep `version` from existing resolution rules (e.g. URL basename for direct URL / repo direct URL). Same basename after a content change is allowed.
4. Index URL change for `repo_direct_url`: download the new URL and commit on sha256 (or identity) change per existing upgrade transaction; refresh metadata accordingly.
5. Failures during download/probe/commit: existing rollback behavior; leave `current` intact.

### 5. Writing validators on install

When install (or upgrade) completes a remote fetch and response headers are available, populate `http_etag` / `http_last_modified` / `http_content_length` alongside existing metadata. Missing headers are fine; fields stay absent/empty.

## Error handling

- Unsupported kinds unchanged: `local_path` remains unsupported; plain URL is no longer in that set.
- HEAD unsupported or empty validators → preview `upgradable` (verify on upgrade), not `error`.
- True network/resolve failures → `error` on update; upgrade surfaces the download/resolve error.
- sha256 match after forced download → success path “already up to date”, not a failed upgrade.

## Testing

Minimum coverage:

1. `url` install is previewable/upgradable (not `unsupported`)
2. Matching validators → `current`
3. Changed ETag (or Last-Modified) → `upgradable`
4. No headers → preview `upgradable` with verify reason; upgrade uses sha256
5. `repo_direct_url`: index URL change → `upgradable`
6. `repo_direct_url`: same URL, content/validators change → `upgradable`
7. Same URL, same content → `current`; failed mid-upgrade does not replace `current`
8. Successful upgrade persists new `sha256` and `http_*` when headers exist

## Non-goals reminder

No website crawling for plain URL installs; no download sidecars; no change to GitHub release update logic.
