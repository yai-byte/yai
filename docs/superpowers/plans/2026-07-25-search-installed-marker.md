# Search Installed Marker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mark yai-installed packages in `yai search` with a localized trailing tag and green whole-line color on TTY (no ANSI when piped or when `NO_COLOR` is set).

**Architecture:** Add tiny stdout color helpers (`stdout_color_enabled`, `color_green`). In `search_packages`, after a keyword match, if `metadata_exists(paths_for(id))`, append ` tr("[installed]")` to the summary and wrap the full line with `color_green` before printing. Keep the three-column TSV layout; tag is a summary suffix, not a new column.

**Tech Stack:** C++17, existing `tr()` / `env` patterns, `isatty(STDOUT_FILENO)`, shell smoke tests, Makefile, gettext-style `po/*.po`.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-25-search-installed-marker-design.md` exactly
- msgid = English source text; sync `po/en.po` and `po/zh.po`
- Do not change the stable `id\tname\tsummary` column count (tag is summary suffix only)
- Do not color or tag `list` / `info` / other commands
- Do not detect flatpak/rpm/system packages—only yai-managed AppImages via `metadata_exists`
- Prefer mechanical style matching existing query/progress code
- Commit after each task unless the user forbids commits

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | Declare `stdout_color_enabled`, `color_green` |
| `src/terminal_color.cpp` | Implement TTY + `NO_COLOR` checks and green wrap |
| `Makefile` | Compile `src/terminal_color.cpp` |
| `src/commands_query.cpp` | Wire marker + color into `search_packages` |
| `po/en.po`, `po/zh.po` | msgid `[installed]` / zh `[已安装]` |
| `tests/stage4_smoke.sh` | Installed tag, uninstalled clean, piped no-ANSI, `NO_COLOR`, zh tag |

---

### Task 1: Failing smoke for installed search marker

**Files:**
- Modify: `tests/stage4_smoke.sh`

**Interfaces:**
- Consumes: none yet (tests expect future CLI behavior)
- Produces: failing assertions that Task 2–3 must satisfy

- [ ] **Step 1: Extend `tests/stage4_smoke.sh` after install succeeds, before remove**

After the line that runs `repo-demo` and confirms `list` contains `repo-demo` (around the block ending with `HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "repo-demo"`), **before** `HOME="$TMP_HOME" "$ROOT/yai" remove 'repo-*'`, insert:

```bash
# --- search installed marker ---
SEARCH_INSTALLED="$(HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" search repo-demo)"
printf '%s\n' "$SEARCH_INSTALLED" | grep -q $'repo-demo\tRepository Demo\t'
printf '%s\n' "$SEARCH_INSTALLED" | grep -q '\[installed\]'
# Tag must be a summary suffix, not a fourth tab column before the tag text alone
if printf '%s\n' "$SEARCH_INSTALLED" | grep -q $'\t\[installed\]$'; then
  echo "installed tag must not be a separate TSV column" >&2
  exit 1
fi
if printf '%s\n' "$SEARCH_INSTALLED" | grep -q $'\[installed\]\t'; then
  echo "installed tag must not appear before a tab column" >&2
  exit 1
fi

SEARCH_OTHER="$(HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" search repo-delta)"
printf '%s\n' "$SEARCH_OTHER" | grep -q "repo-delta"
if printf '%s\n' "$SEARCH_OTHER" | grep -q '\[installed\]'; then
  echo "uninstalled search hit unexpectedly had [installed] tag" >&2
  exit 1
fi

SEARCH_PIPE="$(HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" search repo-demo | cat)"
printf '%s\n' "$SEARCH_PIPE" | grep -q '\[installed\]'
if printf '%s\n' "$SEARCH_PIPE" | grep -q $'\033'; then
  echo "piped search output contained ANSI escapes" >&2
  exit 1
fi

SEARCH_NO_COLOR="$(HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" NO_COLOR=1 "$ROOT/yai" search repo-demo)"
printf '%s\n' "$SEARCH_NO_COLOR" | grep -q '\[installed\]'
if printf '%s\n' "$SEARCH_NO_COLOR" | grep -q $'\033'; then
  echo "NO_COLOR=1 search output contained ANSI escapes" >&2
  exit 1
fi

SEARCH_ZH="$(HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" YAI_LANG=zh "$ROOT/yai" search repo-demo)"
printf '%s\n' "$SEARCH_ZH" | grep -q '\[已安装\]'
if printf '%s\n' "$SEARCH_ZH" | grep -q '\[installed\]'; then
  echo "zh search still showed English [installed] tag" >&2
  exit 1
fi
```

Keep the existing `remove` line after these checks so cleanup still runs.

- [ ] **Step 2: Rebuild and run smoke to confirm failure**

Run:

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/stage4_smoke.sh
```

Expected: FAIL near the new `[installed]` assertion (tag missing), non-zero exit.

- [ ] **Step 3: Commit the failing test**

```bash
git add tests/stage4_smoke.sh
git commit -m "$(cat <<'EOF'
Add failing smoke for search installed marker.

EOF
)"
```

---

### Task 2: Terminal color helpers

