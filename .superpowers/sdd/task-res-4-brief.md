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

