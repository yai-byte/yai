# Download Id Naming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Save downloads as `{source.id}.AppImage`, strip version/arch from URL and local default ids, and recover repo package id/name when installing a local AppImage without `--id`.

**Architecture:** Add `base_name_from_appimage_filename` (and arch-token helper) for shared stripping; switch `download_output_name` to `{id}.AppImage`; wire stripping into URL defaults and local resolution; add best-effort repo stem matching in `resolve_local_install_source` using `!id_explicit` (not `id.empty()`), because `fill_install_defaults_for_direct_target` pre-fills id for local paths.

**Tech Stack:** C++17, existing `tr()`, shell smoke tests under `tests/`, Makefile.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-25-download-id-naming-design.md` exactly
- Keep `source_kind=local_path` for local installs; do not re-download on repo match
- Repo recovery is best-effort: missing/unreadable index must not fail local install
- Explicit `--id` / `--name` always win (`id_explicit` / `name_explicit`)
- msgid = English source text; sync `po/en.po` / `po/zh.po` only if new user-facing strings appear (this plan expects none)
- Do not commit unless the user explicitly asks; skip commit steps when commits are not requested

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | Declare `token_looks_like_arch`, `base_name_from_appimage_filename`, optional `match_repo_package_for_local_stem` |
| `src/arch.cpp` | `token_looks_like_arch` using `arch_alias_rules()` |
| `src/core.cpp` | `base_name_from_appimage_filename` (+ version-token helper in anon namespace) |
| `src/cli_download.cpp` | `fill_install_defaults_for_direct_target` uses base-name helper; fix file banner comment |
| `src/resolver.cpp` | URL + local id/name via helper; local repo recovery |
| `src/commands_lifecycle.cpp` | `download_output_name` → `{id}.AppImage` |
| `src/repo.cpp` | Optional: `match_repo_package_for_local_stem` if not kept private in resolver |
| `tests/download_smoke.sh` | Id-named download paths; URL strip; download→local install id |
| `README.md`, `AppImage包管理器开发文档.md` | Document new download naming + id stripping |

---

### Task 1: Failing download smoke for `{id}.AppImage` naming

**Files:**
- Modify: `tests/download_smoke.sh`

**Interfaces:**
- Consumes: none yet
- Produces: failing assertions for on-disk names `download-demo.AppImage`, `parallel-one.AppImage`, `parallel-two.AppImage`

- [ ] **Step 1: Change expected download paths in `tests/download_smoke.sh`**

Replace upstream-asset expectations with id-based names. After `yai download acme/download-demo`:

```bash
DOWNLOADED="$DOWNLOAD_DIR/download-demo.AppImage"
test -f "$DOWNLOADED"
grep -q "download demo app" "$DOWNLOADED"
test ! -x "$DOWNLOADED"
test ! -e "$TMP_HOME/.local/share/yai/apps/download-demo"
test ! -e "$TMP_HOME/.local/bin/download-demo"
test ! -e "$TMP_HOME/.local/share/applications/yai-download-demo.desktop"
```

After parallel download:

```bash
test -f "$PARALLEL_DOWNLOAD_DIR/parallel-one.AppImage"
test -f "$PARALLEL_DOWNLOAD_DIR/parallel-two.AppImage"
grep -q "parallel one app" "$PARALLEL_DOWNLOAD_DIR/parallel-one.AppImage"
grep -q "parallel two app" "$PARALLEL_DOWNLOAD_DIR/parallel-two.AppImage"
test ! -x "$PARALLEL_DOWNLOAD_DIR/parallel-one.AppImage"
test ! -x "$PARALLEL_DOWNLOAD_DIR/parallel-two.AppImage"
```

After wildcard `download 'download-*'`:

```bash
test -f "$WILDCARD_DOWNLOAD_DIR/download-demo.AppImage"
test ! -x "$WILDCARD_DOWNLOAD_DIR/download-demo.AppImage"
```

Keep the “already exists” check against `$DOWNLOAD_DIR` after the first download (same directory still has `download-demo.AppImage`).

Leave the URL auto-download section on `$AUTO_DOWNLOAD_ASSET` for Task 3 (or temporarily keep old assertion and change it in Task 3). Prefer changing it in Task 3 only so Task 1 failure mode is clearly naming for github/repo ids.

- [ ] **Step 2: Rebuild and run the smoke; confirm failure**

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/download_smoke.sh
```

Expected: FAIL — `test -f` missing `download-demo.AppImage` (file still named `DownloadDemo-x86_64.AppImage`).

- [ ] **Step 3: Skip commit unless user requested commits**

---

### Task 2: Implement `{source.id}.AppImage` download naming

