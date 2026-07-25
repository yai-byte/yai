# Split resolver.cpp Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `src/resolver.cpp` (~1188 lines) into `resolver_github.cpp`, `resolver_url.cpp`, `resolver_website.cpp`, and a thinner `resolver.cpp`, with no public API or behavior change.

**Architecture:** Mechanical contiguous moves along seams in `docs/superpowers/specs/2026-07-25-split-resolver-cpp-design.md`. Keep declarations in `src/yai.hpp`. Put helpers/types not declared in `yai.hpp` into anonymous namespaces in their owning `.cpp`.

**Tech Stack:** C++17, Makefile, existing shell smoke tests.

## Global Constraints

- Do not change function signatures or runtime behavior
- Do not split or edit `src/yai.hpp` except if a new `.cpp` needs an include already required elsewhere (prefer fixing the new `.cpp` only)
- Do not commit unless the user explicitly asks
- Do not “clean up” or refactor website crawl logic while moving
- Line numbers below refer to `src/resolver.cpp` **before** any split; re-read markers if the file has drifted

## File map (after completion)

| File | Owns |
|------|------|
| `src/resolver_github.cpp` | GitHub API/blocklist, `resolve_github_latest`, mirror URL + `download_with_strategy` |
| `src/resolver_url.cpp` | URL/HTML helpers through landing-page AppImage extraction |
| `src/resolver_website.cpp` | `truncate_status_text`, website crawl state machine, `resolve_website_appimage_download` |
| `src/resolver.cpp` | package match helpers, `install_arch_*`, `stage_appimage_source`, resolve_* dispatch, network prompts |
| `Makefile` | Lists all four plus existing sources |

---

### Task 1: Extract `src/resolver_github.cpp`

**Files:**
- Create: `src/resolver_github.cpp`
- Modify: `src/resolver.cpp`, `Makefile`

**Interfaces:**
- Produces public: `github_api_base`, blocklist helpers, `enforce_github_release_policy`, `resolve_github_latest`, `mirror_url_for`, `download_with_strategy`
- Leaves in `resolver.cpp`: `contains_case_insensitive`, `package_matches_keyword` (before this slice)

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(bool contains_case_insensitive|std::string github_api_base|std::string download_with_strategy|std::string strip_url_fragment_query)' src/resolver.cpp
```

Record:
- `G0` = `github_api_base`
- `G1` = last line of `download_with_strategy`
- `U0` = `strip_url_fragment_query` (must stay until Task 2)

- [ ] **Step 2: Create `src/resolver_github.cpp`**

```cpp
#include "yai.hpp"

// GitHub release resolution, blocklists, and mirror-aware download strategy.

```

Move `[G0, G1]`. Keep `yai.hpp`-declared symbols at file scope. Wrap only undeclared helpers (if any) in `namespace { }`.

- [ ] **Step 3: Remove `[G0, G1]` from `src/resolver.cpp`**

Afterward, `package_matches_keyword` should be followed by `strip_url_fragment_query` (aside from blank lines).

- [ ] **Step 4: Makefile + build**

Add `src/resolver_github.cpp \` to `SRC` (alphabetically after `resolver.cpp` or with other `resolver_*` files).

```bash
make clean && make
```

Expected: exit 0.

---

### Task 2: Extract `src/resolver_url.cpp`

**Files:**
- Create: `src/resolver_url.cpp`
- Modify: `src/resolver.cpp`, `Makefile`

**Interfaces:**
- Produces all public URL/HTML helpers declared in `yai.hpp` from `strip_url_fragment_query` through `appimage_url_from_download_landing_page`
- Internal anon: `html_appimage_urls`, `text_looks_like_html`, `appimage_url_from_download_landing_html` (and any other undeclared helpers in the slice)

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(std::string strip_url_fragment_query|std::string appimage_url_from_download_landing_page|std::string install_arch_for_options|std::string truncate_status_text)' src/resolver.cpp
```

- `U0` = `strip_url_fragment_query`
- `U1` = last line of `appimage_url_from_download_landing_page`
- `A0` = `install_arch_for_options` (must remain in resolver.cpp)
- `W0` = `truncate_status_text` (Task 3)

- [ ] **Step 2: Create `src/resolver_url.cpp`**

```cpp
#include "yai.hpp"

// URL/HTML parsing helpers for AppImage candidate discovery.

```

Move `[U0, U1]`. Put INTERNAL helpers in `namespace { }`; keep PUBLIC APIs file-scope.

- [ ] **Step 3: Remove `[U0, U1]` from `resolver.cpp`**

Seam check: `package_matches_keyword` (or match helpers) then `install_arch_for_options` then later website/stage code.

- [ ] **Step 4: Makefile + build**

```bash
make clean && make
```

Expected: exit 0.

---

### Task 3: Extract `src/resolver_website.cpp`

**Files:**
- Create: `src/resolver_website.cpp`
- Modify: `src/resolver.cpp`, `Makefile`

