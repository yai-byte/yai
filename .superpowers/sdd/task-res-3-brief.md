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