**Files:**
- Modify: `src/commands_lifecycle.cpp` (`download_output_name`)
- Modify: `src/cli_download.cpp` (banner comment only)

**Interfaces:**
- Consumes: `ResolvedSource.id` already set by `resolve_install_source`
- Produces: on-disk download basename `{id}.AppImage`

- [ ] **Step 1: Replace `download_output_name`**

In `src/commands_lifecycle.cpp`, replace the function body with:

```cpp
std::string download_output_name(const ResolvedSource& source) {
    if (source.id.empty()) {
        return "download.AppImage";
    }
    return source.id + ".AppImage";
}
```

Update the file comment in `src/cli_download.cpp` that still says download uses `current_path()/upstream_asset_name` so it says package id basename instead.

- [ ] **Step 2: Rebuild and re-run download smoke**

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/download_smoke.sh
```

Expected: PASS for github/repo/parallel/wildcard naming sections (URL section still using old asset name until Task 3–4). If you already changed the URL assertion in Task 1, expect URL section to fail until Task 4.

- [ ] **Step 3: Skip commit unless user requested commits**

---

### Task 3: Failing smoke for URL strip + download→local install id

**Files:**
- Modify: `tests/download_smoke.sh`

**Interfaces:**
- Consumes: Task 2 download naming
- Produces: failing checks for stripped URL id file `autodownload.AppImage` and local install id `download-demo`

- [ ] **Step 1: Update URL auto-download expectations**

Change the auto-download block to expect stripped id naming:

```bash
(
  cd "$AUTO_DOWNLOAD_DIR"
  HOME="$TMP_HOME" \
  PATH="$FAKE_BIN:$PATH" \
  FAKE_DOWNLOADER_LOG="$TMP_HOME/auto-downloader.log" \
  "$ROOT/yai" download "https://example.invalid/$AUTO_DOWNLOAD_ASSET"
)

test -f "$AUTO_DOWNLOAD_DIR/autodownload.AppImage"
grep -q "fake downloader app" "$AUTO_DOWNLOAD_DIR/autodownload.AppImage"
test ! -x "$AUTO_DOWNLOAD_DIR/autodownload.AppImage"
grep -Fq $'curl-head\thttps://example.invalid/AutoDownload-x86_64.AppImage' "$TMP_HOME/auto-downloader.log"
grep -Fq $'aria2c\thttps://example.invalid/AutoDownload-x86_64.AppImage\tfile-allocation=none' "$TMP_HOME/auto-downloader.log"
test ! -e "$TMP_HOME/.local/share/yai/apps/autodownload"
```

(Downloader still fetches the original URL; only the local save name / package id changes.)

- [ ] **Step 2: Append download→local install + versioned local recovery checks**

Before `echo "download smoke test passed"`, after the bad-downloader check, append:

```bash
# download (repo id name) then local install must use package id
VERSIONED_ASSET="download-demo-1.2.3-x86_64.AppImage"
cp "$ORIGINAL_ROOT/$ASSET" "$TMP_HOME/$VERSIONED_ASSET"
HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install "$TMP_HOME/$VERSIONED_ASSET"
grep -q '"id": "download-demo"' \
  "$TMP_HOME/.local/share/yai/apps/download-demo/metadata.json"
grep -q '"source_kind": "local_path"' \
  "$TMP_HOME/.local/share/yai/apps/download-demo/metadata.json"
HOME="$TMP_HOME" "$ROOT/yai" remove download-demo

# URL-style strip without repo: Foo-1.2-x86_64.AppImage -> id foo
cp "$ORIGINAL_ROOT/$ASSET" "$TMP_HOME/Foo-1.2-x86_64.AppImage"
HOME="$TMP_HOME" "$ROOT/yai" install "$TMP_HOME/Foo-1.2-x86_64.AppImage"
test -e "$TMP_HOME/.local/share/yai/apps/foo/metadata.json"
grep -q '"id": "foo"' "$TMP_HOME/.local/share/yai/apps/foo/metadata.json"
HOME="$TMP_HOME" "$ROOT/yai" remove foo

# id-named download then install
(
  cd "$TMP_HOME/id-named-download"
  mkdir -p "$TMP_HOME/id-named-download"
  HOME="$TMP_HOME" \
  YAI_REPO_INDEX="$INDEX" \
  YAI_GITHUB_API_BASE="file://$API_ROOT" \
  "$ROOT/yai" download download-demo
)
HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" install "$TMP_HOME/id-named-download/download-demo.AppImage"
grep -q '"id": "download-demo"' \
  "$TMP_HOME/.local/share/yai/apps/download-demo/metadata.json"