**Interfaces:**
- Public: `truncate_status_text`, `resolve_website_appimage_download`
- Internal anon: `WebsiteSearchProgress`, `WebsiteQueueItem`, `WebsiteSearchState`, and all crawl helpers through `selected_website_candidate` / internals of `resolve_website_appimage_download`
- Must NOT move: `install_arch_for_options`, `with_install_arch`, `stage_appimage_source`

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(std::string install_arch_for_options|std::string truncate_status_text|std::string resolve_website_appimage_download|std::string stage_appimage_source)' src/resolver.cpp
```

- `W0` = `truncate_status_text`
- `W1` = last line of `resolve_website_appimage_download`
- Confirm `install_arch_for_options` is **before** `W0` and `stage_appimage_source` is **after** `W1`

- [ ] **Step 2: Create `src/resolver_website.cpp`**

```cpp
#include "yai.hpp"

// Host-bounded website crawling to discover AppImage download URLs.

```

Move `[W0, W1]`. Wrap all undeclared types/helpers/classes in `namespace { }`. Keep `truncate_status_text` and `resolve_website_appimage_download` at file scope.

Note: `WebsiteSearchProgress` is a `class` with methods — the entire class definition must live inside the anonymous namespace (or be unnamed-namespace-local). Do not leave it with external linkage.

- [ ] **Step 3: Remove `[W0, W1]` from `resolver.cpp`**

Afterward: `with_install_arch` (or `install_arch_*`) should be followed by `stage_appimage_source`.

- [ ] **Step 4: Makefile + build**

```bash
make clean && make
```

Expected: exit 0. Link errors about website helpers usually mean a helper was left outside anon in the wrong TU or still referenced across TUs without a header declaration.

---

### Task 4: Thin `src/resolver.cpp` + finalize Makefile

**Files:**
- Modify: `src/resolver.cpp` (banner + anon wrap for internal resolve helpers)
- Modify: `Makefile` (exact `SRC` list)

**Interfaces:**
- Public file-scope: `contains_case_insensitive`, `package_matches_keyword`, `stage_appimage_source`, `resolve_install_source`, `source_uses_github_release_download`, `prompt_china_network_config`, `prompt_github_release_proxy_for_this_download`, `apply_network_config_to_options`
- Internal anon: `install_arch_for_options`, `with_install_arch`, and all `resolve_*` / `repo_*_source` helpers not in `yai.hpp`

- [ ] **Step 1: Update banner**

```cpp
// Install-source dispatch: package match helpers, staging, resolve_install_source
// routing, and interactive network/mirror prompts. GitHub, URL/HTML, and website
// crawl implementations live in resolver_github.cpp, resolver_url.cpp, and
// resolver_website.cpp.
```

- [ ] **Step 2: Wrap INTERNAL helpers in anonymous namespace**

Keep public APIs outside. Do not change function bodies.

- [ ] **Step 3: Finalize Makefile `SRC`**

Exact list from the design doc (includes `resolver.cpp`, `resolver_github.cpp`, `resolver_url.cpp`, `resolver_website.cpp` among existing sources).

- [ ] **Step 4: Build**

```bash
make clean && make
```

Expected: exit 0; `./yai` produced.

---

### Task 5: Regression verification

**Files:** none unless a smoke hardcodes a stale `g++` list (then update link lines only).

- [ ] **Step 1: Build**

```bash
make clean && make
```

- [ ] **Step 2: Resolution-heavy smokes**

```bash
YAI_LANG=en tests/download_smoke.sh
YAI_LANG=en tests/stage1_smoke.sh
YAI_LANG=en tests/repo_smoke.sh
YAI_LANG=en tests/mirror_policy_smoke.sh
YAI_LANG=en tests/stage5_smoke.sh
YAI_LANG=en tests/appimage_feed_smoke.sh
```

Expected: all pass. If `appimage_feed_smoke.sh` is flaky due to network, note it separately but do not weaken other failures.

- [ ] **Step 3: Line-count spot check**

```bash
wc -l src/resolver.cpp src/resolver_github.cpp src/resolver_url.cpp src/resolver_website.cpp
```

Expected (approximate):
- `resolver_github.cpp` ~160–200
- `resolver_url.cpp` ~350–400
- `resolver_website.cpp` ~330–380
- `resolver.cpp` ~280–350
- Sum ≈ previous `resolver.cpp` (± banners / namespace lines)

- [ ] **Step 4: Commit only if the user asks**

---

## Self-review checklist

1. **Spec coverage:** github / url / website / thin resolver / Makefile / verification — each has a task.
2. **Placeholders:** none intentionally left.
3. **Cut safety:** website cut starts at `truncate_status_text`, not `install_arch_for_options`.
4. **Visibility:** undeclared website/resolve helpers confined to anonymous namespaces; public symbols match `yai.hpp`.
