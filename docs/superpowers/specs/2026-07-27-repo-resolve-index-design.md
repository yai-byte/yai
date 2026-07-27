# Repo resolve → enrich index.json with AppImage download URLs

**Date:** 2026-07-27  
**Status:** Approved  
**Goal:** Add a command that bulk-resolves AppImage direct download URLs into the repo `index.json`, so the enriched index can be shared and reused; wire install/download to prefer and (when missing) fill those fields without overwriting existing ones.

## Problem

Repo `index.json` describes packages (`github_release` / `website_page` / `direct_url`) but usually does **not** store the final `.AppImage` URL. Resolving `website_page` requires crawling; GitHub needs the Releases API. Each machine (or each install) repeats that work. There is already a local `website-resolve-cache.json`, but it is not the shareable catalog format and does not cover a full export into `index.json`.

## Decisions (locked)

| Topic | Choice |
|-------|--------|
| Approach | Bulk `yai repo resolve` + extend existing `index.json` package objects with download URL fields |
| Audience | Shareable enriched index primarily; local use as well |
| Schema | Add `download_url` / `download_urls` / `resolved_at` on packages; keep original `source` unchanged |
| Default arch | Current machine arch; `--arch all` writes multi-arch map |
| Filters | Configurable: `--type`, `--package`; default = all resolvable packages |
| Output | Update local `~/.local/share/yai/repos/index.json` in place; optional `--output <path>` export |
| Consume | Prefer index direct URL as `direct_url`; on failure fall back to original `source` |
| Write-back (install/download) | On successful resolve, write index URL **only if that arch has no URL yet** |
| Recrawl flag | `--recrawl`: ignore index URL, resolve via original `source`, **never overwrite** existing index URLs |
| Bulk overwrite | `repo resolve --overwrite` replaces existing URLs; default **no overwrite** (skip crawl when URL present) |
| Resolve reporting | `--show XYZ` (success/skip/fail bits); `--summary` / `--no-summary`; default `--show 001` + summary on |
| Local website cache | Remains an acceleration hint; shareable source of truth for others is enriched `index.json` |

## Scope

**In scope**

- New `yai repo resolve` subcommand (filters, arch, overwrite, output, show/summary).
- Extend index package schema with `download_url`, `download_urls`, `resolved_at`.
- install/download: prefer index URLs; write-back when missing; `--recrawl` to ignore without overwrite.
- Load/save helpers for the new fields compatible with existing index parse/normalize.
- Smoke/unit tests for skip/overwrite/recrawl/read priority/`--show` parsing.
- i18n strings for new CLI help and errors (en + zh), consistent with existing commands.

**Out of scope**

- Replacing or removing `website-resolve-cache.json`
- Changing how GitHub asset scoring or website crawl algorithms work
- Auto-publishing / CDN upload of enriched index
- Semver comparison of AppImage filenames beyond existing resolve behavior
- Clearing index URLs on `yai remove`

## Architecture

| Piece | Role |
|-------|------|
| `commands_repo.cpp` (or small new `commands_repo_resolve.cpp`) | CLI for `yai repo resolve` |
| Existing `resolve_*` / `resolve_install_source` path | Per-package resolution reused by bulk command |
| `repo.cpp` / feed normalize | Parse/write `download_url`, `download_urls`, `resolved_at` |
| install/download in lifecycle commands | Prefer index URL; write-back-if-absent; honor `--recrawl` |
| `RepoPackage` in `yai.hpp` | Optional fields for resolved URLs |

**Data flow — `yai repo resolve`**

1. Load local repo index packages.
2. Filter by `--type` / `--package`. Default “resolvable” types: `github_release`, `website_page`, `direct_url`. Skip `unavailable` (and any unknown type) unless explicitly requested and handled.
3. For each package × requested arch(es):
   - If no `--overwrite` and index already has URL for that arch → **skip** (no network).
   - Else resolve via original `source` (reuse existing resolvers; website path may still hit disk cache). For `direct_url`, the resolved download URL is the package `source.url` (no crawl).
   - Success → write `download_url` / `download_urls` + `resolved_at`.
   - Failure → record for reporting; leave existing fields untouched.
4. Persist local index; if `--output` set, write a copy there.
5. Print per `--show` / summary flags; exit non-zero if any failure.

**Data flow — install / download (no `--recrawl`)**

1. Resolve package from index.
2. If index has usable URL for requested arch → treat as direct download URL.
3. If download/validation fails → fall back to original `source` resolve.
4. If no index URL → resolve via original `source`.
5. After a successful resolve that produced a download URL: if that arch still has **no** index URL → write it back; if already present → do not write.

**Data flow — install / download with `--recrawl`**

