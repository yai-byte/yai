# Repo Remove Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `yai repo remove <name-or-pattern>` that drops a configured source, deletes its cache JSON, rebuilds the merged index, and leaves installed apps alone—backed by shared `resolve_configured_repo_name`.

**Architecture:** Implement name resolution in `repo.cpp` (declared in `yai.hpp`). Wire `repo_remove_app` in `commands_repo.cpp` next to add/update, reusing the existing anonymous `write_empty_repo_index()` for the last-source case. Drive behavior with extensions to `tests/repo_smoke.sh`.

**Tech Stack:** C++17, existing `tr()`/`tr_format()`, shell smoke tests, Makefile.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-25-repo-remove-design.md` exactly
- msgid = English source text; sync `po/en.po` and `po/zh.po`
- Do not uninstall apps or rewrite per-app metadata on remove
- Do not add confirmation / `--yes` / `remove --all`
- Prefer mechanical, pattern-matching existing `repo add` / `resolve_installed_package_id` style
- Commit after each task unless the user forbids commits

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | Declare `resolve_configured_repo_name`, `repo_remove_app` |
| `src/repo.cpp` | Implement `resolve_configured_repo_name` |
| `src/commands_repo.cpp` | `repo_remove_app`, dispatch, subcommand error strings |
| `src/cli_download.cpp` | Help usage line for `repo remove` |
| `po/en.po`, `po/zh.po` | New msgids |
| `README.md`, `AppImage包管理器开发文档.md` | Document remove; clear “暂不做” for delete |
| `tests/repo_smoke.sh` | Exact remove, glob, failures, installed retention |

---

### Task 1: Failing smoke for exact remove + retention

**Files:**
- Modify: `tests/repo_smoke.sh`

**Interfaces:**
- Consumes: none yet (tests expect future CLI)
- Produces: failing assertions that Task 2–3 must satisfy

- [ ] **Step 1: Extend `tests/repo_smoke.sh` before the final success echo**

After the existing `repo update missing` failure check (before `echo "repo smoke test passed"`), append:

```bash
# --- repo remove ---
cat > "$TMP_HOME/remove-source.json" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-25T00:00:00Z",
  "packages": [
    {
      "id": "remove-me",
      "name": "Remove Me",
      "summary": "Source for remove smoke",
      "homepage": "https://example.com/remove-me",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "repo-managed",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" "$ROOT/yai" repo add keepme "$SOURCE_INDEX"
HOME="$TMP_HOME" "$ROOT/yai" repo add dropme "$TMP_HOME/remove-source.json"
HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install remove-me
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/remove-me" | grep -q "repo managed app"

HOME="$TMP_HOME" "$ROOT/yai" repo remove dropme | grep -q "Removed repo dropme"
if HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q $'dropme\t'; then
  echo "expected dropme to be gone from repo list" >&2
  exit 1
fi
test ! -e "$TMP_HOME/.local/share/yai/repos/dropme.json"
if HOME="$TMP_HOME" "$ROOT/yai" search "Remove Me" | grep -q "remove-me"; then
  echo "expected remove-me package to leave merged index after source remove" >&2
  exit 1
fi
HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q $'keepme\t'
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/remove-me" | grep -q "repo managed app"
HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "remove-me"

if HOME="$TMP_HOME" "$ROOT/yai" repo remove nosuch 2>"$TMP_HOME/remove-missing.err"; then
  echo "expected missing repo remove to fail" >&2
  exit 1
fi
grep -q "repo not configured: nosuch" "$TMP_HOME/remove-missing.err"

HOME="$TMP_HOME" "$ROOT/yai" repo add alpha "$TMP_HOME/remove-source.json"
HOME="$TMP_HOME" "$ROOT/yai" repo add abelian "$SOURCE_INDEX"
if HOME="$TMP_HOME" "$ROOT/yai" repo remove 'a*' 2>"$TMP_HOME/remove-ambiguous.err"; then
  echo "expected ambiguous repo remove to fail" >&2
  exit 1
fi
grep -q "repo pattern is ambiguous:" "$TMP_HOME/remove-ambiguous.err"

# dropme already removed; pattern should fail with no match
if HOME="$TMP_HOME" "$ROOT/yai" repo remove 'drop*' 2>"$TMP_HOME/remove-nomatch.err"; then
  echo "expected no-match repo remove to fail" >&2
  exit 1
fi
grep -q "repo pattern matched no configured repos:" "$TMP_HOME/remove-nomatch.err"

HOME="$TMP_HOME" "$ROOT/yai" repo remove 'alp*' | grep -q "Removed repo alpha"
if HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q $'alpha\t'; then
  echo "expected alpha to be removed via glob" >&2
  exit 1
fi
```

- [ ] **Step 2: Run smoke and confirm failure**

```bash
cd /home/fsx/yai && YAI_LANG=en bash tests/repo_smoke.sh
```

Expected: FAIL (unknown subcommand `remove` or similar).

- [ ] **Step 3: Commit the failing test**

```bash
git add tests/repo_smoke.sh
git commit -m "$(cat <<'EOF'
Add failing repo remove smoke coverage before implementation.

EOF
)"
```

---

### Task 2: `resolve_configured_repo_name` + `repo remove` command

**Files:**
- Modify: `src/yai.hpp` (declarations near other repo helpers / `repo_add_app`)
- Modify: `src/repo.cpp` (implement resolver near `find_repo_package` / `package_match_list`)
- Modify: `src/commands_repo.cpp` (`repo_remove_app`, dispatch, error strings)

**Interfaces:**
- Consumes: `load_repo_entries`, `validate_repo_name`, `has_glob_wildcards`, `glob_match_case_insensitive`, `write_repo_entries`, `named_repo_index_path`, `rebuild_repo_index_from_cached_files`, `remove_if_exists`, anonymous `write_empty_repo_index`
- Produces:
  - `std::string resolve_configured_repo_name(const std::string& pattern);`
  - `void repo_remove_app(int argc, char** argv);`

- [ ] **Step 1: Declare in `src/yai.hpp`**

Near repo command declarations:

```cpp
std::string resolve_configured_repo_name(const std::string& pattern);
void repo_remove_app(int argc, char** argv);
```

(Keep `repo_add_app` / `repo_update_app` / `repo_app` nearby; insert `repo_remove_app` next to them.)

- [ ] **Step 2: Implement resolver in `src/repo.cpp`**

```cpp
std::string resolve_configured_repo_name(const std::string& pattern) {
    const std::vector<RepoEntry> entries = load_repo_entries();
    if (!has_glob_wildcards(pattern)) {
        const std::string name = validate_repo_name(pattern);
        for (const RepoEntry& entry : entries) {
            if (entry.name == name) {
                return name;
            }
        }
        throw std::runtime_error(tr("repo not configured: ") + name);
    }

    std::vector<std::string> matches;
    for (const RepoEntry& entry : entries) {
        if (glob_match_case_insensitive(pattern, entry.name)) {
            matches.push_back(entry.name);
        }
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());

    if (matches.empty()) {
        throw std::runtime_error(tr("repo pattern matched no configured repos: ") + pattern);
    }
    if (matches.size() > 1) {
        std::string listed;
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (i > 0) {
                listed += ", ";
            }
            listed += matches[i];
        }
        throw std::runtime_error(tr_format(
            "repo pattern is ambiguous: {pattern} (matches: {matches})",
            {{"{pattern}", pattern}, {"{matches}", listed}}));
    }
    return matches.front();
}
```

Ensure `<algorithm>` is already available via `yai.hpp` / includes used by `repo.cpp`.

- [ ] **Step 3: Implement `repo_remove_app` and wire dispatch in `src/commands_repo.cpp`**

```cpp
void repo_remove_app(int argc, char** argv) {
    if (argc != 4) {
        throw std::runtime_error(tr("repo remove requires a name or pattern"));
    }

    const std::string name = resolve_configured_repo_name(argv[3]);
    std::vector<RepoEntry> entries = load_repo_entries();
    std::vector<RepoEntry> remaining;
    remaining.reserve(entries.size());
    for (const RepoEntry& entry : entries) {
        if (entry.name != name) {
            remaining.push_back(entry);
        }
    }

    write_repo_entries(remaining);
    remove_if_exists(named_repo_index_path(name));
    if (remaining.empty()) {
        write_empty_repo_index();
    } else {
        rebuild_repo_index_from_cached_files(remaining);
    }
    std::cout << tr("Removed repo ") << name << "\n";
}
```

Update `repo_app`:

```cpp
void repo_app(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(tr("repo requires a subcommand: list, add, update, or remove"));
    }

    const std::string subcommand = argv[2];
    if (subcommand == "list") {
        repo_list_app(argc);
    } else if (subcommand == "add") {
        repo_add_app(argc, argv);
    } else if (subcommand == "update") {
        repo_update_app(argc, argv);
    } else if (subcommand == "remove") {
        repo_remove_app(argc, argv);
    } else {
        throw std::runtime_error(tr("unknown repo subcommand: ") + subcommand);
    }
}
```

- [ ] **Step 4: Build**

```bash
cd /home/fsx/yai && make clean && make
```

Expected: exit 0.

- [ ] **Step 5: Run repo smoke**

```bash
YAI_LANG=en bash tests/repo_smoke.sh
```

Expected: `repo smoke test passed`

If po catalogs are missing msgids, English fallback still returns msgid (current loader behavior)—but Task 3 must still add them. If anything throws on missing translation infrastructure, add the po entries in this task before re-running.

- [ ] **Step 6: Commit**

```bash
git add src/yai.hpp src/repo.cpp src/commands_repo.cpp
git commit -m "$(cat <<'EOF'
Add repo remove with shared configured-repo name resolver.

