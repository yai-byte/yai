# Task 4 Report: `yai repo resolve` command

## Status

**PASS** — `yai repo resolve` implemented with filters, `--overwrite`, `--show`/`--summary`, `--output`, concurrency, and non-zero exit on failures; full smoke green on `feature/repo-resolve-index`.

## What landed

### CLI / types (`src/yai.hpp`, `src/commands_repo.cpp`, `src/commands_repo_resolve.cpp`, `Makefile`)

- `RepoResolveOptions` + `parse_repo_resolve_options` / `repo_resolve_app`
- Flags: `--output`, `--arch`/`all`, `--type`, `--package`, `--overwrite`, `--concurrency`, `--show XYZ` (default `001`), `--summary`/`--no-summary`
- `repo_app` dispatches `resolve`; help string includes resolve
- New translation unit `commands_repo_resolve.cpp`

### Resolve core

- Skip before network when URL present and not `--overwrite`
- Bulk resolve always uses `recrawl=true` (original source); overwrite only controls write
- Persist to `repo_index_path()` when writable; then `upsert_repo_package_download_urls` for each successfully updated package (named caches); optional `--output` copy
- On any failure: print per `--show`, summary unless `--no-summary`, then throw `repo resolve completed with failures`

### Supporting

- `canonical_arches()` in `src/arch.cpp`
- Export `resolve_repo_package_install_source(options, package)` from `src/resolver.cpp`

## Tests

Command: `bash tests/repo_resolve_index_smoke.sh`

| Section | Result |
|---------|--------|
| Task 1 url unit | passed |
| Task 2 persist/merge | passed |
| Task 3 prefer/recrawl/fallback | passed |
| Task 4 repo resolve | `repo resolve command smoke passed` |

Task 4 coverage:

1. filled skipped / empty resolved; summary `resolved: 1 skipped: 1 failed: 0`
2. `--show 000 --no-summary` quiet
3. `--overwrite` replaces filled URL from `source_url`
4. `--output` writes file; local index updated
5. dead `website_page` → non-zero exit; id printed under default `--show 001`
6. named cache + resolve + `repo update` merge retains URLs

## Commits

`6c4edcb2648b1f65ea5315188b656d677ae9bed6`

Follow-up (Important finding): `0f5499726f7dbf1c1f2592462bcec7d1d0e1f0b1`

## Out of scope (as required)

- Full po sync (Task 5); English `tr()` strings introduced for resolve CLI only

## Concerns

1. **Concurrency line order** is nondeterministic when `--concurrency > 1`; default `1` keeps smoke deterministic.
2. **Fail lines** use `fail <id> <arch>: <reason>` on stdout; the final exception text does not list package ids (ids come from `--show`).
3. **Task 5** still needed to sync new msgids into `po/en.po` / `po/zh.po`.

## Follow-up fix (Important finding)

**Issue:** `yai repo resolve` only called `save_repo_packages_index` on the combined `index.json`. Named repo caches (`repos/<name>.json`) were left without `download_url` / `download_urls`, so `repo update` rebuild wiped resolved URLs.

**Fix:** After successful URL writes, save the full in-memory package list to the combined index, then call `upsert_repo_package_download_urls(updated_package)` for each package that wrote at least one URL (patches named caches; combined stays consistent). `--output` still writes the full in-memory snapshot.

**Smoke:** Task 4 section 6 — `repo add` → `repo resolve` → assert named+combined URLs → rewrite feed without URL fields → `repo update` → URLs retained.

**Verification:** `bash tests/repo_resolve_index_smoke.sh` + `make -j$(nproc)` — green. Commit: `0f54997`.
