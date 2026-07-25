# Final review fix — wildcard multi-match

**Date:** 2026-07-25  
**Workdir:** `/home/fsx/yai/.worktrees/wildcard-multi-match`  
**Branch:** `feature/wildcard-multi-match`  
**Status:** PASS

## Findings addressed

### Critical — URL / non-repo targets must not expand as repo globs

`parse_batch_command` in `src/main.cpp` previously called `find_repo_packages` on any target containing `*`/`?`. Query-string URLs such as `https://cdn.example.com/app.AppImage?token=1` therefore hit repo glob expansion and failed with `package pattern matched no repo packages`.

**Fix:** Expand only when `has_glob_wildcards(target) && looks_like_repo_package_target(target)`. URLs, GitHub `owner/repo`, and local AppImage paths are left as ordinary targets.

### Important — unique-match globs keep the single-target path

The same loop forced `batch.requested = true` for every glob, so a unique pattern (expand to 1) entered the batch / child-process path and skipped interactive single-target behavior (e.g. GitHub proxy prompts).

**Fix:** Set `batch.expanded_multi` only when a pattern expands to N>1. Do not set `batch.requested` merely because a target has glob characters. Unique matches fall through to the normal `install`/`download` handler via original argv. Explicit multi-argv or `--jobs` still request batch as before.

### Smoke — URL with `?` is not a repo glob

Extended `tests/wildcard_multi_smoke.sh`:

- Unique `install 'pack-a*'` must not emit multi-match confirm / `yai: running` batch banner.
- `download "file://…/PackA.AppImage?token=1"` must not report `package pattern matched no repo packages` (download may still fail for local path+query; non-glob failure is acceptable).

## Test evidence

Commands (all exit 0):

```bash
make yai
bash tests/wildcard_multi_smoke.sh
bash tests/stage4_smoke.sh
bash tests/download_smoke.sh
```

Observed:

- `wildcard multi smoke passed`
- `stage4 smoke test passed`
- `download smoke test passed`

## Files

- `src/main.cpp` — gate + unique-match batch request fix
- `tests/wildcard_multi_smoke.sh` — unique-match + URL `?` assertions
- `.superpowers/sdd/final-review-fix.md` — this report
