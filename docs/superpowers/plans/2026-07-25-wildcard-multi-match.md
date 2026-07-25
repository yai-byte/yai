# Wildcard Multi-Match Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `*` / `?` patterns match multiple packages/repos and act on every match, with confirm + `--yes`/`-y` for side-effect multi-match and stop-on-first-failure.

**Architecture:** Change shared resolvers to return sorted match lists (no `ambiguous` errors). Add a shared multi-match confirm helper. Command handlers loop matches sequentially; `install`/`download` expand repo globs before the parallel batch path and force sequential processing when a pattern expands to N>1.

**Tech Stack:** C++17, existing `tr()`/`tr_format()`, shell smoke tests, Makefile, gettext `po/en.po` + `po/zh.po`.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-25-wildcard-multi-match-design.md` exactly
- msgid = English source text; sync `po/en.po` and `po/zh.po`
- Multi-match side effects: list targets, `[y/N]` confirm (default no), `--yes`/`-y` skips
- Stop on first failure; do not roll back successes
- Wildcard-expanded `install`/`download` are sequential; explicit multi-argv without multi-expanding globs keep today’s parallel jobs
- Do not change `upgrade --all` continue-on-error aggregation (only pattern multi-match uses stop-on-first)
- Commit after each task unless the user forbids commits

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | Plural resolver decls; confirm helper; `InstallOptions::yes`; thin wrappers |
| `src/commands.cpp` | `resolve_installed_package_ids`; shared `confirm_multi_match` |
| `src/repo.cpp` | `resolve_configured_repo_names`; `find_repo_packages` |
| `src/commands_query.cpp` | Multi `remove` / `info` |
| `src/commands_lifecycle.cpp` | Multi `repair` / `rollback`; pattern loops in install/download if needed |
| `src/commands_repo.cpp` | Multi `repo remove` + `--yes` |
| `src/commands_upgrade.cpp` | Pattern multi `upgrade` confirm + stop-on-first |
| `src/commands_update.cpp` | Multi `update` preview rows |
| `src/main.cpp` | Expand install/download globs; `--yes`; sequential when needed |
| `src/cli_download.cpp` | Help lines for `--yes` where added |
| `po/en.po`, `po/zh.po` | Remove ambiguous msgids; add confirm/cancel strings |
| `README.md` | Document multi-match + confirm |
| `tests/stage4_smoke.sh` | Flip ambiguous `info`; multi-match behavior |
| `tests/repo_smoke.sh` | Flip ambiguous `repo remove`; confirm / `--yes` |
| `tests/wildcard_multi_smoke.sh` | New focused smoke (create) |

---

### Task 1: Failing smokes for multi-match behavior

**Files:**
- Modify: `tests/stage4_smoke.sh`
- Modify: `tests/repo_smoke.sh`
- Create: `tests/wildcard_multi_smoke.sh`

**Interfaces:**
- Consumes: none yet (expects future CLI)
- Produces: failing assertions Tasks 2–7 must satisfy

- [ ] **Step 1: Flip stage4 ambiguous `info` expectation**

Replace the block that expects `info 'repo-d*'` to fail with ambiguous:

```bash
HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" info 'repo-d*' | tee "$TMP_HOME/info-multi.out" >/dev/null
grep -q "repo-demo" "$TMP_HOME/info-multi.out"
grep -q "repo-delta" "$TMP_HOME/info-multi.out"
```

Keep unique-pattern `info 'repo-dem*'` and `install 'repo-dem*'` as-is.

- [ ] **Step 2: Flip repo_smoke ambiguous `repo remove` to multi-remove with `--yes`**

Replace the `a*` ambiguous failure block with:

```bash
HOME="$TMP_HOME" "$ROOT/yai" repo add alpha "$TMP_HOME/remove-source.json"
HOME="$TMP_HOME" "$ROOT/yai" repo add abelian "$SOURCE_INDEX"
# decline confirm: no change
printf 'n\n' | HOME="$TMP_HOME" "$ROOT/yai" repo remove 'a*' >"$TMP_HOME/remove-cancel.out" 2>"$TMP_HOME/remove-cancel.err" || true
grep -q "alpha" <<<"$(HOME="$TMP_HOME" "$ROOT/yai" repo list)"
grep -q "abelian" <<<"$(HOME="$TMP_HOME" "$ROOT/yai" repo list)"
HOME="$TMP_HOME" "$ROOT/yai" repo remove 'a*' --yes | tee "$TMP_HOME/remove-multi.out" >/dev/null
grep -q "Removed repo alpha" "$TMP_HOME/remove-multi.out"
grep -q "Removed repo abelian" "$TMP_HOME/remove-multi.out"
if HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -Eq $'^(alpha|abelian)\t'; then
  echo "expected alpha and abelian removed" >&2
  exit 1
