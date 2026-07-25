# Search installed marker design

**Date:** 2026-07-25  
**Status:** Approved (approach: trailing localized tag + green whole-line color on TTY)

## Goal

When `yai search` lists packages, visually distinguish apps already installed by yai: green whole-line coloring in interactive terminals, plus a localized `[installed]` / `[已安装]` text tag that remains in non-TTY output.

## Behavior

`yai search <keyword>` keeps the existing three-column TSV shape:

```text
id\tname\tsummary
```

For each match that is installed via yai:

1. Append a space and a localized tag after the truncated summary: English `[installed]`, Chinese `[已安装]` (via `tr()`).
2. When stdout is a TTY **and** `NO_COLOR` is unset, wrap the **entire line** in green ANSI (and reset).
3. When stdout is not a TTY, or `NO_COLOR` is set: print the tag text with **no** ANSI escapes.

Uninstalled matches are unchanged: same columns, no tag, no color.

Example (TTY, Chinese locale):

```text
foo	Foo App	short summary
bar	Bar App	short summary [已安装]
```

(the second line is green end-to-end)

## Installed detection

“Installed” means yai manages the AppImage locally—not flatpak/rpm/system packages.

- Prefer building an `unordered_set` of installed ids once before the search loop (reuse `installed_package_ids()`, or equivalent enumeration over `~/.local/share/yai/apps/*/metadata.{json,conf}` via `paths_for` + `metadata_exists`).
- Per match: O(1) set lookup; do not re-scan the filesystem for every package.

The tag string is display-only and must **not** participate in keyword matching (matching remains id / name / summary only).

## Implementation

Primary change site: `search_packages` in `src/commands_query.cpp`.

Add a small stdout color helper (new tiny unit or next to existing terminal helpers such as `download_progress.cpp`):

- `stdout_color_enabled()` — true iff `isatty(STDOUT_FILENO)` and `NO_COLOR` is unset/empty.
- Wrap helpers that return green + reset around a string when color is enabled, otherwise return the string unchanged.

Do not introduce a project-wide color theme or recolor other commands in this change.

### Output construction

For an installed match:

1. Build the plain line: `id + '\t' + name + '\t' + search_summary(summary) + ' ' + tr("[installed]")`.
2. If color enabled, print green(line); else print line.
3. Always end with `\n`.

First column remains the package id so existing `grep`/field-1 scripts keep working. The tag is a suffix on the summary field, not a fourth TSV column and not a line prefix.

## i18n

- msgid: `[installed]`
- `po/en.po`: msgstr = msgid
- `po/zh.po`: msgstr = `[已安装]`
- All user-visible tag text goes through `tr()`.

Do not translate the TSV column structure or package id/name fields.

## Tests

Extend an existing search smoke test (e.g. `tests/stage4_smoke.sh` or a focused search smoke), with `YAI_LANG=en` where assertions need English tags:

- After installing a fixture package, `yai search` for that package includes `[installed]` after the summary; tabs still separate the three columns.
- A non-installed match has no tag.
- Piped output (`| cat` or redirect) contains the tag and contains **no** ANSI CSI sequences for that run.
- With `NO_COLOR=1` on a TTY-like path (or the helper’s contract covered by unit/smoke), output has the tag without color codes.
- Empty install set: search output matches pre-change behavior for all hits.

## Out of scope

- Coloring or tagging `list` / `info` / other commands
- Detecting flatpak, rpm, dnf, or other system packages with the same name
- Adding a fourth status column or changing the stable three-column contract beyond the summary suffix
- Full CLI color theming or configurable color palettes
- GUI/TUI

## Compatibility notes

- Scripts that take field 1 (id) or field 2 (name) are unaffected.
- Scripts that treat field 3 as “summary only” may see a trailing ` [installed]` on installed rows; this is intentional per the approved “always show text tag” choice.
- ANSI is never emitted when not a TTY or when `NO_COLOR` is set.
