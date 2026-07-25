# Download id naming and local install repo recovery

**Date:** 2026-07-25  
**Status:** Approved  
**Goal:** Make `download` → local `install` produce the same stable package id as installing a repo package directly, and derive URL/local default ids from the app name only (no version or architecture tokens).

## Problem

- `yai install kdenlive` resolves via the repo index and uses `package.id` → `kdenlive`.
- `yai download kdenlive` saves the upstream asset name (e.g. `kdenlive-26.04.3-x86_64.AppImage`).
- `yai install ./kdenlive-26.04.3-x86_64.AppImage` derives id from the filename → `kdenlive-26.04.3-x86_64`.

Direct URL installs have the same filename-derived id problem (version and arch stay in the id).

## Scope

**In scope**

- Change `download` output basename to `{source.id}.AppImage`
- Add shared filename → base-name stripping (arch + version-like tokens)
- Use that stripping for default id/name on direct URL sources and on local-path sources when repo recovery does not apply
- When installing a local AppImage without explicit `--id`, try to recover a unique matching repo package id/name
- Update README / Chinese developer docs and smoke tests

**Out of scope**

- Promoting local installs to `source_kind` other than `local_path`
- Writing sidecar metadata next to downloaded files
- Changing how repo package ids themselves are assigned in the index
- Making `local_path` installs upgradable via GitHub/repo metadata

## Behavior

### 1. Download output name

`download_output_name` always returns `{source.id}.AppImage` (after existing empty-id fallback to `download.AppImage` if id were empty). Upstream asset names remain available on `ResolvedSource.github_asset` / URL basename for resolution and (for install) metadata; they are no longer used as the on-disk download filename.

Collision policy unchanged: refuse if the target path already exists.

### 2. Base name from AppImage filename

Introduce shared helpers `base_name_from_appimage_filename` and (thin wrappers as needed) used whenever default id/name are derived from a filename or URL basename:

1. Strip `.AppImage` via existing `strip_appimage_suffix`
2. Split remaining stem on `-` and `_`
3. From the right, drop tokens that are:
   - architecture: token lowercased/normalized matches any alias or asset needle from `arch_alias_rules()` (e.g. `x86_64`, `amd64`, `aarch64`, `arm64`)
   - version-like: optional leading `v`/`V`, then only digits and dots, with at least one digit (e.g. `26.04.3`, `v1.2.3`, `1.0`, `20240101`)
4. Rejoin remaining tokens with `-` (preserve original token spelling for display name)
5. Default **id** = `sanitize_id(base)`; default **display name** = rejoined base; if base empty after stripping, fall back to full-stem `strip_appimage_suffix` then existing sanitize / name behavior

Explicit `--id` / `--name` always win.

Apply this helper for:

- `resolve_url_install_source` / `fill_install_defaults_for_direct_target` (direct URL)
- `resolve_local_install_source` when repo recovery does not set id/name

Repo package installs and `owner/repo` GitHub installs keep their existing id rules (`package.id` / repo name); they already avoid versioned asset names as ids. After resolution, `download` still names the file from `source.id`.

### 3. Local install: recover repo identity

In `resolve_local_install_source`, when `--id` was not explicit:

1. Let `stem` = `sanitize_id(strip_appimage_suffix(filename))` (full stem, not yet arch/version-stripped — needed so prefix matches like `kdenlive-26.04.3-x86_64` still find `kdenlive`)
2. Scan loaded repo packages:
   - exact: `stem == package.id`
   - prefix: `stem` starts with `package.id + "-"`
3. If multiple prefix/exact candidates: prefer the **longest** `package.id`; if still tied, treat as no match
4. On unique match: set `id` / `name` from the package (unless `--name` was explicit); keep `source_kind = local_path` and keep staging the local file
5. On no match: derive id/name via the base-name helper in §2

`--id` explicit → no repo id override. Repo recovery must not re-download or change `source_kind`.

### 4. Expected outcomes

| Command | Result |
|---------|--------|
| `yai install kdenlive` | id `kdenlive` |
| `yai download kdenlive` | `./kdenlive.AppImage` |
| `yai install ./kdenlive.AppImage` | id `kdenlive` |
| `yai install ./kdenlive-26.04.3-x86_64.AppImage` | id `kdenlive` (repo prefix or strip) |
| `yai install https://…/Foo-1.2-x86_64.AppImage` | id `foo` |
| `yai download https://…/Foo-1.2-x86_64.AppImage` | `./foo.AppImage` |

## Docs and tests

- README and `AppImage包管理器开发文档.md`: document download naming as resolved package id, not upstream asset name; note URL/local default id stripping
- Update `tests/download_smoke.sh` expectations for on-disk names (`{id}.AppImage`)
- Add coverage for: repo `download` then local `install` id equals direct install; URL id strips version/arch; local path recovers repo id via prefix when possible

## Non-goals / explicit non-changes

- Download still does not chmod, probe, write metadata, wrappers, or desktop entries
- Install metadata for local files remains `source_kind=local_path` with absolute local paths
- Parallel download / `--jobs` behavior unchanged aside from output filenames