HOME="$TMP_HOME" "$ROOT/yai" remove download-demo
```

Fix the mkdir order: create the directory before `cd`. Use:

```bash
mkdir -p "$TMP_HOME/id-named-download"
(
  cd "$TMP_HOME/id-named-download"
  HOME="$TMP_HOME" \
  YAI_REPO_INDEX="$INDEX" \
  YAI_GITHUB_API_BASE="file://$API_ROOT" \
  "$ROOT/yai" download download-demo
)
```

- [ ] **Step 3: Run smoke; confirm new failures**

```bash
bash /home/fsx/yai/tests/download_smoke.sh
```

Expected: FAIL on `autodownload.AppImage` and/or local id `foo` / `download-demo` recovery.

- [ ] **Step 4: Skip commit unless user requested commits**

---

### Task 4: Implement base-name stripping and wire URL / defaults

**Files:**
- Modify: `src/yai.hpp`
- Modify: `src/arch.cpp`
- Modify: `src/core.cpp`
- Modify: `src/cli_download.cpp` (`fill_install_defaults_for_direct_target`)
- Modify: `src/resolver.cpp` (`resolve_url_install_source`)

**Interfaces:**
- Produces:
  - `bool token_looks_like_arch(const std::string& token);`
  - `std::string base_name_from_appimage_filename(const std::string& filename);`
- Consumes: `arch_alias_rules()`, `strip_appimage_suffix`, `sanitize_id`, `to_lower`, `normalize_arch`

- [ ] **Step 1: Declare APIs in `src/yai.hpp`**

Near `sanitize_id` / arch declarations:

```cpp
bool token_looks_like_arch(const std::string& token);
std::string base_name_from_appimage_filename(const std::string& filename);
```

- [ ] **Step 2: Implement `token_looks_like_arch` in `src/arch.cpp`**

```cpp
bool token_looks_like_arch(const std::string& token) {
    const std::string lower = to_lower(trim(token));
    if (lower.empty()) {
        return false;
    }
    for (const ArchAliasRule& rule : arch_alias_rules()) {
        if (lower == rule.canonical) {
            return true;
        }
        for (const std::string& alias : rule.aliases) {
            if (lower == alias) {
                return true;
            }
        }
    }
    return false;
}
```

- [ ] **Step 3: Implement `base_name_from_appimage_filename` in `src/core.cpp`**

```cpp
namespace {

bool token_looks_like_version(const std::string& token) {
    if (token.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (token[0] == 'v' || token[0] == 'V') {
        if (token.size() == 1) {
            return false;
        }
        i = 1;
    }
    bool saw_digit = false;
    for (; i < token.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(token[i]);
        if (ch >= '0' && ch <= '9') {
            saw_digit = true;
            continue;
        }
        if (ch == '.') {
            continue;
        }
        return false;
    }
    return saw_digit;
}

}  // namespace

std::string base_name_from_appimage_filename(const std::string& filename) {
    const std::string stem = strip_appimage_suffix(fs::path(filename).filename().string());
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : stem) {
        if (ch == '-' || ch == '_') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }

    while (!tokens.empty() &&
           (token_looks_like_arch(tokens.back()) || token_looks_like_version(tokens.back()))) {
        tokens.pop_back();
    }

    if (tokens.empty()) {
        return stem;
    }

    std::string out = tokens.front();
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        out.push_back('-');
        out += tokens[i];
    }
    return out;
}
```

- [ ] **Step 4: Wire defaults and URL resolver**

In `fill_install_defaults_for_direct_target`:

```cpp
const std::string base = basename_from_url(options.target);
const std::string stripped = base_name_from_appimage_filename(base);
if (options.id.empty()) {
    options.id = sanitize_id(stripped);
}
if (options.name.empty()) {
    options.name = stripped.empty() ? strip_appimage_suffix(base) : stripped;
}
```

In `resolve_url_install_source`:

```cpp
const std::string base = basename_from_url(options.target);
const std::string stripped = base_name_from_appimage_filename(base);
source.id = options.id.empty() ? sanitize_id(stripped) : options.id;
source.name = options.name.empty()
    ? (stripped.empty() ? strip_appimage_suffix(base) : stripped)
    : options.name;
```

- [ ] **Step 5: Rebuild; run download smoke**

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/download_smoke.sh
```

Expected: URL `autodownload.AppImage` section PASS; local recovery sections may still FAIL until Task 6.

- [ ] **Step 6: Skip commit unless user requested commits**

---

### Task 5: Implement local repo recovery + local strip fallback

**Files:**
- Modify: `src/resolver.cpp` (`resolve_local_install_source`)
- Optionally modify: `src/yai.hpp` + `src/repo.cpp` if extracting `match_repo_package_for_local_stem`

**Interfaces:**
- Consumes: `load_repo_packages`, `base_name_from_appimage_filename`, `id_explicit` / `name_explicit`
- Produces: local installs with repo id when stem matches; otherwise stripped id

