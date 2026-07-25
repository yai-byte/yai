# Wildcard multi-match design

**Date:** 2026-07-25  
**Status:** Approved (approach: list-returning resolvers + sequential command loops)

## Goal

Allow shell-style `*` / `?` patterns to match **multiple** targets and act on every match. Remove the current “exactly one match or fail as ambiguous” restriction across all commands that already accept wildcards.

## Requirements (agreed)

1. Multi-match → operate on **all** matches (sorted, deterministic order).
2. Scope → **every** command that already supports wildcards (side-effect and read-only).
3. Failures → **stop on first failure**; keep already-successful work; non-zero exit; do not roll back successes.
4. Confirmation → when a pattern matches **more than one** target and the command has side effects (including additive `install` / `download`), list matches and prompt `[y/N]` before acting.
5. `--yes` / `-y` skips that confirmation (same semantics as `upgrade --all`).

## Architecture: resolvers return lists

Replace single-value “unique match” resolvers with list-returning APIs (rename or add plural forms; update all call sites):

| Current | New |
|---------|-----|
| `resolve_installed_package_id` → `std::string` | `resolve_installed_package_ids` → `std::vector<std::string>` |
| `find_repo_package` (wildcard path throws if >1) | Prefer `find_repo_packages` → `std::vector<RepoPackage>` at call sites that need multi-match; exact-id miss stays empty vector (callers keep today’s “not found” handling). |
| `resolve_configured_repo_name` → `std::string` | `resolve_configured_repo_names` → `std::vector<std::string>` |

Single-value helpers may be removed or thin-wrapped around the plural APIs if that simplifies call sites; behavior must match the rules below.

### Matching rules

- **No wildcards:** keep today’s exact-match behavior (sanitize/validate as today). Success → one-element vector. Exact miss: installed/repo-name resolvers still throw the same errors as today; repo-package lookup returns an empty vector (same as today’s `nullopt` path).
- **With wildcards:** collect all matches with existing `glob_match_case_insensitive`; dedupe; sort lexicographically by id/name. **Do not** throw `… pattern is ambiguous …`.
- **Zero wildcard matches:** keep existing zero-match error strings (`package pattern matched no …`, `repo pattern matched no …`, etc.).

Exact non-pattern ids that resolve to a single target continue to run **without** a confirmation prompt.

## Command behavior

Shared execution model for pattern-capable commands:

1. Resolve to a sorted match list.
2. If side-effect command and `matches.size() > 1` and not `--yes`/`-y`: print the match list, prompt with `[y/N]` (default no). Decline → cancel with no mutations (exit 0 after a cancelled message, matching `upgrade --all` cancel style).
3. Process matches **sequentially** in list order.
4. On first exception/failure: stop; leave prior successes in place; surface which target failed; non-zero exit.

### Confirmation required (size > 1)

Any side-effect command whose target list comes from resolving one pattern to N>1 matches, including:

- `remove`
- `repair`
- `repo remove`
- Wildcard-driven multi-target `upgrade` (align with `upgrade --all` confirm UX)
- `install` and `download` when a **pattern argument** expands to multiple packages

### No confirmation

- Read-only multi-match: `info`, `update` preview (print/preview each match in order)
- Exact id / unique pattern (size == 1)
- Explicit multi-target `install` / `download` argument lists that are already separate argv targets (unchanged parallel path; not introduced by this feature’s confirm gate)

### Flags

- Add optional `--yes` / `-y` to `remove`, `repair`, and `repo remove`.
- Wire `--yes` / `-y` through `install` / `download` for pattern-expansion confirmation when not already present on those commands.
- Reuse existing `--yes` / `-y` on `upgrade`.

### Parallelism note

Today `install` / `download` run **explicit** multi-target argv lists in parallel (job pool). Wildcard **expansion** of one pattern into N packages uses the **sequential** stop-on-first path above, not the parallel worker pool. Explicit multi-target argv behavior stays as today.

## Error handling and i18n

- Remove user-facing `package pattern is ambiguous: …` and `repo pattern is ambiguous: …` msgids from code, `po/en.po`, and `po/zh.po`.
- Add confirmation / cancel / per-target failure strings via `tr()` / `tr_format()` with en+zh catalog entries.
- Prefer reusing upgrade-style prompt wording patterns where reasonable (count + list + `[y/N]`).

## Documentation

Update README (and help if it restates the single-match rule):

- Patterns may match multiple packages/repos.
- Side-effect commands list matches and confirm when N>1; `--yes`/`-y` skips confirm.
- Zero matches still fail; ambiguous errors are gone.

## Tests

Update / extend smokes (`YAI_LANG=en`) that currently expect ambiguous failure (e.g. `tests/repo_smoke.sh`, `tests/stage4_smoke.sh`):

- Multi-match `remove` / `repo remove` / pattern `install`: decline confirm → no change; `--yes` → all applied (or until first failure).
- Mid-batch failure: earlier targets succeed, later ones not attempted.
- Single match and exact id: unchanged, no confirm.
- `info` with multi-match prints all matched entries.
- Zero-match still fails.

## Out of scope

- Changing glob syntax beyond existing `*` / `?`
- Rolling back successful operations after a later failure
- Making wildcard-expanded install/download use the parallel job pool
- `repo remove --all` or other new bulk flags beyond pattern + `--yes`
- GUI/TUI

## Approach rejected

- Duplicating per-command glob loops without shared list resolvers (inconsistent confirm/errors)
- Expanding patterns into the existing parallel multi-target install path (conflicts with stop-on-first-failure)
)
