# Repo resolve → enrich index.json Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `yai repo resolve` to bulk-fill AppImage direct URLs into `index.json`, and make install/download prefer those URLs (with `--recrawl` and fill-if-absent write-back) so an enriched index can be shared and reused.

**Architecture:** Extend `RepoPackage` with optional `download_url` / `download_urls` / `resolved_at`. Add small index URL helpers (read priority, set-if-absent, serialize, persist, merge-across-`repo update`). Prefer index URLs inside repo package resolve; add `yai repo resolve` under `commands_repo.cpp` (or a focused new cpp if file grows too large).

**Tech Stack:** C++17, existing `json_*` / resolvers / `file://` fixtures, shell smokes, Makefile, gettext `tr()`.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-27-repo-resolve-index-design.md` exactly for CLI names and semantics
- Prefer index URL → fall back to original `source` on failure; default paths never overwrite existing index URLs; only `repo resolve --overwrite` overwrites; `--recrawl` never overwrites
- `--show XYZ` default `001`; `--summary` on by default; `--no-summary` suppresses totals
- Persist enriched fields in **named repo caches** as well as combined `index.json`, and **merge them back after `repo update`/`rebuild`** so upstream refresh does not wipe URLs
- Write-back only when the active index is a **local writable path** (default or `YAI_REPO_INDEX` file path); skip write-back silently if index is a remote URL
- msgid = English; sync `po/en.po` / `po/zh.po` when adding user-facing strings
- Do not commit unless the user explicitly asks; skip commit steps when commits are not requested
- Do not change website crawl / GitHub scoring algorithms; keep `website-resolve-cache.json` as local acceleration only

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | `RepoPackage` URL fields; `InstallOptions::recrawl`; index-URL helper declarations; `RepoResolveOptions` |
| `src/repo.cpp` | Parse/serialize URL fields; persist index; merge URL fields on rebuild/update |
| `src/repo_index_urls.cpp` (new) | Read priority, has-url-for-arch, apply URL write (absent vs overwrite), UTC `resolved_at` helper wrappers if not already covered |
| `src/resolver.cpp` | Prefer index URL in `resolve_repo_package_install_source`; honor `recrawl` |
| `src/cli_download.cpp` | Parse `--recrawl` for install/download; help text |
| `src/commands_lifecycle.cpp` | Fallback re-resolve if preferred index URL staging fails; write-back-if-absent after success |
| `src/commands_repo.cpp` (+ optional `src/commands_repo_resolve.cpp`) | `yai repo resolve` |
| `Makefile` | Add new `.cpp` if created |
| `po/en.po`, `po/zh.po` | New strings |
| `tests/repo_resolve_index_smoke.sh` | Unit + CLI smokes |

---

### Task 1: RepoPackage URL fields + parse/serialize + read priority

**Files:**
- Modify: `src/yai.hpp` (`RepoPackage`)
- Modify: `src/repo.cpp` (`parse_repo_package`, add serialize + map parse)
- Create: `src/repo_index_urls.cpp`
- Modify: `Makefile`
- Create: `tests/repo_resolve_index_smoke.sh` (unit section)

**Interfaces:**
- Produces:
  - Extend `RepoPackage`:
    ```cpp
    std::string download_url;                 // optional single-arch convenience
    std::map<std::string, std::string> download_urls; // arch -> url
    std::string resolved_at;                  // ISO-8601 UTC, may be empty
    ```
  - `std::optional<std::string> repo_package_download_url_for_arch(const RepoPackage& package, const std::string& arch);`
    — implements spec read priority (normalize arch first)
  - `bool repo_package_has_download_url_for_arch(const RepoPackage& package, const std::string& arch);`
  - `void repo_package_set_download_url(RepoPackage& package, const std::string& arch, const std::string& url, bool overwrite);`
    — if `!overwrite` and already has URL for arch → no-op; else set `download_urls[arch]`, set `download_url` when arch is the only/single write or equals `current_arch()` / caller’s “mirror default arch” (pass `bool mirror_to_download_url`); set `resolved_at = current_utc_timestamp()` on successful write
  - `std::string serialize_repo_package(const RepoPackage& package);` — schema-v1 package object including optional URL fields only when non-empty
  - `std::map<std::string, std::string> json_find_string_map(const std::string& object_text, const std::string& key);` — parse `"download_urls": { "x86_64": "..." }` via `json_find_object` + scanning top-level string values (add to `json.cpp` / declare in `yai.hpp` if no existing helper)

Read priority (must match spec):

1. `download_urls[normalize_arch(arch)]` if present and non-empty
2. else if `download_url` non-empty and `download_urls` empty → `download_url`
3. else → nullopt

- [ ] **Step 1: Write failing unit smoke**

Create `tests/repo_resolve_index_smoke.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
export HOME="$TMP_DIR/home"
mkdir -p "$HOME"

cat > "$TMP_DIR/url_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

