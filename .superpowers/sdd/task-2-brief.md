### Task 2: Persist enriched index + merge across repo update

**Files:**
- Modify: `src/repo.cpp` (`rebuild_repo_index_from_cached_files`, add save/merge helpers)
- Modify: `src/commands_repo.cpp` (`store_repo_index_updates` / update path)
- Modify: `src/yai.hpp` (declare APIs)
- Extend: `tests/repo_resolve_index_smoke.sh`

**Interfaces:**
- Produces:
  - `bool repo_index_is_locally_writable();` — true when `YAI_REPO_INDEX` unset or is a non-URL filesystem path
  - `void save_repo_packages_index(const std::vector<RepoPackage>& packages, const fs::path& path);` — uses `repo_index_json_from_package_objects` over `serialize_repo_package` outputs
  - `void upsert_repo_package_download_urls(const RepoPackage& updated);`
    — load combined index packages; replace matching `id`; write combined index; also patch any `named_repo_index_path(entry)` whose packages contain that id (load objects → parse → update → rewrite named cache + rebuild combined **or** rewrite named + combined consistently)
  - `RepoPackage merge_repo_package_download_url_fields(const RepoPackage& incoming, const RepoPackage& previous);`
    — copy `download_url` / `download_urls` / `resolved_at` from `previous` onto `incoming` when incoming lacks them (per-arch: keep previous arch URL if incoming has none for that arch; never delete previous URLs unless incoming explicitly has newer — for `repo update`, incoming from upstream has none, so keep all previous)
  - Wire `rebuild_repo_index_from_cached_files` / `repo update` so after fetching normalized upstream text, merge URL fields from the **previous** named cache (if present) before writing the new named cache

Merge algorithm for one package id:

```
for each arch in previous.download_urls:
  if incoming lacks that arch URL → copy
if previous.download_url set and incoming.download_url empty and incoming.download_urls empty:
  copy download_url + resolved_at
else if any URLs merged:
  keep previous.resolved_at if incoming.resolved_at empty
```

- [ ] **Step 1: Extend smoke for persist + merge**

Append a second compiled unit (or shell section) that:

1. Writes a minimal named cache + combined index under `$HOME/.local/share/yai/repos/` with one `direct_url` package **without** download fields.
2. Calls `repo_package_set_download_url` + `upsert_repo_package_download_urls`.
3. Reloads via `load_repo_packages()` and asserts URL present.
4. Simulates update: build “incoming” package without URLs, `merge_repo_package_download_url_fields(incoming, previous)` keeps URLs.
5. Writes merged package into a fake post-update named cache and `rebuild_repo_index_from_cached_files` — combined index still has URLs.

- [ ] **Step 2: Run — expect FAIL** on missing APIs

- [ ] **Step 3: Implement persist + merge wiring**

In `store_repo_index_updates` (commands_repo.cpp), before `write_text_file(named_repo_index_path(...))`:

```cpp
if (fs::exists(named_repo_index_path(entry.name))) {
  // load previous packages from old named cache
  // parse new index_text packages
  // for matching ids, merge_repo_package_download_url_fields
  // re-serialize packages into index_text
}
```

Keep behavior unchanged when no previous cache exists.

- [ ] **Step 4: Run smoke — expect PASS**

- [ ] **Step 5: Commit** (only if user asked)

---

