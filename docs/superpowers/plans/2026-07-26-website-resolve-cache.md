# Website resolve cache (install + update) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Skip full `website_page` crawls when a prior AppImage URL is still valid—on update/upgrade via metadata + `probe_url_freshness`, and on install via a TTL disk resolve cache.

**Architecture:** Add a small `website_resolve_cache` module (JSON under `~/.local/share/yai/website-resolve-cache.json`). Wrap install resolve to lookup → validate → crawl → upsert. Change `repo_website_page` update/upgrade/preview to short-circuit on freshness Unchanged before calling `resolve_repo_update_source`.

**Tech Stack:** C++17, existing `json_*` / `probe_url_freshness` / metadata helpers, `file://` fixtures, shell smokes, Makefile.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-26-website-resolve-cache-design.md`
- Update path: metadata + freshness; install path: disk cache; installed-app source of truth remains metadata
- Cache key = `package_id` + `normalize_arch(arch)` + repo `source.url` (normalized); TTL = **7 days**; path = `~/.local/share/yai/website-resolve-cache.json`
- Miss/invalid/expired → full crawl; success → upsert; `yai remove` does not clear cache
- GitHub / `direct_url` / crawl trust/top-N/mtime behavior unchanged
- msgid = English; sync `po/en.po` / `po/zh.po` only if new user-facing strings appear
- Do not commit unless the user explicitly asks; skip commit steps when commits are not requested

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | Declare cache entry/API + install/update helpers as needed |
| `src/website_resolve_cache.cpp` | Path, key, load/save JSON, TTL, validate cached URL |
| `src/resolver.cpp` | Install: use cache around `resolve_website_appimage_download` |
| `src/commands_update.cpp` | Preview: website freshness short-circuit |
| `src/commands_upgrade.cpp` | Upgrade: website freshness short-circuit + cache upsert after re-resolve |
| `Makefile` | Add `src/website_resolve_cache.cpp` to `SRC` |
| `tests/website_resolve_cache_smoke.sh` | Unit + install/update integration fixtures |

---

### Task 1: Disk cache helpers (unit smoke)

**Files:**
- Create: `src/website_resolve_cache.cpp`
- Modify: `src/yai.hpp`, `Makefile`
- Create: `tests/website_resolve_cache_smoke.sh` (unit section first)

**Interfaces:**
- Produces:
  - `constexpr std::int64_t kWebsiteResolveCacheTtlSeconds = 7 * 24 * 60 * 60;`
  - `struct WebsiteResolveCacheEntry { std::string package_id; std::string arch; std::string source_url; std::string download_url; std::int64_t resolved_at = 0; HttpValidators validators; };`
  - `fs::path website_resolve_cache_path();` → `expand_home_path(".local/share/yai/website-resolve-cache.json")`
  - `std::string website_resolve_cache_key(const std::string& package_id, const std::string& arch, const std::string& source_url);` — use `sanitize`/`normalize_arch` + `strip_url_fragment_query(source_url)`; stable delimiter e.g. `\n` join or `"id|arch|url"`
  - `std::vector<WebsiteResolveCacheEntry> load_website_resolve_cache();` — missing/corrupt → `{}`
  - `void save_website_resolve_cache(const std::vector<WebsiteResolveCacheEntry>& entries);` — create parent dirs; write JSON array of objects
  - `std::optional<WebsiteResolveCacheEntry> find_website_resolve_cache_entry(const std::vector<WebsiteResolveCacheEntry>& entries, const std::string& package_id, const std::string& arch, const std::string& source_url);`
  - `bool website_resolve_cache_entry_expired(const WebsiteResolveCacheEntry& entry, std::int64_t now_epoch_seconds);` — expired if `now - resolved_at > kWebsiteResolveCacheTtlSeconds` (or `resolved_at <= 0`)
  - `void upsert_website_resolve_cache_entry(WebsiteResolveCacheEntry entry);` — load, replace same key, save; set `resolved_at` if 0 to “now”
  - `bool website_cached_download_url_usable(const std::string& download_url, const HttpValidators& stored);` — requires `is_appimage_download_url`; then `probe_url_freshness`; usable if status is `Unchanged`, or `Unknown` while URL is reachable (`file://` exists already implied by non-Error probe for missing file; for `Unknown` after successful HEAD/file size read accept); **not** usable on `Error` or `Changed`

