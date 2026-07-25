# Direct URL Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `yai update` / `yai upgrade` work for `source_kind=url`, and detect same-URL content changes for `repo_direct_url`, using HTTP validators first and sha256 as fallback.

**Architecture:** Add a focused `url_freshness` module (parse/compare/probe). Persist optional `http_*` fields in metadata. Capture validators from download response headers before they are deleted. Wire preview and upgrade so plain URL is no longer unsupported, and same-URL `repo_direct_url` packages are not stuck on basename/version identity alone.

**Tech Stack:** C++17, existing `tr()` / `po/*.po`, curl `--head` / `--dump-header`, shell smoke tests under `tests/`, Makefile.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-25-url-direct-update-design.md` exactly
- Do not change GitHub / `repo_website_page` / `local_path` update semantics
- `yai update` must never download the AppImage body (HEAD / local file metadata / existing sha256 only)
- Upgradability for direct-url kinds must not depend on `version` string equality alone
- msgid = English source text; sync `po/en.po` and `po/zh.po` when adding user-facing strings
- Do not commit unless the user explicitly asks; skip commit steps when commits are not requested

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | `HttpValidators`, `UrlFreshnessOutcome`, probe/parse/compare declarations; optional `http_*` on `ResolvedSource`; extend `write_metadata` / `download_file` signatures |
| `src/url_freshness.cpp` | Parse headers, compare validators, probe URL freshness (HEAD + file:// size), map outcomes |
| `src/cli_download.cpp` | `download_file` returns `HttpValidators` parsed before deleting `.headers` |
| `src/resolver_github.cpp` | `download_with_strategy` propagates validators onto `ResolvedSource` |
| `src/resolver.cpp` | `stage_appimage_source` takes non-const `ResolvedSource&` and fills `http_*` |
| `src/appimage.cpp` | `write_metadata` writes `http_etag` / `http_last_modified` / `http_content_length` |
| `src/commands_update.cpp` | Preview for `url` + freshness for same-URL `repo_direct_url` |
| `src/commands_upgrade.cpp` | Allow `url`; same-URL freshness; sha256 short-circuit after download |
| `src/commands_lifecycle.cpp` | Install path already stages into `ResolvedSource` — ensure validators survive to `write_metadata` |
| `Makefile` | Compile `src/url_freshness.cpp` |
| `tests/url_freshness_smoke.sh` | Unit-style checks for parse/compare |
| `tests/url_update_smoke.sh` | End-to-end URL + same-URL direct_url update/upgrade |
| `tests/stage5_smoke.sh` | Keep existing index-URL-change case; adjust if plain-URL preview text appears in `update` all |
| `README.md`, `AppImage包管理器开发文档.md` | Document new upgradable kinds |
| `po/en.po`, `po/zh.po` | New/changed msgids |

---

### Task 1: HttpValidators parse + compare (failing unit smoke)

**Files:**
- Create: `tests/url_freshness_smoke.sh`
- Create: `src/url_freshness.cpp` (minimal stubs only after red)
- Modify: `src/yai.hpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: none
- Produces:
  - `struct HttpValidators { std::string etag; std::string last_modified; std::string content_length; };`
  - `bool http_validators_empty(const HttpValidators& v);`
  - `HttpValidators parse_http_validators_from_headers(const fs::path& headers);`
  - `enum class UrlFreshness { Unchanged, Changed, Unknown, Error };` (or string status equivalents if enum is awkward across files — prefer enum class in `yai.hpp`)
  - `UrlFreshness compare_http_validators(const HttpValidators& stored, const HttpValidators& remote);`

**Compare rules (lock these):**
- If remote has ETag and stored has ETag: equal → contribute match; unequal → `Changed`
- Else if remote has Last-Modified and stored has Last-Modified: equal → match; unequal → `Changed`
- Else if neither ETag nor Last-Modified pair exists, but both have Content-Length: equal → match; unequal → `Changed`
- If at least one comparable pair exists and all such pairs match → `Unchanged`
- If no comparable pair exists → `Unknown`
- Case-insensitive header names; trim values; for ETag, compare trimmed values as returned (keep weak/strong `W/` prefix if present)

