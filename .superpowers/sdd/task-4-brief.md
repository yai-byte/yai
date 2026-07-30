### Task 4: `yai repo resolve` command

**Files:**
- Modify: `src/yai.hpp` (`RepoResolveOptions`, declarations)
- Modify: `src/commands_repo.cpp` or Create: `src/commands_repo_resolve.cpp`
- Modify: `Makefile` if new file
- Modify: `src/commands_repo.cpp` `repo_app` dispatch
- Extend: `tests/repo_resolve_index_smoke.sh`

**Interfaces:**
- Produces:

```cpp
struct RepoResolveOptions {
    std::optional<fs::path> output;
    std::vector<std::string> arches; // empty → { current_arch() }; "all" → all canonical arches from arch_alias_rules
    std::vector<std::string> types;  // empty → github_release, website_page, direct_url
    std::vector<std::string> packages; // empty → all
    bool overwrite = false;
    int concurrency = 1;
    bool show_success = false;
    bool show_skip = false;
    bool show_fail = true;   // default --show 001
    bool summary = true;
};

RepoResolveOptions parse_repo_resolve_options(int argc, char** argv); // argv starts at subcommand args
int repo_resolve_app(int argc, char** argv); // returns process exit code semantics via throw or int — match existing commands (throw on hard errors; for partial failure set exit: prefer returning int from main path). Existing yai uses exceptions; for non-zero on partial failure, throw after printing OR set a flag. **Preferred:** print results, then `if (failed) throw std::runtime_error(tr("repo resolve completed with failures"));` or return non-zero through existing main catch. Match how other batch commands signal failure.
```

`--show` parser:

```cpp
void parse_show_mask(const std::string& value, RepoResolveOptions& o) {
  if (value.size() != 3 || !all_of digits 0/1) throw ...
  o.show_success = value[0] == '1';
  o.show_skip = value[1] == '1';
  o.show_fail = value[2] == '1';
}
```

Core loop (concurrency=1 first; if `concurrency > 1`, resolve packages with a simple worker pool / `std::async` capped at N — website crawler keeps its own internal parallelism):

```
packages = load_repo_packages()
filter by --package / --type
for each package:
  for each arch in arches:
    if !overwrite && has url → skip++
    else try:
      InstallOptions opt; opt.target = package.id; opt.target_arch = arch; opt.arch_explicit = true; opt.recrawl = true;
      // always resolve via original source for bulk fill; overwrite flag only controls write
      ResolvedSource src = resolve_repo_package_install_source(opt, package);
      repo_package_set_download_url(package, arch, src.download_url, overwrite, arch == arches.front() || arch == current_arch());
      success++
    catch → fail++; record id + reason
persist via upsert / save_repo_packages_index(repo_index_path())
if output → save_repo_packages_index(..., *output)
print per show bits; print summary unless --no-summary
```

Notes:

- For bulk resolve use `recrawl=true` so existing index URLs are ignored when `--overwrite`; when not overwriting, skip before resolve (never call network).
- `direct_url`: resolved URL is `package.source_url` (no network) — still fills index fields.
- Update `repo_app` help: `list, add, update, remove, or resolve`.
- `--arch all`: iterate canonical arches from `arch_alias_rules()` (export a `std::vector<std::string> canonical_arches()` from `arch.cpp` if needed).

- [ ] **Step 1: Write CLI smoke**

```bash
# index with two direct_url packages; one pre-filled download_url
yai repo resolve --package filled --package empty
# expect: filled skipped, empty resolved; summary resolved/skipped/failed
yai repo resolve --show 000 --no-summary   # quiet aside from nothing
yai repo resolve --overwrite --package filled
# expect filled URL replaced when source differs or at least rewrite allowed
yai repo resolve --output "$TMP/out.json"
# expect out.json exists and local index updated
# force one failure (website_page with dead file://) → exit non-zero and print id with default --show 001
```

- [ ] **Step 2: Run — expect FAIL**

- [ ] **Step 3: Implement command + dispatch**

- [ ] **Step 4: Run full `tests/repo_resolve_index_smoke.sh` — expect PASS**

- [ ] **Step 5: Commit** (only if user asked)

---

