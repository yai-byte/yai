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