- [ ] **Step 1: Add failing smoke that compiles a tiny C++ driver against `yai.hpp` + linked objects**

Create `tests/url_freshness_smoke.sh` modeled on `tests/json_smoke.sh`: write a temp `url_freshness_test.cpp` that `#include "yai.hpp"`, compile with the same sources the parser needs (at least `url_freshness.cpp` plus whatever it calls — start with only `url_freshness.cpp` if self-contained, else link `core.cpp` for `trim`/`to_lower`).

Assertions:

```cpp
// Write a headers file with final response block:
// HTTP/1.1 200 OK
// ETag: "abc"
// Last-Modified: Sat, 01 Jan 2026 00:00:00 GMT
// Content-Length: 42

HttpValidators v = parse_http_validators_from_headers(path);
require(v.etag == "\"abc\"", "etag");
require(v.last_modified == "Sat, 01 Jan 2026 00:00:00 GMT", "lm");
require(v.content_length == "42", "len");

HttpValidators stored{"\"abc\"", "Sat, 01 Jan 2026 00:00:00 GMT", "42"};
require(compare_http_validators(stored, v) == UrlFreshness::Unchanged, "unchanged");

HttpValidators remote_changed = v;
remote_changed.etag = "\"xyz\"";
require(compare_http_validators(stored, remote_changed) == UrlFreshness::Changed, "etag changed");

HttpValidators empty{};
require(compare_http_validators(empty, empty) == UrlFreshness::Unknown, "unknown");
require(compare_http_validators(stored, empty) == UrlFreshness::Unknown, "remote empty");
```

Also assert that when multiple HTTP response blocks exist (redirects), **last** ETag / Last-Modified / Content-Length win (same approach as `download_total_from_headers`).

- [ ] **Step 2: Run smoke; expect compile or link failure**

```bash
bash /home/fsx/yai/tests/url_freshness_smoke.sh
```

Expected: FAIL — missing symbols / missing source file.

- [ ] **Step 3: Declare types and functions in `src/yai.hpp`; add `src/url_freshness.cpp`; add to `Makefile` `SRC`**

Implement parse + compare only (no network probe yet).

Reuse `trim` / `to_lower` from existing helpers. Prefer last matching header occurrence.

- [ ] **Step 4: Re-run smoke; expect PASS**

```bash
bash /home/fsx/yai/tests/url_freshness_smoke.sh
```

Expected: `url freshness smoke test passed` (or equivalent).

- [ ] **Step 5: Skip commit unless user requested commits**

---

### Task 2: Probe URL freshness (HEAD + file://)

**Files:**
- Modify: `src/yai.hpp`
- Modify: `src/url_freshness.cpp`
- Modify: `tests/url_freshness_smoke.sh`

**Interfaces:**
- Consumes: `parse_http_validators_from_headers`, `compare_http_validators`, existing `is_file_url`, `run_process_capture`, `executable_available`
- Produces:
  - `struct UrlFreshnessResult { UrlFreshness status; std::string detail; HttpValidators remote; };`
  - `UrlFreshnessResult probe_url_freshness(const std::string& url, const HttpValidators& stored);`

**Probe algorithm:**
1. If `is_file_url(url)`: resolve path (strip `file://`); if file missing → `{Error, ...}`; else build remote validators with `content_length = std::to_string(file_size)` only; `return {compare_http_validators(stored, remote), ..., remote}`.
2. Else if `curl` unavailable → `{Unknown, "curl is not available for freshness probe", {}}` (upgrade will GET).
3. Else run HEAD equivalent to `prefetch_download_headers` but **keep** the headers file, parse it, delete temp file:
   - On hard failure (non-zero exit / timeout with empty/unusable headers) → `{Error, ...}`
   - On success with empty validators → `{Unknown, ...}`
   - Else `{compare_http_validators(stored, remote), ..., remote}`

Do not download AppImage body in this function.

- [ ] **Step 1: Extend unit smoke with file:// size compare**