JSON shape (array):

```json
[
  {
    "package_id": "pkg",
    "arch": "x86_64",
    "source_url": "file:///…/index.html",
    "download_url": "file:///…/app.AppImage",
    "resolved_at": 1720000000,
    "http_etag": "",
    "http_last_modified": "",
    "http_content_length": "123"
  }
]
```

Use existing `json_find_string` / `json_escape_string` / array scan patterns from `repo.cpp` / feed code. Prefer a simple array + linear find (YAGNI).

- [ ] **Step 1: Write failing unit smoke**

Create `tests/website_resolve_cache_smoke.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
export HOME="$TMP_DIR/home"
mkdir -p "$HOME"

cat > "$TMP_DIR/cache_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

int main() {
    const std::string id = "cache-pkg";
    const std::string arch = "x86_64";
    const std::string src = "file:///tmp/site/index.html#frag";
    const std::string key = website_resolve_cache_key(id, arch, src);
    require(key.find("cache-pkg") != std::string::npos, "key has id");
    require(key.find("#frag") == std::string::npos, "key strips fragment");

    require(load_website_resolve_cache().empty(), "empty when missing");

    WebsiteResolveCacheEntry e;
    e.package_id = id;
    e.arch = arch;
    e.source_url = strip_url_fragment_query(src);
    e.download_url = "file:///tmp/missing.AppImage";
    e.resolved_at = 1;
    upsert_website_resolve_cache_entry(e);

    const auto loaded = load_website_resolve_cache();
    require(loaded.size() == 1, "one entry");
    const auto found = find_website_resolve_cache_entry(loaded, id, arch, src);
    require(found.has_value(), "find by key");
    require(found->download_url.find("missing.AppImage") != std::string::npos, "url stored");

    require(website_resolve_cache_entry_expired(*found, found->resolved_at + kWebsiteResolveCacheTtlSeconds + 1),
            "expired after ttl");
    require(!website_resolve_cache_entry_expired(*found, found->resolved_at + 10), "fresh inside ttl");

    require(!website_cached_download_url_usable(found->download_url, {}), "missing file unusable");

    // Create a tiny AppImage path and accept it
    // (write file in /tmp via test harness before this binary — see shell below)
    require(website_cached_download_url_usable(
                "file://" + std::string(std::getenv("YAI_CACHE_TEST_APPIMAGE")), {}),
            "existing file usable");

    std::cout << "website resolve cache unit smoke passed\n";
    return 0;
}
CPP

APP="$TMP_DIR/ok.AppImage"
printf '#!/bin/sh\necho ok\n' > "$APP"
chmod +x "$APP"
export YAI_CACHE_TEST_APPIMAGE="$APP"

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/cache_unit" \
  "$TMP_DIR/cache_unit.cpp" \
  "$ROOT/src/website_resolve_cache.cpp" \
  "$ROOT/src/url_freshness.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/json.cpp" \
  "$ROOT/src/cli_download.cpp" \
  "$ROOT/src/appimage.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/repo.cpp" \
  "$ROOT/src/repo_feed.cpp" \
  -lpthread

"$TMP_DIR/cache_unit"
```

Adjust link list until it compiles (same pattern as `website_mtime_smoke.sh`). `chmod +x` the smoke.

- [ ] **Step 2: Run to verify fail**

Run: `bash /home/fsx/yai/tests/website_resolve_cache_smoke.sh`

Expected: FAIL — missing `website_resolve_cache.cpp` / undeclared symbols.

- [ ] **Step 3: Implement helpers + Makefile**

Declare APIs in `src/yai.hpp`. Implement in `src/website_resolve_cache.cpp`. Add to `Makefile` `SRC`.

For `resolved_at` “now”, use UTC unix seconds (`std::chrono::system_clock`).

- [ ] **Step 4: Run to verify pass**

Run: `bash /home/fsx/yai/tests/website_resolve_cache_smoke.sh`

Expected: `website resolve cache unit smoke passed`

- [ ] **Step 5: Commit** (skip unless user asked)