int main() {
    RepoPackage p;
    p.id = "pkg";
    p.name = "Pkg";
    p.source_type = "direct_url";
    p.source_url = "https://example/a.AppImage";

    require(!repo_package_has_download_url_for_arch(p, "x86_64"), "empty");
    repo_package_set_download_url(p, "x86_64", "https://example/x.AppImage", false, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/x.AppImage", "single");
    require(p.download_url == "https://example/x.AppImage", "mirrored");
    require(!p.resolved_at.empty(), "timestamp");

    // no overwrite
    repo_package_set_download_url(p, "x86_64", "https://example/other.AppImage", false, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/x.AppImage", "kept");

    // overwrite
    repo_package_set_download_url(p, "x86_64", "https://example/other.AppImage", true, true);
    require(repo_package_download_url_for_arch(p, "x86_64").value_or("") == "https://example/other.AppImage", "overwritten");

    // multi-arch: missing arch must not fall back to download_url when map exists
    repo_package_set_download_url(p, "aarch64", "https://example/a.AppImage", true, false);
    require(!repo_package_download_url_for_arch(p, "armv7").has_value(), "no wrong-arch fallback");

    // serialize round-trip via parse_repo_package
    const std::string obj = serialize_repo_package(p);
    require(obj.find("download_urls") != std::string::npos, "has map");
    const RepoPackage again = parse_repo_package(obj);
    require(repo_package_download_url_for_arch(again, "aarch64").value_or("").find("a.AppImage") != std::string::npos, "roundtrip");

    // single-field compatibility: only download_url
    RepoPackage single;
    single.id = "s";
    single.name = "s";
    single.source_type = "direct_url";
    single.source_url = "https://example/s.AppImage";
    single.download_url = "https://example/only.AppImage";
    require(repo_package_download_url_for_arch(single, "x86_64").value_or("") == "https://example/only.AppImage", "compat");

    std::cout << "repo index url unit smoke passed\n";
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/url_unit" \
  "$TMP_DIR/url_unit.cpp" \
  "$ROOT/src/repo_index_urls.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/terminal_color.cpp" \
  "$ROOT/src/website_resolve_cache.cpp" \
  -pthread

"$TMP_DIR/url_unit"
```

(Adjust linked objs to whatever the unit binary actually needs after compile errors; prefer linking the same set as `tests/website_resolve_cache_smoke.sh` if unresolved symbols appear.)

Declare `parse_repo_package` in `yai.hpp` if it is not already public (today it is file-local in `repo.cpp` — **export it** for tests and serialize round-trip, or keep parse private and expose `RepoPackage parse_repo_package_object(const std::string&)`).

- [ ] **Step 2: Run smoke — expect FAIL**

Run: `bash /home/fsx/yai/tests/repo_resolve_index_smoke.sh`  
Expected: compile/link failure (missing symbols / files).

- [ ] **Step 3: Implement minimal helpers**

1. Add fields to `RepoPackage` in `yai.hpp`.
2. In `parse_repo_package`, read `download_url`, `resolved_at`, and `download_urls` object (string values only).
3. Implement `serialize_repo_package` matching existing feed object style (id/name/summary/homepage/license/source + optional URL fields). Reuse `current_utc_timestamp()` already in `repo.cpp` (export via `yai.hpp` if needed — it may already be declared).
4. Implement `repo_index_urls.cpp` APIs as specified. Signature for set:

```cpp
void repo_package_set_download_url(
    RepoPackage& package,
    const std::string& arch,
    const std::string& url,
    bool overwrite,
    bool mirror_to_download_url);
```

5. Add `src/repo_index_urls.cpp` to `Makefile` `SRC`.
6. Export `parse_repo_package` in `yai.hpp`.

- [ ] **Step 4: Run smoke — expect PASS**

Run: `bash /home/fsx/yai/tests/repo_resolve_index_smoke.sh`  
Expected: `repo index url unit smoke passed`

- [ ] **Step 5: Commit** (only if user asked)

```bash
git add src/yai.hpp src/repo.cpp src/repo_index_urls.cpp src/json.cpp Makefile tests/repo_resolve_index_smoke.sh
git commit -m "Add repo index download_url fields and read/write helpers."
```

---

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

### Task 5: i18n + usage polish

**Files:**
- Modify: `po/en.po`, `po/zh.po`
- Modify: usage/help strings in `cli_download.cpp` / `commands_repo.cpp` / `core.cpp` if global help lists repo subcommands

**Interfaces:**
- Consumes: all new `tr("...")` msgids introduced in Tasks 3–4

- [ ] **Step 1: Collect new English msgids** from the diff (examples):

  - `repo requires a subcommand: list, add, update, remove, or resolve`
  - `unknown repo resolve option: `
  - `--show must be three digits of 0 or 1`
  - `repo resolve completed with failures`
  - `resolved: {resolved} skipped: {skipped} failed: {failed}`
  - `--recrawl` related usage lines

- [ ] **Step 2: Add msgstr to `po/en.po` (same as msgid) and Chinese `po/zh.po`**

- [ ] **Step 3: Run smoke under `YAI_LANG=zh` once for resolve help/error path sanity**

- [ ] **Step 4: Commit** (only if user asked)

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| Extend index with `download_url` / `download_urls` / `resolved_at` | 1 |
| Read priority rules | 1 |
| Persist local index + `--output` | 2, 4 |
| Survive `repo update` rebuild (merge) | 2 |
| Prefer index URL on install/download | 3 |
| Write-back only if absent | 3 |
| `--recrawl` ignore, never overwrite | 3 |
| Fallback when index URL fails | 3 |
| `yai repo resolve` filters/arch/overwrite/concurrency | 4 |
| `--show` / `--summary` / `--no-summary` | 4 |
| Non-zero exit on failures | 4 |
| i18n | 5 |
| Keep website-resolve-cache | unchanged (3 may still upsert on crawl) |

## Self-review notes

- No TBD placeholders left; merge-on-update is an explicit plan addition required to honor “local index stays useful”.
- `parse_repo_package` must become callable from helpers/tests.
- Concurrency>1 is optional performance; default `1` is correct and sufficient for first green smoke.