EOF
)"
```

---

### Task 3: Help, docs, i18n catalogs

**Files:**
- Modify: `src/cli_download.cpp` (`print_usage`)
- Modify: `po/en.po`, `po/zh.po`
- Modify: `README.md`
- Modify: `AppImage包管理器开发文档.md`

**Interfaces:**
- Consumes: Task 2 command surface
- Produces: documented + translated usage

- [ ] **Step 1: Add usage line in `print_usage`**

After the `repo update` line:

```cpp
<< tr("  yai repo remove <name-or-pattern>\n")
```

- [ ] **Step 2: Sync po files**

Add these msgids to both catalogs (en: msgstr = msgid; zh: Simplified Chinese). Insert near other repo strings:

| msgid | zh msgstr |
|-------|-----------|
| `  yai repo remove <name-or-pattern>\n` | `  yai repo remove <名称-或-模式>\n` |
| `repo requires a subcommand: list, add, update, or remove` | `repo 需要子命令：list、add、update 或 remove` |
| `repo remove requires a name or pattern` | `repo remove 需要名称或模式` |
| `Removed repo ` | `已移除包源 ` |
| `repo pattern matched no configured repos: ` | `包源模式未匹配到已配置包源：` |
| `repo pattern is ambiguous: {pattern} (matches: {matches})` | `包源模式不唯一：{pattern}（匹配：{matches}）` |

Update the existing msgid `repo requires a subcommand: list, add, or update` to the new string in **both** po files and in source (Task 2 already changed source)—remove the obsolete old msgid entry so catalogs stay consistent.

- [ ] **Step 3: Update README Commands section**

Add under repo commands:

```
./yai repo remove <name-or-pattern>
```

Note briefly that patterns follow the same single-match `*`/`?` rules as other side-effect commands.

- [ ] **Step 4: Update development doc**

In stage four:

- Add bullet that `yai repo remove <name>` / pattern removes config + cache and rebuilds `index.json` without uninstalling apps.
- Delete or rewrite the “删除包源放到后续阶段” line under “阶段四暂不做”.

- [ ] **Step 5: Verify**

```bash
cd /home/fsx/yai && make
YAI_LANG=en bash tests/repo_smoke.sh
YAI_LANG=zh ./yai help | grep -q "repo remove"
YAI_LANG=en ./yai help | grep -q "yai repo remove"
```

Expected: all succeed; zh help shows Chinese prose for the remove line.

- [ ] **Step 6: Commit**

```bash
git add src/cli_download.cpp po/en.po po/zh.po README.md "AppImage包管理器开发文档.md"
git commit -m "$(cat <<'EOF'
Document and translate repo remove in help, README, and catalogs.

EOF
)"
```

---

### Task 4: Full regression

**Files:** none (verification only)

- [ ] **Step 1: Full smoke**

```bash
cd /home/fsx/yai && make && export YAI_LANG=en
for t in tests/*_smoke.sh; do bash "$t" || exit 1; done
```

Expected: every script prints its `* smoke test passed` line.

- [ ] **Step 2: No further commit unless Task 4 fixed bugs**

If a fix was required, commit it with a focused message, then re-run Step 1.

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| `repo remove <name-or-pattern>` | 2 |
| Shared `resolve_configured_repo_name` | 2 |
| Exact → `repo not configured` | 1, 2 |
| Glob 0 / ambiguous / unique | 1, 2 |
| Delete conf entry + cache + rebuild / empty index | 2 |
| Leave installed apps | 1, 2 |
| Help / README / doc / po | 3 |
| Smoke + make verification | 1, 2, 3, 4 |
