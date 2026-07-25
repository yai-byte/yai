# Task 3 Report: Wire search marker + i18n

## Status
**DONE**

## What Was Implemented

Wired installed-package marker into `search_packages` and added i18n strings per task brief.

### Modified: `src/commands_query.cpp`
- Replaced `search_packages` body: build three-column TSV line, append space + `tr("[installed]")` when `metadata_exists(paths_for(package.id))`, wrap full line with `color_green`, then print.
- Keyword matching unchanged (`package_matches_keyword`); tag is not part of match logic.

### Modified: `po/en.po`
- Added `msgid "[installed]"` / `msgstr "[installed]"` after search keyword error string.

### Modified: `po/zh.po`
- Added `msgid "[installed]"` / `msgstr "[已安装]"` in same location.

## Build Verification

```bash
make -C /home/fsx/yai/.worktrees/search-installed-marker
```

- Exit code: 0
- Warnings: none

## TDD GREEN Evidence

Task 1 smoke was RED before this task (missing tag). After Task 3:

```bash
bash /home/fsx/yai/.worktrees/search-installed-marker/tests/stage4_smoke.sh
```

Output (final line):
```
stage4 smoke test passed
```

Exit code: 0

Assertions satisfied:
- `[installed]` on installed `repo-demo` hit; absent on uninstalled `repo-delta`
- Tag is summary suffix (not fourth TSV column)
- Piped search: tag present, no ANSI (`\033`)
- `NO_COLOR=1`: tag present, no ANSI
- `YAI_LANG=zh`: `[已安装]` present, no English `[installed]`

Related smoke:

```bash
bash /home/fsx/yai/.worktrees/search-installed-marker/tests/repo_smoke.sh
```

Output (final line):
```
repo smoke test passed
```

Exit code: 0

## Files Changed

| File | Action |
|------|--------|
| `src/commands_query.cpp` | Modified (`search_packages`) |
| `po/en.po` | Modified (`[installed]`) |
| `po/zh.po` | Modified (`[已安装]`) |

## Spec Coverage (Self-Review)

| Spec requirement | Verified |
|------------------|----------|
| Trailing localized `[installed]` / `[已安装]` | stage4 grep assertions |
| Whole-line green on TTY | `color_green(line)` wraps full line |
| Always show text tag; no ANSI when not TTY | pipe assertion in stage4 |
| `NO_COLOR` disables ANSI | stage4 `NO_COLOR=1` assertion |
| Three-column TSV; tag is summary suffix | stage4 tab-column guards |
| yai-only install detection via metadata | `metadata_exists(paths_for(...))` |
| Tag not part of keyword match | match path unchanged |
| Out of scope items | not implemented |

## Self-Review Notes

1. **Scope** — Only `search_packages` + po entries; no other commands touched.
2. **Interfaces** — Uses Task 2 `stdout_color_enabled` / `color_green` via `color_green(line)`.
3. **i18n** — Tag uses `tr("[installed]")`; zh.po provides `[已安装]`.
4. **Layout** — Space before tag keeps three tab columns; tag appended to summary field.
5. **Regression** — `repo_smoke.sh` still passes (grep on id unaffected by optional suffix).

## Concerns

None.

## Commits

- `a07ead7` Mark installed packages in search with green tag.

## Base Commit

- `c15cba0` (Task 2) — Add stdout green color helpers honoring NO_COLOR.

## Final-review fixes

### Changes
- **`src/commands_query.cpp`**: Added `installed_package_id_set()` (anonymous namespace) building `std::unordered_set<std::string>` once before the search loop, mirroring `list_apps` / `installed_package_ids` enumeration; replaced per-hit `metadata_exists(paths_for(...))` with O(1) set lookup. Output unchanged (tag + `color_green`).
- **`tests/stage4_smoke.sh`**: Added optional pseudo-TTY checks via `script -qefc` — TTY expects `\033[32m` and `[installed]`; TTY + `NO_COLOR=1` expects tag without `\033`. Skips with stderr message when `script` is unavailable. Existing pipe / non-TTY assertions kept. No separate empty-install-set test (redundant with post-install `repo-delta` check).

### Test results

```bash
make
bash tests/stage4_smoke.sh
bash tests/repo_smoke.sh
```

- `make`: exit 0, rebuilt `commands_query.cpp`
- `stage4_smoke.sh`: exit 0 — `stage4 smoke test passed` (TTY block skipped: `script` not installed on runner)
- `repo_smoke.sh`: exit 0 — `repo smoke test passed`