**Files:**
- Create: `src/terminal_color.cpp`
- Modify: `src/yai.hpp` (declare helpers near other terminal helpers, after `terminal_width`)
- Modify: `Makefile` (add `src/terminal_color.cpp` to `SRC`)

**Interfaces:**
- Consumes: `isatty`, `std::getenv` (via `<unistd.h>` / `<cstdlib>` already in `yai.hpp`)
- Produces:
  - `bool stdout_color_enabled();`
  - `std::string color_green(const std::string& text);`

- [ ] **Step 1: Declare helpers in `src/yai.hpp`**

Immediately after `std::size_t terminal_width();`, add:

```cpp
bool stdout_color_enabled();
std::string color_green(const std::string& text);
```

- [ ] **Step 2: Implement `src/terminal_color.cpp`**

```cpp
#include "yai.hpp"

bool stdout_color_enabled() {
    // no-color.org: presence of NO_COLOR (including empty) disables color.
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    return isatty(STDOUT_FILENO) != 0;
}

std::string color_green(const std::string& text) {
    if (!stdout_color_enabled()) {
        return text;
    }
    return std::string("\033[32m") + text + "\033[0m";
}
```

- [ ] **Step 3: Register the new source in `Makefile`**

In the `SRC := \` list, add `src/terminal_color.cpp \` after `src/download_progress.cpp \` (keep alphabetical-ish grouping with other `src/` files; place it after `src/repo_feed.cpp` alphabetically would be `terminal_color` near the end—insert after `src/repo_feed.cpp \` and before `src/resolver.cpp \`, or simply after `download_progress.cpp`).

Exact line to add among `SRC`:

```makefile
	src/terminal_color.cpp \
```

Place it after `src/repo_feed.cpp \` so the list stays roughly alphabetical (`t` after `repo_feed`, before `resolver`).

- [ ] **Step 4: Build**

Run:

```bash
make -C /home/fsx/yai
```

Expected: successful link of `yai` with no new warnings about missing symbols.

- [ ] **Step 5: Commit**

```bash
git add src/yai.hpp src/terminal_color.cpp Makefile
git commit -m "$(cat <<'EOF'
Add stdout green color helpers honoring NO_COLOR.

EOF
)"
```

---

### Task 3: Wire search marker + i18n

**Files:**
- Modify: `src/commands_query.cpp` (`search_packages`)
- Modify: `po/en.po`
- Modify: `po/zh.po`

**Interfaces:**
- Consumes: `stdout_color_enabled` / `color_green` from Task 2; `metadata_exists`, `paths_for`, `tr`, `search_summary`
- Produces: `search_packages` prints installed tag + optional green line

- [ ] **Step 1: Update `search_packages` in `src/commands_query.cpp`**

Replace the body of `search_packages` with:

```cpp
void search_packages(int argc, char** argv) {
    if (argc != 3) {
        throw std::runtime_error(tr("search requires exactly one keyword"));
    }
    const std::string keyword = argv[2];
    for (const RepoPackage& package : load_repo_packages()) {
        if (!package_matches_keyword(package, keyword)) {
            continue;
        }
        std::string line = package.id + "\t" + package.name + "\t" + search_summary(package.summary);
        if (metadata_exists(paths_for(package.id))) {
            line += " ";
            line += tr("[installed]");
            line = color_green(line);
        }
        std::cout << line << "\n";
    }
}
```

- [ ] **Step 2: Add po entries**

In `po/en.po`, insert near the other search-related strings (after `msgid "search requires exactly one keyword"` block):

```po
msgid "[installed]"
msgstr "[installed]"
```

In `po/zh.po`, same location:

```po
msgid "[installed]"
msgstr "[已安装]"
```

- [ ] **Step 3: Rebuild and run stage4 smoke**

Run:

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/stage4_smoke.sh
```

Expected: `stage4 smoke test passed`

- [ ] **Step 4: Spot-check related search smokes still pass**

Run:

```bash
bash /home/fsx/yai/tests/repo_smoke.sh
```

Expected: `repo smoke test passed` (existing `grep -q id` still works with optional tag suffix).

- [ ] **Step 5: Commit**

```bash
git add src/commands_query.cpp po/en.po po/zh.po
git commit -m "$(cat <<'EOF'
Mark installed packages in search with green tag.

EOF
)"
```

---

## Spec coverage (self-review)

| Spec requirement | Task |
|------------------|------|
| Trailing localized `[installed]` / `[已安装]` | Task 3 + Task 1 zh assertion |
| Whole-line green on TTY | Task 2 `color_green` + Task 3 wrap |
| Always show text tag; no ANSI when not TTY | Task 1 pipe assertion + Task 2 `isatty` |
| `NO_COLOR` disables ANSI | Task 1 + Task 2 `getenv("NO_COLOR")` |
| Three-column TSV; tag is summary suffix | Task 1 column assertions + Task 3 line build |
| yai-only install detection via metadata | Task 3 `metadata_exists(paths_for(...))` |
| Tag not part of keyword match | Unchanged match path in Task 3 |
| Out of scope: other commands / system packages / 4th column | Not implemented |

No placeholders. Helper signatures are consistent across Task 2 and Task 3.
