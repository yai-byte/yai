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
- Persist to `repo_index_path()` when writable; optional `--output` copy
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

## Commits

See git log on `feature/repo-resolve-index` after this task (feature commit SHA filled at commit time).

## Out of scope (as required)

- Full po sync (Task 5); English `tr()` strings introduced for resolve CLI only

## Concerns

1. **Concurrency line order** is nondeterministic when `--concurrency > 1`; default `1` keeps smoke deterministic.
2. **Fail lines** use `fail <id> <arch>: <reason>` on stdout; the final exception text does not list package ids (ids come from `--show`).
3. **Task 5** still needed to sync new msgids into `po/en.po` / `po/zh.po`.
