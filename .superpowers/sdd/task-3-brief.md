### Task 3: install/download prefer index URL, `--recrawl`, write-back, fallback

**Files:**
- Modify: `src/yai.hpp` (`InstallOptions::recrawl = false`)
- Modify: `src/cli_download.cpp` (parse `--recrawl`; update usage strings)
- Modify: `src/resolver.cpp` (`resolve_repo_package_install_source`)
- Modify: `src/commands_lifecycle.cpp` (fallback + write-back)
- Extend: `tests/repo_resolve_index_smoke.sh`

**Interfaces:**
- Consumes: Task 1–2 helpers
- Produces:
  - `InstallOptions.recrawl`
  - Prefer path inside `resolve_repo_package_install_source(options, package)`:

```cpp
ResolvedSource resolve_repo_package_install_source(const InstallOptions& options, const RepoPackage& package) {
    const std::string arch = install_arch_for_options(options); // existing helper in resolver.cpp
    if (!options.recrawl) {
        if (const auto url = repo_package_download_url_for_arch(package, arch)) {
            ResolvedSource source;
            source.source_kind = /* map source_type → repo_* kind same as today */;
            source.id = repo_source_id(options, package);
            source.name = repo_source_name(options, package);
            source.version = basename_from_url(*url);
            source.source_url = *url;
            source.download_url = *url;
            // For github_release packages preferred via index URL, leave github_* empty
            // so download uses direct URL path (no mirror rewrite required). Fallback
            // re-resolve restores full github metadata.
            return with_install_arch(source, options);
        }
    }
    // existing direct_url / website_page / github_release / unavailable branches
}
```

  - After successful `stage_appimage_source` in install/download for **repo** packages: if `repo_index_is_locally_writable()` and package lacked URL for arch before resolve → `repo_package_set_download_url(..., overwrite=false)` + `upsert_repo_package_download_urls`.
  - Fallback: if staging throws and the resolve used an index URL (detect via `!options.recrawl && repo_package_has_download_url_for_arch` at start), retry once with a copy of options where `recrawl = true` (or an internal `force_source_resolve` flag). On retry success, **still do not overwrite** existing index URL.

Helper worth adding to avoid duplicating lifecycle logic:

```cpp
ResolvedSource resolve_install_source_with_index_fallback(InstallOptions options);
// tries resolve_install_source; on staging failure the lifecycle owns retry —
```

Prefer keeping retry in `commands_lifecycle.cpp`:

```cpp
ResolvedSource source = resolve_install_source(options);
try {
  source.download_url = stage_appimage_source(...);
} catch (const std::exception& first) {
  if (!options.recrawl && looks_like_repo_package_target(options.target) &&
      /* package had index url */) {
    InstallOptions retry = options;
    retry.recrawl = true;
    source = resolve_install_source(retry);
    source.download_url = stage_appimage_source(source, effective_options, target);
  } else {
    throw;
  }
}
maybe_write_back_index_download_url(options, source);
```

- [ ] **Step 1: Write integration smoke**

Using `file://` fixtures (pattern from `website_resolve_cache_smoke.sh`):

1. Build a local repo index with one `website_page` package pointing at a tiny HTML page that links to a local `.AppImage`, **no** `download_url`.
2. `yai download <id>` → succeeds; index gains `download_url`.
3. Change the HTML so crawl would find a **different** AppImage; run `yai download` again without `--recrawl` → still uses old index URL (or skips crawl); index URL unchanged.
4. `yai download <id> --recrawl` → uses crawl; index URL **unchanged** when already present.
5. Seed index with a broken `download_url` (`file:///missing.AppImage`) and a working website_page → download falls back and succeeds; index URL **unchanged**.

Also cover `--recrawl` parse rejection of unknown options (existing behavior).

- [ ] **Step 2: Run — expect FAIL**

- [ ] **Step 3: Implement resolver + CLI + lifecycle**

Parse in both `parse_install_options` and `parse_download_options`:

```cpp
if (arg == "--recrawl") {
    options.recrawl = true;
    return true; // via parse_common_download_option or dedicated
}
```

Update usage banners in `cli_download.cpp` to mention `--recrawl`.

- [ ] **Step 4: Run smoke — expect PASS**

- [ ] **Step 5: Commit** (only if user asked)

---