1. Ignore index download URL fields.
2. Resolve via original `source`.
3. Never overwrite an existing index URL for that arch.
4. If that arch had no URL and resolve succeeded → may write-back (same “fill if absent” rule).

## Behavior

### 1. Index schema extensions

On each package object (additive, backward compatible):

```json
{
  "id": "krita",
  "source": { "type": "website_page", "url": "https://..." },
  "download_url": "https://example/krita-x86_64.AppImage",
  "download_urls": {
    "x86_64": "https://example/krita-x86_64.AppImage",
    "aarch64": "https://example/krita-aarch64.AppImage"
  },
  "resolved_at": "2026-07-27T09:00:00Z"
}
```

- Single-arch resolve (default): set `download_url`; if `download_urls` exists, also set `download_urls[arch]`.
- `--arch all`: set/update `download_urls` for each arch resolved; also mirror the host/default arch into `download_url` when that arch succeeds.
- `resolved_at`: UTC ISO-8601 timestamp of the last successful write for that package’s URL fields.
- Missing fields = not resolved; old indexes remain valid.

### 2. Read priority (requested arch)

1. If `download_urls[requested_arch]` exists → use it.
2. Else if `download_url` exists and `download_urls` is absent → use `download_url` (single-arch export compatibility).
3. Else if `download_url` exists but `download_urls` is present and lacks `requested_arch` → **do not** use `download_url`; fall back to original `source`.
4. Else → no index URL; use original `source`.

### 3. CLI — `yai repo resolve`

```text
yai repo resolve [options]
```

| Option | Meaning | Default |
|--------|---------|---------|
| `--output <path>` | Also write enriched index to path | off (local index still updated) |
| `--arch <arch\|all>` | Architectures to resolve | current machine arch |
| `--type <source_type>` | Filter source type (repeatable) | all resolvable |
| `--package <id>` | Filter package id (repeatable) | all |
| `--overwrite` | Re-resolve and replace existing URLs | off (skip if URL present) |
| `--concurrency <n>` | Parallelism hint where applicable | conservative default aligned with website resolve |
| `--show XYZ` | Print success/skip/fail lines (`0`/`1` bits) | `001` |
| `--summary` | Print totals line | on |
| `--no-summary` | Suppress totals line | — |

**`--show XYZ`**

| Bit | Controls |
|-----|----------|
| 1st | Print **success** items |
| 2nd | Print **skipped** items |
| 3rd | Print **failed** items |

- Default `001`: only failure ids (plus reason on the same or following detail line).
- Invalid value (not exactly three `0`/`1` chars) → error exit.
- Summary line format: `resolved: N skipped: M failed: K` (wording may follow i18n).
- Exit code: non-zero if `failed > 0`; else zero.

### 4. CLI — install / download

| Option | Meaning |
|--------|---------|
| `--recrawl` | Ignore index download URLs; resolve via original `source`; never overwrite existing index URLs |

No other new flags required for the prefer-and-fill-if-absent behavior (that is default).

### 5. Interaction with `website-resolve-cache.json`

- Unchanged as a local crawl accelerator.
- Enriched `index.json` is what gets shared; consumers do not need the disk cache file.
- Preferring an index `download_url` skips crawl **and** does not require a cache hit.
- `--recrawl` / `repo resolve --overwrite` may still update the disk cache on successful website resolve (existing cache upsert behavior), but must not overwrite index URLs unless the bulk `--overwrite` path applies (`--recrawl` never overwrites index URLs).

## Error handling

- Per-package resolve failure in `repo resolve`: continue; leave that package’s URL fields as they were; include in failure report when `--show` bit 3 is set.
- Index URL used on install/download but download fails: fall back to original `source`; if that also fails, existing error path.
- `--recrawl` failure: do not modify existing index URLs.
- Corrupt index on load: existing repo load error behavior.
- Illegal `--show` / unknown options: immediate usage error.

## Testing

1. Package without URL → successful resolve/install writes `download_url` (and arch map key when applicable).
2. Package with URL → default `repo resolve` skips (no network needed in fixture); install does not overwrite.
3. `repo resolve --overwrite` replaces URL.
4. `--recrawl` ignores index URL and does not overwrite when URL already present; fills when absent.
5. Read priority cases for `download_url` vs `download_urls[arch]`.
6. `--show` / `--summary` / `--no-summary` output and non-zero exit on failures.
7. `--output` writes a second copy while local index is updated.

## Success criteria

- `yai repo resolve` can produce a shareable `index.json` with direct AppImage URLs for filtered or full package sets.
- Others using that index skip crawl/API when URLs are present, with safe fallback.
- Default paths never clobber existing index URLs; explicit `--overwrite` is required for bulk replace; `--recrawl` never clobbers.