fi
```

Keep the no-match `drop*` and unique `alp*` cases (re-add `alpha` before unique glob if the multi-remove already deleted it—adjust ordering so unique `alp*` still runs against a fresh `alpha` add, or drop unique `alp*` if redundant after multi `--yes`).

- [ ] **Step 3: Add `tests/wildcard_multi_smoke.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
trap 'rm -rf "$TMP_HOME"' EXIT
export YAI_LANG=en
make -C "$ROOT" yai >/dev/null

# Minimal: two installed stub apps via metadata dirs for remove multi-match
for id in multi-a multi-b; do
  mkdir -p "$TMP_HOME/.local/share/yai/apps/$id"
  cat >"$TMP_HOME/.local/share/yai/apps/$id/metadata.json" <<JSON
{"id":"$id","name":"$id","install_mode":"extract","version":"1","source_kind":"unavailable"}
JSON
  : >"$TMP_HOME/.local/share/yai/apps/$id/current.AppImage"
  mkdir -p "$TMP_HOME/.local/bin" "$TMP_HOME/.local/share/applications"
  : >"$TMP_HOME/.local/bin/$id"
  : >"$TMP_HOME/.local/share/applications/yai-$id.desktop"
done

# cancel remove
printf 'n\n' | HOME="$TMP_HOME" "$ROOT/yai" remove 'multi-*' >"$TMP_HOME/rm-cancel.out" 2>"$TMP_HOME/rm-cancel.err" || true
test -d "$TMP_HOME/.local/share/yai/apps/multi-a"
test -d "$TMP_HOME/.local/share/yai/apps/multi-b"

# --yes removes both (sorted: multi-a then multi-b)
HOME="$TMP_HOME" "$ROOT/yai" remove 'multi-*' --yes | tee "$TMP_HOME/rm-yes.out" >/dev/null
grep -q "Removed multi-a" "$TMP_HOME/rm-yes.out"
grep -q "Removed multi-b" "$TMP_HOME/rm-yes.out"
test ! -d "$TMP_HOME/.local/share/yai/apps/multi-a"
test ! -d "$TMP_HOME/.local/share/yai/apps/multi-b"

# Mid-batch failure coverage for install globs is in Task 7 (pack-a then unavailable pack-b).

echo "wildcard multi smoke passed"
```

Make executable: `chmod +x tests/wildcard_multi_smoke.sh`.

- [ ] **Step 4: Run smokes; confirm they fail for the right reason**

```bash
bash tests/stage4_smoke.sh; bash tests/repo_smoke.sh; bash tests/wildcard_multi_smoke.sh
```

Expected: FAIL (info multi still ambiguous / repo remove still ambiguous / remove rejects `--yes` or ambiguous).

- [ ] **Step 5: Commit**

```bash
git add tests/stage4_smoke.sh tests/repo_smoke.sh tests/wildcard_multi_smoke.sh
git commit -m "$(cat <<'EOF'
Add failing smokes for wildcard multi-match behavior.