In the temp C++ driver (or a second section of the shell script writing a real file):

```cpp
// Write file of 4 bytes; stored content_length "4" → Unchanged
// Change stored to "5" → Changed
// stored empty → Unknown
```

- [ ] **Step 2: Run smoke; expect FAIL (probe missing)**

```bash
bash /home/fsx/yai/tests/url_freshness_smoke.sh
```

- [ ] **Step 3: Implement `probe_url_freshness`**

Use a temp headers path under `fs::temp_directory_path()` with a unique name; always best-effort remove it.

For HEAD args, mirror `prefetch_download_headers` (`curl --fail --location --silent --show-error --head --dump-header ... --output /dev/null url`) but do **not** delete headers when Content-Length is missing — empty validators are a valid `Unknown` outcome.

- [ ] **Step 4: Re-run smoke; expect PASS**

- [ ] **Step 5: Skip commit unless user requested commits**

---

### Task 3: Capture validators on download and write metadata

**Files:**
- Modify: `src/yai.hpp` (`ResolvedSource` add `http_etag`, `http_last_modified`, `http_content_length`; change `download_file` / `stage_appimage_source` / `write_metadata` declarations)
- Modify: `src/cli_download.cpp`
- Modify: `src/resolver_github.cpp` (`download_with_strategy`)
- Modify: `src/resolver.cpp` (`stage_appimage_source`)
- Modify: `src/appimage.cpp` (`write_metadata`)
- Modify: `src/commands_lifecycle.cpp` / `src/commands_upgrade.cpp` as needed for signature changes
- Modify: `tests/metadata_json_smoke.sh` only if it asserts exact JSON shape without `http_*` keys — prefer always writing the three keys (empty string when unknown) to keep shape stable

**Interfaces:**
- Consumes: `parse_http_validators_from_headers`
- Produces: install/upgrade metadata may contain:
  - `"http_etag"`
  - `"http_last_modified"`
  - `"http_content_length"`

- [ ] **Step 1: Change `download_file` to return `HttpValidators`**

Before `remove_best_effort(headers)` on success path, parse validators. On failure paths return empty validators / throw as today.

```cpp
HttpValidators download_file(const std::string& url, const fs::path& target, const std::string& downloader);
```

Update all call sites. `download_with_strategy` should accept `ResolvedSource& source` (or an out-param `HttpValidators*`) and assign:

```cpp
source.http_etag = validators.etag;
source.http_last_modified = validators.last_modified;
source.http_content_length = validators.content_length;
```

Change `stage_appimage_source` to take `ResolvedSource& source` so validators from the final successful download remain on `source`. Return value stays the downloaded URL string. Update callers that currently pass `const ResolvedSource&` / temporary to use a mutable `ResolvedSource`.

- [ ] **Step 2: Extend `write_metadata`**

After `download_url` (or near other source fields), write:

```cpp
"  \"http_etag\": \"" + json_escape_string(source.http_etag) + "\",\n"
"  \"http_last_modified\": \"" + json_escape_string(source.http_last_modified) + "\",\n"
"  \"http_content_length\": \"" + json_escape_string(source.http_content_length) + "\",\n"
```

Always emit the keys (empty allowed) so readers can rely on key presence.