- [ ] **Step 1: Add matching helper (anonymous in `resolver.cpp` is fine)**

```cpp
std::optional<RepoPackage> match_repo_package_for_local_stem(const std::string& stem) {
    std::vector<RepoPackage> packages;
    try {
        packages = load_repo_packages();
    } catch (const std::exception&) {
        return std::nullopt;
    }

    std::vector<const RepoPackage*> candidates;
    for (const RepoPackage& package : packages) {
        if (package.id.empty()) {
            continue;
        }
        if (stem == package.id || stem.rfind(package.id + "-", 0) == 0) {
            candidates.push_back(&package);
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }

    std::size_t best_len = 0;
    for (const RepoPackage* package : candidates) {
        best_len = std::max(best_len, package->id.size());
    }
    std::vector<const RepoPackage*> best;
    for (const RepoPackage* package : candidates) {
        if (package->id.size() == best_len) {
            best.push_back(package);
        }
    }
    if (best.size() != 1) {
        return std::nullopt;
    }
    return *best.front();
}
```

- [ ] **Step 2: Rewrite id/name assignment in `resolve_local_install_source`**

Critical: use `id_explicit` / `name_explicit`, not `options.id.empty()`, because `fill_install_defaults_for_direct_target` already fills id for `.AppImage` paths.

```cpp
ResolvedSource resolve_local_install_source(const InstallOptions& options) {
    const fs::path local_path = options.target;
    if (!fs::exists(local_path)) {
        throw std::runtime_error(tr("local AppImage file does not exist: ") + options.target);
    }
    if (!fs::is_regular_file(local_path)) {
        throw std::runtime_error(tr("local AppImage target is not a file: ") + options.target);
    }

    const std::string filename = local_path.filename().string();
    const std::string full_stem_id = sanitize_id(strip_appimage_suffix(filename));
    const std::string stripped = base_name_from_appimage_filename(filename);
    const std::optional<RepoPackage> matched =
        options.id_explicit ? std::nullopt : match_repo_package_for_local_stem(full_stem_id);

    ResolvedSource source;
    source.source_kind = "local_path";
    if (options.id_explicit) {
        source.id = options.id;
    } else if (matched.has_value()) {
        source.id = sanitize_id(matched->id);
    } else {
        source.id = sanitize_id(stripped);
    }

    if (options.name_explicit) {
        source.name = options.name;
    } else if (matched.has_value()) {
        source.name = matched->name;
    } else {
        source.name = stripped.empty() ? strip_appimage_suffix(filename) : stripped;
    }

    source.source_url = fs::absolute(local_path).lexically_normal().string();
    source.download_url = source.source_url;
    return with_install_arch(source, options);
}
```

- [ ] **Step 3: Rebuild and run full relevant smokes**

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/download_smoke.sh
bash /home/fsx/yai/tests/stage1_smoke.sh
```

Expected: both PASS (`Parallel-One.AppImage` still → `parallel-one`; explicit `--id` paths unchanged).

- [ ] **Step 4: Skip commit unless user requested commits**

---

### Task 6: Documentation

**Files:**
- Modify: `README.md`
- Modify: `AppImage包管理器开发文档.md`

**Interfaces:**
- Consumes: final behavior from Tasks 2–5
- Produces: docs matching spec

- [ ] **Step 1: Update README download paragraph**

Replace the “upstream file name” sentence with: download saves as `{resolved package id}.AppImage` in the current directory. Note that URL/local default ids strip trailing version and architecture tokens; local install without `--id` tries to match a repo package by filename stem.

- [ ] **Step 2: Update Chinese developer doc**

Same substance at the `download` command description (~line 194) and the implementation note about upstream filenames (~line 347).

- [ ] **Step 3: Re-run download smoke once after doc-only edits (sanity)**

```bash
bash /home/fsx/yai/tests/download_smoke.sh
```

Expected: PASS.

- [ ] **Step 4: Skip commit unless user requested commits**

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| download → `{source.id}.AppImage` | 1–2 |
| `base_name_from_appimage_filename` strip arch/version | 4 |
| URL defaults use strip | 3–4 |
| Local fallback uses strip | 3, 5 |
| Local repo recovery by exact/prefix + longest id | 3, 5 |
| Keep `local_path`; no sidecar; `--id` wins | 5 |
| Docs + smoke coverage | 1, 3, 6 |

## Self-review notes

- No TBD placeholders remain; helper names match the spec.
- `id_explicit` vs `id.empty()` called out so Task 5 does not silently fail behind `fill_install_defaults_for_direct_target`.
- `token_looks_like_arch` matches whole tokens only (aliases/canonical), avoiding false positives from needles like `x86.appimage`.