EOF
)"
```

---

### Task 2: List resolvers + shared confirm helper

**Files:**
- Modify: `src/yai.hpp`
- Modify: `src/commands.cpp`
- Modify: `src/repo.cpp`

**Interfaces:**
- Consumes: `has_glob_wildcards`, `glob_match_case_insensitive`, `load_repo_entries`, `load_repo_packages`, `tr`/`tr_format`
- Produces:
  - `std::vector<std::string> resolve_installed_package_ids(const std::string& pattern);`
  - `std::vector<std::string> resolve_configured_repo_names(const std::string& pattern);`
  - `std::vector<RepoPackage> find_repo_packages(const std::string& pattern);`
  - `bool confirm_multi_match(const std::string& prompt, const std::vector<std::string>& matches, bool yes);`
  - Keep `resolve_installed_package_id` / `resolve_configured_repo_name` / `find_repo_package` as thin wrappers that call plural APIs and, **only for temporary single-caller compatibility**, if `matches.size() != 1` after wildcard resolve throw the **old** ambiguous error until Task 3–7 migrate callers—**prefer migrating wrappers to:** return `matches.front()` when size==1, and when size>1 throw a clear internal error `multi-match pattern requires list resolver` **OR** delete singular functions once all callers moved in the same task set. This task must remove ambiguous throws from the plural APIs.

- [ ] **Step 1: Declare APIs in `yai.hpp`**

Replace singular decls with plural + confirm; keep singular only if still referenced:

```cpp
std::vector<std::string> resolve_installed_package_ids(const std::string& pattern);
std::vector<std::string> resolve_configured_repo_names(const std::string& pattern);
std::vector<RepoPackage> find_repo_packages(const std::string& pattern);
bool confirm_multi_match(const std::string& prompt, const std::vector<std::string>& matches, bool yes);
// Optional convenience while migrating:
std::string resolve_installed_package_id(const std::string& pattern); // returns single or throws zero-match / requires unique for non-migrated callers — DELETE once unused
```

- [ ] **Step 2: Implement `confirm_multi_match` in `commands.cpp`**

```cpp
bool confirm_multi_match(const std::string& prompt, const std::vector<std::string>& matches, bool yes) {
    for (const std::string& match : matches) {
        std::cerr << match << "\n";
    }
    if (yes) {
        return true;
    }
    std::cerr << prompt;
    std::string answer;
    if (!std::getline(std::cin, answer)) {
        std::cerr << "\n";
        return false;
    }
    answer = to_lower(trim(answer));
    return answer == "y" || answer == "yes";
}
```

Prompt strings (call-site msgids, English):

- `Remove {count} package(s)? [y/N] `
- `Repair {count} package(s)? [y/N] `
- `Roll back {count} package(s)? [y/N] `
- `Remove {count} repo(s)? [y/N] `
- `Upgrade {count} package(s)? [y/N] ` (reuse existing)
- `Install {count} package(s)? [y/N] `
- `Download {count} package(s)? [y/N] `

Cancel messages: `Remove cancelled\n`, `Repair cancelled\n`, `Rollback cancelled\n`, `Repo remove cancelled\n`, `Upgrade cancelled\n` (existing), `Install cancelled\n`, `Download cancelled\n`.

- [ ] **Step 3: Implement plural resolvers without ambiguous errors**

`resolve_installed_package_ids`: same scan as today’s `resolve_installed_package_id`, but on `matches.size() >= 1` return sorted matches; only empty throws `package pattern matched no installed packages: `.

`resolve_configured_repo_names`: same as today’s name resolver without the `matches.size() > 1` throw.

`find_repo_packages`:

```cpp
std::vector<RepoPackage> find_repo_packages(const std::string& id) {
    const std::vector<RepoPackage> packages = load_repo_packages();
    if (!has_glob_wildcards(id)) {
        for (const RepoPackage& package : packages) {
            if (package.id == id) {
                return {package};
            }
        }
        return {};
    }
    std::vector<RepoPackage> matches;
    for (const RepoPackage& package : packages) {
        if (glob_match_case_insensitive(id, package.id)) {
            matches.push_back(package);
        }
    }
    std::sort(matches.begin(), matches.end(),
              [](const RepoPackage& a, const RepoPackage& b) { return a.id < b.id; });
    if (matches.empty()) {
        throw std::runtime_error(tr("package pattern matched no repo packages: ") + id);
    }
    return matches;
}
```

Update `find_repo_package` to:

```cpp
std::optional<RepoPackage> find_repo_package(const std::string& id) {
    const std::vector<RepoPackage> matches = find_repo_packages(id);
    if (matches.empty()) {
        return std::nullopt;
    }
    if (matches.size() > 1) {
        // Temporary: callers that still assume uniqueness must migrate.
        // Prefer returning nullopt only for exact miss; for wildcards multi,
        // callers must use find_repo_packages. Until migrated, return first
        // match ONLY if you also migrate info in Task 5 same commit series —
        // do not silently pick first. Throw:
        throw std::runtime_error(tr("internal error: use find_repo_packages for multi-match"));
    }
    return matches.front();
}
```

Better for this task: migrate `find_repo_package` body to call `find_repo_packages` and keep exact empty→nullopt; **remove** the ambiguous branch entirely; if `matches.size() > 1`, still throw internal error **or** change `info` in the same commit (allowed to combine Task 2+5 if needed to keep tree building). Prefer completing plural APIs here and updating `info` immediately to loop so the tree compiles and stage4 can progress.

Minimal compile-safe approach in this task: change `info_package` to use `find_repo_packages` and print each (read-only, no confirm). That unblocks stage4 Step 1 without waiting for Task 5.

- [ ] **Step 4: Build**

```bash
make -C /home/fsx/yai yai
```

Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/yai.hpp src/commands.cpp src/repo.cpp src/commands_query.cpp
git commit -m "$(cat <<'EOF'
Return all glob matches from package and repo resolvers.

EOF
)"
```

