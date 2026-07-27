# Final-review fix report: prefer-index Critical + Important

## Status

**DONE**

## Summary

Fixed prefer-index short-circuiting update/upgrade website resolve, and restored `github_*` identity metadata when preferring an index URL for `github_release` packages without forcing mirror transport on non-GitHub direct URLs.

## Findings addressed

### Critical: Prefer index URL must not short-circuit update/upgrade website resolve

`resolve_repo_package_install_source` preferred index download URLs whenever `!recrawl`. Update/upgrade already sets `id_explicit && name_explicit && arch_explicit` (same signal used to skip website disk cache). Prefer-index now uses that same skip condition: when those three are explicit, resolve via original `source` (crawl/API).

### Important: Prefer path for github_release must still fill github_* metadata

When preferring an index URL for `github_release`, set `github_owner` / `github_repo` (and asset basename when known) from `package.source_owner` / `package.source_repo`. Download still uses the index URL.

`source_uses_github_release_download` no longer treats `github_owner` alone as GitHub Release transport: mirror applies when the URL targets GitHub/githubusercontent, or when `source_kind == "github_release"` (live owner/repo resolve, including file:// fixtures). Prefer-index `repo_github_release` + non-GitHub URL stays direct.

## Commits

- `48721bc` Fix prefer-index for update resolve and github metadata.

## Files changed

| Path | Change |
|------|--------|
| `src/resolver.cpp` | Prefer-index gated by update-style explicit flags; fill github_*; tighten mirror gate |
| `tests/repo_resolve_index_smoke.sh` | Regressions for update-style crawl + github_* prefer metadata |

## Test summary

- `bash tests/repo_resolve_index_smoke.sh` — PASS
  - repo index url unit smoke passed
  - repo index persist merge smoke passed
  - repo index prefer/recrawl/fallback smoke passed
  - repo index prefer final-review unit smoke passed
  - repo index final-review prefer regressions passed
  - repo resolve command smoke passed
- `make -j$(nproc)` — PASS

## Self-review

No remaining Critical/Important defects from these two findings.