---

### Task 2: Install resolve uses disk cache

**Files:**
- Modify: `src/resolver.cpp` (`repo_website_page_source`)
- Modify: `tests/website_resolve_cache_smoke.sh` (install integration)

**Interfaces:**
- Consumes: cache find/upsert/expired/usable; `resolve_website_appimage_download`
- Produces: install skips crawl on valid cache hit

```cpp
ResolvedSource repo_website_page_source(const InstallOptions& options, const RepoPackage& package) {
    const std::string arch = /* same effective arch as today via with_install_arch / options.target_arch */;
    const std::string source_page = package.source_url; // already what crawl uses
    const auto entries = load_website_resolve_cache();
    const auto hit = find_website_resolve_cache_entry(entries, package.id, /*normalized arch*/, source_page);
    const std::int64_t now = /* unix now */;
    if (hit.has_value() &&
        !website_resolve_cache_entry_expired(*hit, now) &&
        website_cached_download_url_usable(hit->download_url, hit->validators)) {
        // Build ResolvedSource from hit->download_url (same fields as crawl success path)
        ...
        return with_install_arch(source, options);
    }

    const std::string download_url = resolve_website_appimage_download(package, options.target_arch);
    WebsiteResolveCacheEntry fresh;
    fresh.package_id = package.id;
    fresh.arch = normalize_arch(/*effective*/);
    fresh.source_url = strip_url_fragment_query(package.source_url);
    fresh.download_url = download_url;
    fresh.resolved_at = now;
    // validators optional empty until download captures headers
    upsert_website_resolve_cache_entry(fresh);

    // existing ResolvedSource fill...
}
```

Match existing `repo_website_page_source` field assignment exactly when using either path.

- [ ] **Step 1: Add install integration to smoke**

After unit section, build a tiny `website_page` fixture (reuse mtime smoke style under `$TMP/download/cache-site/`), install once with `HOME=$TMP_HOME`, note curl log or progress shows crawl; remove app; install again with fake-curl log asserting the listing `index.html` is **not** fetched on second install (AppImage URL may still be probed/stat’d). Assert second install succeeds and metadata `download_url` matches.

Also: poison cache `download_url` to missing file → install still succeeds via crawl recovery.

- [ ] **Step 2: Run smoke expecting fail** (before wiring) or implement then green if doing wiring in same session after red unit already green.

- [ ] **Step 3: Wire `repo_website_page_source`**

- [ ] **Step 4: `make && bash tests/website_resolve_cache_smoke.sh` + `YAI_LANG=en bash tests/appimage_feed_smoke.sh`**

Expected: pass

- [ ] **Step 5: Commit** (skip unless user asked)

---

### Task 3: Update/upgrade (+ preview) freshness short-circuit

**Files:**
- Modify: `src/commands_upgrade.cpp` (`repo_website_page` branch)
- Modify: `src/commands_update.cpp` (`build_update_preview` for `repo_website_page`)
- Modify: `tests/website_resolve_cache_smoke.sh`

**Interfaces:**
- Consumes: `probe_url_freshness`, `validators_from_metadata`, `resolve_repo_update_source`, `upsert_website_resolve_cache_entry`
- Produces: Unchanged skips crawl; re-resolve upserts cache

**Upgrade** (`commands_upgrade.cpp`), replace the current blind resolve:

```cpp
if (context.source_kind == "repo_website_page") {
    const fs::path metadata = readable_metadata_path(context.paths);
    const std::string download_url =
        metadata_value(metadata, "download_url").value_or(context.source_url);
    if (!download_url.empty()) {
        const UrlFreshnessResult probe =
            probe_url_freshness(download_url, validators_from_metadata(metadata));
        if (probe.status == UrlFreshness::Unchanged) {
            print_already_up_to_date(context);
            return;
        }
    }
    ResolvedSource source = resolve_repo_update_source(context);
    // upsert disk cache from package id + repo source url + resolved download
    // (need RepoPackage or store package source_url in metadata — metadata has source_url
    //  which for website installs is currently set to the AppImage URL in repo_website_page_source.
    //  IMPORTANT: for cache key, use the *repo listing* URL, not the AppImage URL.)
    ...
}
```