- [ ] **Step 3: Rebuild and run existing smokes that install packages**

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/stage1_smoke.sh
bash /home/fsx/yai/tests/metadata_json_smoke.sh
```

Expected: PASS (or fix metadata_json expectations if they snapshot full JSON).

- [ ] **Step 4: Skip commit unless user requested commits**

---

### Task 4: `yai update` preview for `url` and same-URL `repo_direct_url`

**Files:**
- Modify: `src/commands_update.cpp`
- Modify: `po/en.po`, `po/zh.po`
- Create: `tests/url_update_smoke.sh` (preview assertions first; upgrade in Task 5)

**Interfaces:**
- Consumes: `probe_url_freshness`, `HttpValidators` loaded from metadata via `metadata_value`
- Produces: preview rows for `url`; enhanced `repo_direct_url` when index URL unchanged

**Helper to load stored validators from metadata path:**

```cpp
HttpValidators validators_from_metadata(const fs::path& metadata) {
    HttpValidators v;
    v.etag = metadata_value(metadata, "http_etag").value_or("");
    v.last_modified = metadata_value(metadata, "http_last_modified").value_or("");
    v.content_length = metadata_value(metadata, "http_content_length").value_or("");
    return v;
}
```

Place in `url_freshness.cpp` or anonymous namespace in `commands_update.cpp` — prefer shared in `url_freshness.cpp` / declared in `yai.hpp` if upgrade also needs it.

**Preview mapping:**
- `UrlFreshness::Unchanged` → status `current`, reason `already up to date`
- `UrlFreshness::Changed` → status `upgradable`, reason `remote content changed` (or include URL)
- `UrlFreshness::Unknown` → status `upgradable`, reason `download verification required`
- `UrlFreshness::Error` → status `error`, reason = detail

Remove `url` from the unsupported branch in `build_update_preview`. Keep `unsupported_update_reason` for `local_path` (can leave the unused `url` msgid for now or delete from po when no longer referenced).

For `repo_direct_url`:
1. Resolve repo source as today
2. If `update_source_identity_changed` (URL or version identity) → existing `upgradable` path
3. Else probe `source.source_url` (or resolved URL) with stored validators → map as above

For `url`:
1. Candidate URL = metadata `source_url`, else `download_url`
2. If empty → `error`
3. Else probe and map

- [ ] **Step 1: Write failing end-to-end preview smoke**

`tests/url_update_smoke.sh`:

1. Build tiny AppImage via existing `write_appimage` pattern from stage5
2. `yai install "file://$ASSET"` with `--id url-updatable`
3. Manually ensure metadata has `source_kind` `url` (install from URL should already)
4. Seed metadata `http_content_length` to match file size (or rely on Task 3 capture)
5. `yai update url-updatable` → expect `current` when size matches
6. Append bytes to the AppImage file (same path) so size changes
7. `yai update url-updatable` → expect `upgradable` and reason matching `remote content changed` **or** `download verification required` depending on whether length was stored

Also add a fake-curl HTTPS case optional if file:// length probing is enough for Changed/Unchanged; file:// is sufficient for Task 4 if Task 2 file:// probe is live.

Include a `repo_direct_url` same-URL case: install from index pointing at `file://$SAME`; overwrite file content (same path, different bytes, **same size** if possible by rewriting in place with same length — or change length); with unchanged index URL, preview must not say `current` solely because basename/version matched.

For same-size content change without ETag: expect `Unknown` → `upgradable` / `download verification required`.

- [ ] **Step 2: Run smoke; expect FAIL (url still unsupported)**

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/url_update_smoke.sh
```

Expected: FAIL — `unsupported` / plain URL reason.

- [ ] **Step 3: Implement preview wiring + po strings**

New msgids (English):
- `remote content changed`
- `download verification required`

Chinese msgstr in `po/zh.po` (natural equivalents).

- [ ] **Step 4: Re-run `tests/url_update_smoke.sh` preview sections; expect PASS for preview**

If the script already includes upgrade steps, temporarily comment them until Task 5, or split with a marker.

- [ ] **Step 5: Skip commit unless user requested commits**

---

### Task 5: `yai upgrade` for `url` and same-URL content changes

**Files:**
- Modify: `src/commands_upgrade.cpp`
- Modify: `tests/url_update_smoke.sh`
- Modify: `po/en.po`, `po/zh.po` if new strings

**Interfaces:**
- Consumes: `probe_url_freshness`, `validators_from_metadata`, existing download/probe/commit transaction, `sha256_file`
- Produces: upgradable plain URL installs; same-URL repo_direct_url upgrades when content changes; sha256-equal short-circuit

**`load_update_context`:** allow `source_kind == "url"` (remove it from the throw list).

**`upgrade_installed_target` flow changes:**

```text
if github* → existing
else if repo_website_page → existing resolve + identity changed check
else if repo_direct_url → resolve repo source
    if identity changed → download/commit as today
    else → treat like direct URL freshness path below using resolved source_url
