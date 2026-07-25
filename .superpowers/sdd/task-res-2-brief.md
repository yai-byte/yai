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