**Critical design fix to implement in Tasks 2–3:** Today `repo_website_page_source` sets `source.source_url = download_url` (AppImage). Cache key needs the **catalog/listing** URL from `RepoPackage.source_url`. Options (pick one and stick to it):

1. Add metadata field `website_source_url` (or `resolve_source_url`) written at install from `package.source_url`, use that for cache key + update upsert; **or**
2. Keep disk cache key using `package.id` + arch + look up current `RepoPackage` from index by id when upserting on update.

**Prefer (2) for less metadata churn:** on update upsert, `find_repo_package(context.id)` (or by metadata id) and use `package->source_url` for the cache key. On install, use `package.source_url` directly. Document this in code comments.

If package was removed from index but still installed, skip disk upsert on update (freshness short-circuit still works from metadata).

**Preview** (`build_update_preview`): for `repo_website_page`, before `resolve_repo_update_source`, if metadata `download_url` probes Unchanged → return `current` / already up to date without crawl.

- [ ] **Step 1: Extend smoke — update Unchanged skips crawl**

Install fixture package; ensure metadata has `http_content_length` matching file size (install/download path should already write validators when possible; for `file://` copy size into metadata if missing so probe returns Unchanged). Run `yai update` / `yai upgrade` with fake-curl; assert listing pages not fetched; status already up to date.

- [ ] **Step 2: Extend smoke — dead URL forces re-resolve**

After install, delete/replace AppImage file that metadata points at (or change size so probe Changed); put a newer AppImage only discoverable via listing crawl; upgrade should crawl and pick it up (or install-equivalent resolve).

- [ ] **Step 3: Implement upgrade + preview short-circuit + upsert on re-resolve**

- [ ] **Step 4: Run**

```bash
make -C /home/fsx/yai -j"$(nproc)"
bash /home/fsx/yai/tests/website_resolve_cache_smoke.sh
YAI_LANG=en bash /home/fsx/yai/tests/appimage_feed_smoke.sh
bash /home/fsx/yai/tests/url_freshness_smoke.sh
```

Expected: all pass

- [ ] **Step 5: Commit** (skip unless user asked)

---

### Task 4: TTL miss integration + regression polish

**Files:**
- Modify: `tests/website_resolve_cache_smoke.sh`
- Touch implementation only if gaps appear

**Interfaces:** none new

- [ ] **Step 1: TTL miss case**

Write cache entry with `resolved_at = now - kWebsiteResolveCacheTtlSeconds - 10` pointing at a valid AppImage but ensure crawl would be visible (e.g. only listing contains the AppImage and cache URL is valid—expired must still crawl). Assert fake-curl fetches listing again and install/resolve succeeds; entry `resolved_at` refreshed.

Simpler approach: call unit-level `website_resolve_cache_entry_expired` already in Task 1; integration: manually edit JSON `resolved_at` to `1`, run install, assert crawl happens (listing fetched) even though AppImage file still exists.

- [ ] **Step 2: Run full related smokes**

```bash
make -C /home/fsx/yai -j"$(nproc)"
bash /home/fsx/yai/tests/website_resolve_cache_smoke.sh
bash /home/fsx/yai/tests/website_mtime_smoke.sh
YAI_LANG=en bash /home/fsx/yai/tests/appimage_feed_smoke.sh
```

Expected: all pass; echo a final `website resolve cache smoke passed` covering unit+integration.

- [ ] **Step 3: Commit** (skip unless user asked)

---

## Placeholder / consistency self-review

1. **Spec coverage:** update short-circuit, install cache, key/TTL/path, validation, upsert on crawl/re-resolve, remove-does-not-clear, tests 1–6 → Tasks 1–4. Metadata vs listing URL for cache key resolved via index lookup (prefer option 2).
2. **Placeholders:** none; concrete APIs and JSON shape included.
3. **Type consistency:** `WebsiteResolveCacheEntry`, `kWebsiteResolveCacheTtlSeconds`, `website_cached_download_url_usable`, upsert/load/find names shared.
4. **Commits:** skipped by default per Global Constraints.

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-07-26-website-resolve-cache.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — execute in this session with executing-plans checkpoints  

Which approach?