else if url → build ResolvedSource from metadata (id/name/version/arch/source_url/download_url/source_kind)
    run freshness path
```

**Direct URL freshness path:**
1. `stored = validators_from_metadata(...)`
2. `probe = probe_url_freshness(candidate_url, stored)`
3. If `Unchanged` → `print_already_up_to_date`; return
4. If `Error` → throw with probe.detail
5. If `Changed` or `Unknown` → set `source.source_url` / `source.download_url` to candidate; `download_and_probe_update_candidate`
6. Compare `sha256_file(candidate)` to metadata `sha256`:
   - Equal: `cleanup_update_candidate`; best-effort update metadata http_* from `source` without changing appimage (call a small helper or `write_metadata` only after copying http fields onto a source rebuilt from current metadata — simplest: read current source fields from metadata into `ResolvedSource`, overlay `http_*` from download, `write_metadata` with current mode from metadata `install_mode`); print already up to date; return
   - Different: `commit_update_transaction` as today (writes metadata including new http_* and sha256)

Ensure `build_update_source` / repo resolve still set `version` via existing basename rules.

- [ ] **Step 1: Extend `tests/url_update_smoke.sh` with upgrade cases**

Cases:
1. URL install; grow file; `upgrade --yes` → new content runs; metadata sha256 changed; http_* refreshed when available
2. URL install; no change; `upgrade` → already up to date; no `versions/previous` required
3. `repo_direct_url` same URL; change file bytes; upgrade applies
4. After upgrade failure injection optional — skip unless easy; stage5 already covers rollback for GitHub

- [ ] **Step 2: Run smoke; expect FAIL on upgrade**

```bash
bash /home/fsx/yai/tests/url_update_smoke.sh
```

- [ ] **Step 3: Implement upgrade wiring**

Remove / stop using msgid `plain URL install has no stable update source` from upgrade throw path (preview already handled). Keep `local_path` unsupported.

- [ ] **Step 4: Re-run smokes**

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/url_update_smoke.sh
bash /home/fsx/yai/tests/stage5_smoke.sh
bash /home/fsx/yai/tests/url_freshness_smoke.sh
```

Expected: all PASS. stage5 direct-updatable index URL change behavior unchanged.

- [ ] **Step 5: Skip commit unless user requested commits**

---

### Task 6: Docs + README product text

**Files:**
- Modify: `README.md` (update section ~lines 102–110)
- Modify: `AppImage包管理器开发文档.md` (wherever update/unsupported URL is described — search for `plain URL` / `source_url` / update)

**Interfaces:**
- Consumes: shipped behavior from Tasks 1–5
- Produces: accurate user/developer docs

- [ ] **Step 1: Update README**

Replace the sentence that plain URL installs remain unsupported for update. New behavior summary:

- Plain URL installs use the recorded download URL; `update` probes HTTP validators (or file size for `file://`) and may mark `download verification required` without downloading
- `upgrade` re-fetches and compares sha256
- `repo_direct_url` still follows index URL changes; same URL also uses freshness probing
- `local_path` remains unsupported

- [ ] **Step 2: Update Chinese developer doc in the matching section**

Keep terminology consistent with metadata field names.

- [ ] **Step 3: Skip commit unless user requested commits**

---

## Self-review (plan vs spec)

| Spec requirement | Task |
|------------------|------|
| Enable update/upgrade for `url` | 4, 5 |
| `repo_direct_url` index URL wins | 4, 5 (identity_changed first) |
| Same URL → HTTP then sha256 | 1–2, 4–5 |
| Persist `http_*` | 3 |
| Shared freshness helper | 1–2 |
| `update` does not download AppImage body | 2, 4 |
| Version string not sole signal | 4–5 |
| Out of scope kinds untouched | Global + Task 5 github/website branches |
| README / docs | 6 |
| Tests for matrix in spec | 1, 4, 5 |

No TBD placeholders. `HttpValidators` / `UrlFreshness` / `probe_url_freshness` names are consistent across tasks. `download_file` return type change is called out with call-site updates.