---

### Task 3: Multi-match `remove` / `repair` / `rollback` + `--yes`

**Files:**
- Modify: `src/commands_query.cpp` (`remove_app`)
- Modify: `src/commands_lifecycle.cpp` (`repair_app`, `rollback_app`)
- Modify: `src/cli_download.cpp` (usage lines)
- Modify: `po/en.po`, `po/zh.po`

**Interfaces:**
- Consumes: `resolve_installed_package_ids`, `confirm_multi_match`
- Produces: CLI `remove|repair|rollback <pattern> [--yes|-y]`

- [ ] **Step 1: Parse optional `--yes`/`-y` for these commands**

Pattern: allow `argc` 3 or 4; if 4th arg is `--yes` or `-y`, set `yes=true`; otherwise error. If `argv[2]` is `--yes`, error (pattern required).

Extract core remove body:

```cpp
void remove_installed_id(const std::string& id) {
    const InstallPaths paths = paths_for(id);
    // ... existing remove_app body using id ...
}

void remove_app(int argc, char** argv) {
    bool yes = false;
    std::string pattern;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--yes" || arg == "-y") {
            yes = true;
        } else if (pattern.empty() && arg.rfind("--", 0) != 0) {
            pattern = arg;
        } else {
            throw std::runtime_error(tr("unknown remove option: ") + arg);
        }
    }
    if (pattern.empty()) {
        throw std::runtime_error(tr("remove requires exactly one package id"));
    }
    const std::vector<std::string> ids = resolve_installed_package_ids(pattern);
    if (ids.size() > 1) {
        const std::string prompt = tr_format(
            "Remove {count} package(s)? [y/N] ",
            {{"{count}", std::to_string(ids.size())}});
        if (!confirm_multi_match(prompt, ids, yes)) {
            std::cout << tr("Remove cancelled\n");
            return;
        }
    }
    for (const std::string& id : ids) {
        remove_installed_id(id);
    }
}
```

Mirror the same structure for `repair_app` and `rollback_app` (prompts/cancel strings per Task 2). Stop-on-first is natural: exceptions propagate out of the loop.

- [ ] **Step 2: Update usage strings in `cli_download.cpp`**

```
yai remove <id> [--yes]
yai repair <id> [--yes]
yai rollback <id> [--yes]
```

- [ ] **Step 3: Add po entries** for new msgids (en msgstr=msgid; zh translations). Remove unused ambiguous package msgid if no remaining references (`rg 'pattern is ambiguous' src`).

- [ ] **Step 4: Run**

```bash
bash tests/wildcard_multi_smoke.sh
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/commands_query.cpp src/commands_lifecycle.cpp src/cli_download.cpp po/en.po po/zh.po
git commit -m "$(cat <<'EOF'
Apply installed-id globs to all matches with confirm.

EOF
)"
```

---

### Task 4: Multi-match `repo remove` + `--yes`

**Files:**
- Modify: `src/commands_repo.cpp`
- Modify: `src/cli_download.cpp`
- Modify: `po/en.po`, `po/zh.po`
- Modify: `src/yai.hpp` if singular `resolve_configured_repo_name` removed

**Interfaces:**
- Consumes: `resolve_configured_repo_names`, `confirm_multi_match`
- Produces: `yai repo remove <pattern> [--yes|-y]`

- [ ] **Step 1: Rewrite `repo_remove_app`**

