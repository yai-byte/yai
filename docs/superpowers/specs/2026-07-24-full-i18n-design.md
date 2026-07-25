# Full i18n coverage design

**Date:** 2026-07-24  
**Status:** Approved (approach A)

## Goal

Move all remaining user-visible English strings from C++ source into `po/en.po` and `po/zh.po`, using the existing `tr()` / `tr_format()` helpers.

## Approach

- msgid = English source text (unchanged convention)
- Wrap at print/throw sites with `tr()` or `tr_format()`
- No new i18n API
- `MirrorProvider::description` stores English msgid; callers print `tr(description)`
- Doctor OK/WARN lines use complete msgids (no hardcoded English prefixes)

## In scope

- Hardcoded `cout` / `cerr` warnings and status lines
- User-facing `std::runtime_error` messages
- `help` usage lines (keep English command/option tokens; translate prose and placeholders in zh)
- Built-in mirror provider descriptions

## Out of scope (intentionally untranslated)

- Package IDs, paths, config keys, metadata field names
- Tab-separated `search` / `list` column structure
- HTTP headers, process argv, protocol/constants
- Internal log-like strings that are never shown to users

## Verification

- `make` succeeds
- Existing smoke tests with `YAI_LANG=en` still pass
- Spot-check `YAI_LANG=zh help` and one previously hardcoded warning show Chinese
