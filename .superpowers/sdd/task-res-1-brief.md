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

