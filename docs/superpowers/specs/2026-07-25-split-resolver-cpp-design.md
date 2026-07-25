# Split resolver.cpp by responsibility (approach 2)

**Date:** 2026-07-25  
**Status:** Approved (scope A + approach 2 layout)  
**Goal:** Split `src/resolver.cpp` (~1188 lines) into focused translation units for GitHub/mirror download, URL/HTML utilities, website crawling, and install-source dispatch — without changing public API or runtime behavior.

## Scope

**In scope**

- Create:
  - `src/resolver_github.cpp`
  - `src/resolver_url.cpp`
  - `src/resolver_website.cpp`
- Leave a thinner `src/resolver.cpp` for package-match helpers, arch helpers, staging, `resolve_install_source` dispatch, and network prompts
- Update `Makefile` `SRC`
- Keep all declarations in existing `src/yai.hpp` (no header split)

**Out of scope**

- Changing function signatures, behavior, or website crawl algorithm
- Deduplicating URL/catalog patterns (noted earlier as a separate optimization)
- Splitting `yai.hpp`
- New features

## Architecture

Same mechanical-split pattern as `core.cpp` / `commands.cpp`. Call sites keep `#include "yai.hpp"`.

```
yai.hpp (unchanged public surface)
   ├── resolver.cpp            (match helpers, stage, resolve_* dispatch, network prompts)
   ├── resolver_github.cpp     (GitHub release + mirror download strategy)
   ├── resolver_url.cpp        (URL/HTML parsing and candidate scoring helpers)
   └── resolver_website.cpp    (bounded website crawl + resolve_website_appimage_download)
```

## File boundaries

Line ranges refer to current `src/resolver.cpp` before the split. Re-locate by markers if the file drifts.

### `src/resolver_github.cpp`

From `github_api_base` through `download_with_strategy` (current ~24–184).

Public (in `yai.hpp`), file scope:

- `github_api_base`
- `github_repo_matches_local_blocklist`
- `github_repo_matches_builtin_blocklist`
- `enforce_github_release_policy`
- `resolve_github_latest`
- `mirror_url_for`
- `download_with_strategy`

Any helpers in this slice that are **not** declared in `yai.hpp` go in an anonymous namespace.

### `src/resolver_url.cpp`

From `strip_url_fragment_query` through `appimage_url_from_download_landing_page` (current ~186–544).

Public file-scope APIs (declared in `yai.hpp`): URL/host/href/catalog helpers, candidate allow-lists, `best_appimage_url_from_candidates`, `file_looks_like_html`, `appimage_url_from_download_landing_page`, etc.

Internal (anonymous namespace) examples currently undeclared in the header:

- `html_appimage_urls`
- `text_looks_like_html`
- `appimage_url_from_download_landing_html`

### `src/resolver_website.cpp`

From `truncate_status_text` through `resolve_website_appimage_download` (current ~557–891).

Public file-scope:

- `truncate_status_text`
- `resolve_website_appimage_download`

Internal (anonymous namespace):

- `WebsiteSearchProgress` (and its methods)
- `WebsiteQueueItem`, `WebsiteSearchState`
- Queue/fetch/collect helpers (`queue_*`, `fetch_website_links`, `collect_appimage_candidates`, `selected_website_candidate`, …)

Do **not** move `install_arch_for_options` / `with_install_arch` here (they sit just above `truncate_status_text` and belong with install-source resolution in thin `resolver.cpp`).

### `src/resolver.cpp` (remainder)

- File banner updated for remaining responsibilities
- `contains_case_insensitive`, `package_matches_keyword`
- `install_arch_for_options`, `with_install_arch` (anonymous namespace if undeclared in `yai.hpp`)
- `stage_appimage_source`
- Internal resolve helpers (`resolve_local_install_source`, `source_from_github_release`, `resolve_github_install_source`, `repo_*_source`, overloads of `resolve_repo_package_install_source`, `resolve_url_install_source`) → anonymous namespace if undeclared
- Public: `resolve_install_source`, `source_uses_github_release_download`, `prompt_china_network_config`, `prompt_github_release_proxy_for_this_download`, `apply_network_config_to_options`

## Makefile

```make
SRC := \
	src/arch.cpp \
	src/appimage.cpp \
	src/cli_download.cpp \
	src/commands.cpp \
	src/commands_doctor.cpp \
	src/commands_lifecycle.cpp \
	src/commands_query.cpp \
	src/commands_repo.cpp \
	src/commands_upgrade.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/resolver.cpp \
	src/resolver_github.cpp \
	src/resolver_url.cpp \
	src/resolver_website.cpp
```

## Error handling / data flow

Unchanged. Cross-TU calls use existing `yai.hpp` declarations only (e.g. `stage_appimage_source` → `download_with_strategy` / landing-page helpers; website crawl → URL helpers).

## Testing / verification

1. `make clean && make` succeeds
2. Smokes exercising resolution paths:
   - `tests/download_smoke.sh`
   - `tests/stage1_smoke.sh`
   - `tests/repo_smoke.sh`
   - `tests/mirror_policy_smoke.sh`
   - `tests/stage5_smoke.sh` (update/resolve paths)
   - `tests/appimage_feed_smoke.sh` if present and stable
3. No unit-style smoke currently compiles `resolver.cpp` alone; if link failures appear, update only g++ source lists

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Website internals left with external linkage | Put undeclared types/helpers in anonymous namespace in `resolver_website.cpp` |
| Accidental move of `install_arch_*` into website | Cut starts at `truncate_status_text`, not at `install_arch_for_options` |
| Public URL helpers wrongly wrapped in anon | Keep every `yai.hpp`-declared symbol at file scope |
| Behavior change while moving | Contiguous block moves; no algorithm edits |
