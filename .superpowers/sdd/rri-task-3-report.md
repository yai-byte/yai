# Task 3 Report: install/download prefer index URL, `--recrawl`, write-back, fallback

## Status

**PASS** — prefer-index resolve, `--recrawl`, write-back-if-absent, and staging fallback implemented; smoke green on `feature/repo-resolve-index`. **Not committed** (commits not requested).

## What landed

### CLI (`src/yai.hpp`, `src/cli_download.cpp`, `src/main.cpp`)

- `InstallOptions::recrawl = false`
- `--recrawl` parsed in `parse_common_download_option` (install + download)
- Usage banners mention `--recrawl`
- `main.cpp` batch argv scanner accepts `--recrawl` as a flag (otherwise unknown-option reject before command handlers)

### Resolver (`src/resolver.cpp`)

`resolve_repo_package_install_source(options, package)`:

1. If `!recrawl` and index has URL for install arch → return `ResolvedSource` with that URL (`source_kind` mapped from `source_type`; `github_*` left empty for direct download path)
2. Else existing `direct_url` / `unavailable` / `website_page` / `github_release` branches

### Lifecycle (`src/commands_lifecycle.cpp`)

Shared helpers for install + download:

- `inspect_repo_index_url_state` — capture had/lacked index URL for arch before resolve
- `stage_resolved_source_with_index_fallback` — stage once; on failure if `!recrawl` and package had index URL → retry once with `recrawl=true`
- `maybe_write_back_index_download_url` — if lacked URL before, locally writable index, and staging produced a URL → `repo_package_set_download_url(..., overwrite=false)` + `upsert_repo_package_download_urls`

## Tests

Command: `bash tests/repo_resolve_index_smoke.sh`

| Section | Result |
|---------|--------|
| Task 1 url unit | `repo index url unit smoke passed` |
| Task 2 persist/merge | `repo index persist merge smoke passed` |
| Task 3 prefer/recrawl/fallback | `repo index prefer/recrawl/fallback smoke passed` |

Task 3 coverage (`file://` website_page fixture):

1. First `yai download` (no index URL) succeeds and write-backs `download_url`
2. HTML swapped to v2 AppImage; without `--recrawl` still downloads v1; index URL unchanged
3. `--recrawl` downloads v2; index URL still unchanged
4. Broken index URL → fallback crawl succeeds; broken URL left in index
5. Unknown option still rejected

## Commits

None (user/brief: commit only if asked).

## Out of scope (as required)

- `yai repo resolve` (Task 4)
- Full po sync (Task 5); English `tr()` usage strings for `--recrawl` added

## Concerns

1. **Upgrade/update paths** do not yet prefer index URLs / write-back / fallback — Task 3 scoped to install/download lifecycle only.
2. **Fallback is silent** — first staging error is discarded when retry succeeds; no user-facing “falling back to source resolve” line.
3. **`main.cpp` change required** beyond the brief file list so `--recrawl` is not rejected by the batch option pre-scanner.
4. Smoke uses `bash` to run downloaded fake AppImages because `yai download` does not chmod +x.

## Review follow-up: single network-config apply

**Finding:** `download_app` / `install_app` called `apply_network_config_to_options` for banners, then `stage_resolved_source_with_index_fallback` applied again on raw `options` (TTY could double-prompt for GitHub mirror; recrawl retry could apply a third time).

**Fix:** Staging helper now takes already-applied `effective_options` for the first attempt. Recrawl retry still calls `apply_network_config_to_options(retry, source)` once after re-resolve.

**Verification:** `make -j$(nproc)` + `bash tests/repo_resolve_index_smoke.sh` — all three smoke sections passed.

**Commit:** `2795076256221ab263b0648cb6ee4b842cab66a9`