```cpp
void remove_one_repo(const std::string& name) {
    std::vector<RepoEntry> entries = load_repo_entries();
    std::vector<RepoEntry> remaining;
    for (const RepoEntry& entry : entries) {
        if (entry.name != name) {
            remaining.push_back(entry);
        }
    }
    if (remaining.empty()) {
        write_empty_repo_index();
    } else {
        rebuild_repo_index_from_cached_files(remaining);
    }
    write_repo_entries(remaining);
    remove_repo_cache_if_exists(named_repo_index_path(name));
    std::cout << tr("Removed repo ") << name << "\n";
}

void repo_remove_app(int argc, char** argv) {
    bool yes = false;
    std::string pattern;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--yes" || arg == "-y") {
            yes = true;
        } else if (pattern.empty() && arg.rfind("--", 0) != 0) {
            pattern = arg;
        } else {
            throw std::runtime_error(tr("unknown repo remove option: ") + arg);
        }
    }
    if (pattern.empty()) {
        throw std::runtime_error(tr("repo remove requires a name or pattern"));
    }
    const std::vector<std::string> names = resolve_configured_repo_names(pattern);
    if (names.size() > 1) {
        const std::string prompt = tr_format(
            "Remove {count} repo(s)? [y/N] ",
            {{"{count}", std::to_string(names.size())}});
        if (!confirm_multi_match(prompt, names, yes)) {
            std::cout << tr("Repo remove cancelled\n");
            return;
        }
    }
    for (const std::string& name : names) {
        remove_one_repo(name);
    }
}
```

Note: after removing the first of two, `load_repo_entries()` inside `remove_one_repo` must see updated config—`write_repo_entries` already persists each time. Rebuild uses remaining entries each iteration. OK.

- [ ] **Step 2: Help + po** (`repo remove <name-or-pattern> [--yes]`); delete `repo pattern is ambiguous` msgid if unused.

- [ ] **Step 3: Run**

```bash
bash tests/repo_smoke.sh
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/commands_repo.cpp src/cli_download.cpp src/repo.cpp src/yai.hpp po/en.po po/zh.po
git commit -m "$(cat <<'EOF'
Remove all repos matched by a glob with confirm.

EOF
)"
```

---

### Task 5: Multi-match read-only `info` + `update`

**Files:**
- Modify: `src/commands_query.cpp` (`info_package`) — if not done in Task 2
- Modify: `src/commands_update.cpp`

**Interfaces:**
- Consumes: `find_repo_packages`, `resolve_installed_package_ids`
- Produces: multi-row `info` / `update` preview output

- [ ] **Step 1: `info_package`**

```cpp
void info_package(int argc, char** argv) {
    if (argc != 3) {
        throw std::runtime_error(tr("info requires exactly one package id"));
    }
    const std::string id = argv[2];
    const std::vector<RepoPackage> packages = find_repo_packages(id);
    if (packages.empty()) {
        throw std::runtime_error(tr("package not found in repo index: ") + id);
    }
    for (std::size_t i = 0; i < packages.size(); ++i) {
        if (i > 0) {
            std::cout << "\n";
        }
        print_repo_package_info(packages[i]); // existing helper or inline body
    }
}
```

Refactor existing single-package print into a helper if not already.

- [ ] **Step 2: `update_app` single-pattern branch**

```cpp
if (argc == 3) {
    const std::vector<std::string> ids = resolve_installed_package_ids(argv[2]);
    for (const std::string& id : ids) {
        print_update_preview_row(build_update_preview(id));
    }
    return;
}
```

No confirm.

- [ ] **Step 3: Run**

```bash
bash tests/stage4_smoke.sh
```

Expected: PASS for info multi section (later install sections still OK). If stage4 still fails on later ambiguous remove `repo-*`, leave for Task 3 already fixing installed remove—stage4 end has `remove 'repo-*'` which becomes multi-match confirm: feed `--yes` or `printf 'y\n'`.

Update stage4 final remove line to:

```bash
HOME="$TMP_HOME" "$ROOT/yai" remove 'repo-*' --yes
```

Do that edit in this task if not already in Task 1.

- [ ] **Step 4: Commit**

```bash
git add src/commands_query.cpp src/commands_update.cpp tests/stage4_smoke.sh
git commit -m "$(cat <<'EOF'
Print all info and update rows for glob matches.

EOF
)"
```

---

### Task 6: Multi-match `upgrade <pattern>`

**Files:**
- Modify: `src/commands_upgrade.cpp`

**Interfaces:**
- Consumes: `resolve_installed_package_ids`, `confirm_multi_match`, existing `upgrade_installed_target`
- Produces: pattern upgrade confirms when N>1; stop-on-first

- [ ] **Step 1: Change `upgrade_app` non-`--all` path**

