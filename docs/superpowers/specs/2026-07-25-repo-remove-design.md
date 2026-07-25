# Repo remove design

**Date:** 2026-07-25  
**Status:** Approved (approach: shared `resolve_configured_repo_name` + command handler)

## Goal

Add `yai repo remove <name-or-pattern>` so users can drop a configured package source, delete its local cache, and rebuild the merged repo index—without touching installed apps.

## Command surface

```
yai repo remove <name-or-pattern>
```

- Requires exactly one argument after `remove`.
- Supports shell-style `*` and `?` against configured repo **names** (not locations).
- Without wildcards: exact name match against `repos.conf` entries.
- Zero matches → error (non-zero exit).
- Multiple matches → error listing the matched names (same ambiguity style as installed-package / repo-package patterns).
- On success, print a localized line such as `Removed repo <name>` via `tr()` / `tr_format()`.

Update help (`print_usage`), README, and the stage-four “not yet” note in the development doc so `remove` is documented as supported.

## Shared resolver

Add a public helper (declared in `yai.hpp`, implemented in `repo.cpp`):

```cpp
std::string resolve_configured_repo_name(const std::string& pattern);
```

Behavior:

- Load entries via `load_repo_entries()`.
- Exact match when the pattern has no glob wildcards (`has_glob_wildcards`).
- Otherwise collect names matching `glob_match_case_insensitive(pattern, entry.name)`.
- 0 matches → throw (e.g. `repo pattern matched no configured repos: …` or reuse a clear existing phrasing).
- \>1 matches → throw with an ambiguous list (comma-separated names).
- 1 match → return that name.

`repo remove` must call this helper. Future name-targeted repo subcommands may reuse it; this change does not require rewriting `repo update` in the same patch unless trivial.

## Removal semantics

Given a resolved unique `name`:

1. Remove the matching `RepoEntry` from the in-memory list and `write_repo_entries`.
2. Delete `named_repo_index_path(name)` (`~/.local/share/yai/repos/<name>.json`) if it exists.
3. If remaining entries are non-empty: `rebuild_repo_index_from_cached_files(remaining)`.
4. If remaining entries are empty: write an empty schema-v1 `index.json` (same shape as the existing empty-index path used when `repo update` finds no repos).
5. Do **not** uninstall apps, rewrite per-app metadata, wrappers, or desktop files.

Dispatch: extend `repo_app` so `remove` is a recognized subcommand alongside `list`, `add`, and `update`. Error strings that list allowed subcommands must include `remove`.

## i18n

- All new user-visible strings go through `tr()` / `tr_format()`.
- Add matching msgids to `po/en.po` (msgstr = msgid) and `po/zh.po` (Simplified Chinese).
- Do not translate machine-oriented TSV columns from `repo list`.

## Tests

Extend `tests/repo_smoke.sh` (keep `YAI_LANG=en`):

- Add a named source, confirm it appears in `repo list` / search, then `repo remove` that name; list/cache/search no longer expose that source’s packages.
- Pattern success: a unique glob match removes the intended repo.
- Pattern failure: no match and ambiguous multi-match both fail.
- Optional: an app installed from a source remains installed after that source is removed.

## Out of scope

- Confirmation prompts / `--yes`
- `repo remove --all`
- Removing by location URL
- Schema or `repos.conf` format changes
- Uninstalling packages that originated from the removed source
- GUI/TUI

## Verification

- `make` succeeds
- `YAI_LANG=en` `tests/repo_smoke.sh` (and preferably the full smoke set) passes
- `YAI_LANG=zh ./yai help` shows the remove usage line in Chinese prose with English command tokens