```cpp
void upgrade_app(int argc, char** argv) {
    const UpgradeCommandOptions command = parse_upgrade_command_options(argc, argv);
    if (command.all) {
        upgrade_all_app(command);
        return;
    }

    const std::vector<std::string> ids = resolve_installed_package_ids(command.options.target);
    if (ids.size() > 1) {
        const std::string prompt = yes_no_prompt_text(ids.size()); // existing Upgrade {count}...
        if (!confirm_multi_match(prompt, ids, command.yes)) {
            std::cout << tr("Upgrade cancelled\n");
            return;
        }
    }
    for (const std::string& id : ids) {
        InstallOptions options = command.options;
        options.target = id;
        upgrade_installed_target(options); // let exception stop the loop
    }
}
```

Update `load_update_context` to take a concrete id (already uses `resolve_installed_package_id(options.target)`—once target is a concrete id, singular wrapper or direct id is fine). Change `load_update_context` to use the id directly without re-resolving globs:

```cpp
const std::string id = options.target; // already concrete
// or resolve_installed_package_ids and require size==1
```

Prefer: `paths_for(sanitize)` only after callers pass concrete ids; keep `resolve_installed_package_ids` at the `upgrade_app` boundary only.

- [ ] **Step 2: Build and quick manual check** (optional smoke if fixtures exist). At minimum `make yai` succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/commands_upgrade.cpp
git commit -m "$(cat <<'EOF'
Upgrade every installed package matched by a glob.

EOF
)"
```

---

### Task 7: Multi-match `install` / `download` glob expansion

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/yai.hpp` (`InstallOptions::yes` if needed)
- Modify: `src/cli_download.cpp` (parse `--yes` for install/download; help)
- Modify: `tests/wildcard_multi_smoke.sh` or `tests/stage4_smoke.sh` / `tests/download_smoke.sh`
- Modify: `po/en.po`, `po/zh.po`

**Interfaces:**
- Consumes: `find_repo_packages`, `confirm_multi_match`, `has_glob_wildcards`
- Produces: glob argv targets expanded to concrete package ids; confirm + sequential stop-on-first when any pattern expands to N>1

- [ ] **Step 1: Parse `--yes`/`-y` in install/download option parsers and batch parser**

Add `bool yes = false;` to `InstallOptions`. In `parse_install_options` / `parse_download_options` / `main.cpp` batch loop, accept `--yes`/`-y`.

- [ ] **Step 2: Expand globs before dispatch in `main.cpp`**

After collecting `batch.targets`, before choosing jobs:

```cpp
bool expanded_multi = false;
std::vector<std::string> expanded;
for (const std::string& target : batch.targets) {
    if (!has_glob_wildcards(target)) {
        expanded.push_back(target);
        continue;
    }
    // Only repo-id globs expand here; URL/github/local never contain usable globs in practice.
    const std::vector<RepoPackage> packages = find_repo_packages(target);
    if (packages.size() > 1) {
        expanded_multi = true;
    }
    for (const RepoPackage& package : packages) {
        expanded.push_back(package.id);
    }
}
batch.targets = expanded;

if (expanded_multi) {
    const std::string prompt = tr_format(
        batch.command == "download"
            ? "Download {count} package(s)? [y/N] "
            : "Install {count} package(s)? [y/N] ",
        {{"{count}", std::to_string(batch.targets.size())}});
    // confirm_multi_match needs the list — print ids
    if (!confirm_multi_match(prompt, batch.targets, batch.yes)) {
        std::cout << (batch.command == "download" ? tr("Download cancelled\n") : tr("Install cancelled\n"));
        return 0;
    }
    batch.jobs = 1; // sequential
}
```

Wire `batch.yes` from argv. When `expanded_multi`, run workers with `jobs == 1` **and** stop-on-first: today’s batch collects all failures—change the sequential path so the first non-zero worker result aborts starting further tasks (for `jobs==1` loop, break after first failure and return non-zero).

If current parallel aggregator waits for all jobs, for `expanded_multi` use a simple synchronous for-loop instead of the thread pool:

```cpp
if (expanded_multi) {
    for (const std::string& target : batch.targets) {
        // call install/download for one target; on exception, print and return 1 immediately
    }
    return 0;
}
```

- [ ] **Step 3: Mid-fail smoke**

Extend `tests/wildcard_multi_smoke.sh` (or stage4) using two repo packages where the second is `unavailable` (like `repo-delta`): 

```bash
# with YAI_REPO_INDEX containing repo-demo (installable) and repo-delta (unavailable)
# printf 'y\n' or --yes:
if HOME=... YAI_REPO_INDEX=... YAI_GITHUB_API_BASE=... \
  "$ROOT/yai" install 'repo-d*' --yes; then
  echo "expected multi install to fail on unavailable package" >&2
  exit 1
fi
# repo-demo should be installed; repo-delta not
HOME=... "$ROOT/yai" list | grep -q repo-demo
if HOME=... "$ROOT/yai" list | grep -q repo-delta; then
  echo "repo-delta should not install" >&2
  exit 1
fi
```

Sorted order: `repo-delta` before `repo-demo` lexicographically—**adjust fixture ids** so the installable package sorts first (`repo-demo` < `repo-delta`? `repo-delta` < `repo-demo` because `l`<`m`). So first fails on delta and demo never installs—bad for mid-fail “earlier succeeded”.

Rename fixture ids in the new smoke only: `pack-a` (github ok) and `pack-b` (unavailable) so `pack-a` succeeds then `pack-b` fails.

- [ ] **Step 4: Run**

```bash
bash tests/wildcard_multi_smoke.sh
bash tests/stage4_smoke.sh
bash tests/download_smoke.sh
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/cli_download.cpp src/yai.hpp tests/wildcard_multi_smoke.sh po/en.po po/zh.po
git commit -m "$(cat <<'EOF'
Expand install and download globs across all matches.

EOF
)"
```

---

### Task 8: README + help polish + final verification

**Files:**
- Modify: `README.md`
- Modify: `src/cli_download.cpp` (any remaining usage notes)
- Modify: `po/en.po`, `po/zh.po` (help msgstr sync)

**Interfaces:**
- Consumes: completed CLI behavior
- Produces: docs matching spec

- [ ] **Step 1: Replace README single-match paragraph**

```markdown
Package, repo-name, and installed-id arguments accept shell-style `*` and `?`
patterns. Quote patterns such as `'obs*'` so the shell does not expand them
before yai receives the argument. When a pattern matches multiple targets,
yai acts on every match in sorted order. Side-effect commands list the matches
and ask for confirmation; pass `--yes` or `-y` to skip the prompt. Matching
zero targets still fails. Wildcard-expanded `install`/`download` batches run
sequentially and stop on the first failure.
```

Update any other README lines that say “unique pattern” / “single-match” for remove/repo (around the repo remove section).

- [ ] **Step 2: Full smoke suite used by the project**

```bash
make -C /home/fsx/yai yai
bash tests/stage4_smoke.sh
bash tests/repo_smoke.sh
bash tests/wildcard_multi_smoke.sh
bash tests/download_smoke.sh
# plus any other smokes Makefile/check runs
```

Expected: all PASS.

- [ ] **Step 3: Confirm no ambiguous msgids remain**

```bash
rg "pattern is ambiguous" src po tests README.md
```

Expected: no matches (except historical docs under `docs/superpowers/specs/2026-07-25-repo-remove-design.md` which may stay as historical—do not rewrite old specs unless required).

- [ ] **Step 4: Commit**

```bash
git add README.md src/cli_download.cpp po/en.po po/zh.po
git commit -m "$(cat <<'EOF'
Document wildcard multi-match confirmation behavior.

EOF
)"
```

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| List-returning resolvers; no ambiguous | 2 |
| Sorted deterministic order | 2 |
| Zero-match errors preserved | 2 |
| Confirm N>1 side effects + `--yes` | 3,4,6,7 |
| `remove`/`repair`/`rollback`/`repo remove` | 3,4 |
| `install`/`download` confirm + sequential | 7 |
| `info`/`update` multi no confirm | 5 |
| `upgrade` pattern multi | 6 |
| Stop-on-first; keep successes | 3,4,6,7 |
| Explicit multi-argv parallel unchanged when no multi-expanding glob | 7 |
| README + help + po | 3–4, 8 |
| Smokes flip ambiguous + cancel/`--yes`/mid-fail | 1,7,8 |

## Self-review notes

- No TBD placeholders left in steps; mid-fail concrete fixture uses `pack-a` then unavailable `pack-b`.
- Singular resolver removal must leave the tree compiling after Task 2/5 (`info` migrated).
- `upgrade --all` failure aggregation intentionally unchanged (Global Constraints).
