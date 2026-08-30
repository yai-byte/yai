# yai — AppImage Package Manager

[![Smoke tests](https://img.shields.io/badge/tests-22%20%2F%2022-brightgreen)](#testing)
[![Language](https://img.shields.io/badge/C%2B%2B-17-blue)](#building)
[![Platform](https://img.shields.io/badge/Linux-64bit%20%7C%2032bit%20%7C%20ARM%20%7C%20RISC--V%20%7C%20LoongArch-blue)](#supported-architectures)
[![Repo](https://img.shields.io/badge/Repo-GitHub-yai--byte%2Fyai--repo-blueviolet)](https://github.com/yai-byte/yai-repo)
[![Mirror](https://img.shields.io/badge/Mirror-Gitee--no11no16%2Fyai--repo-c71d23)](https://gitee.com/no11no16/yai-repo)

**yai** is a fast, dependency-light, single-binary **AppImage package manager** written in standard C++17.
It installs, updates, upgrades, rolls back, and repairs Linux AppImage applications, resolving their
download URLs from GitHub releases, custom repository indexes, AppImageHub catalog feeds, and even
project websites when a release page is missing.

* 🔎 **Multi-source resolve** — GitHub Release API + custom JSON repos + AppImageHub catalog feed

  * official-website discovery with a bounded parallel crawler, all racing together so transient
    source failures do not block installs.

* 🚀 **Batch parallel installs** — install multiple packages at once with per-task streaming logs
  and a sticky progress footer on interactive terminals.

* 🔄 **True upgrade semantics** — HTTP ETag / Last-Modified / Content-Length freshness with a
  conservative `Unknown` path; never silently trusts equal-length rewrites.

* 🪞 **Mirror-friendly** — built-in GitHub Release proxies (`ghfast`, `fastgit`, `llkk`, custom
  URL templates) for regions where upstream GitHub is slow.

* 🛡 **Hermetic installs** — every commit writes a signed JSON metadata entry; rollback restores
  the previous `current.AppImage` atomically.

* 🧭 **Auto runtime detection** — probes `--appimage-version` after install; transparently falls
  back to `extract_and_run` or pre-extracted mode when FUSE/libfuse is unavailable.

* 🌏 **i18n** — English and Simplified Chinese CLI output; follows `LANG` / `LC_ALL` or the
  `YAI_LANG` override.

***

## Table of Contents

1. [Quick Start](#quick-start)
2. [Building](#building)
3. [Installing Applications](#installing-applications)
4. [Command Reference](#command-reference)
5. [Update & Upgrade Semantics](#update--upgrade-semantics)
6. [Repositories](#repositories)
7. [Website Discovery](#website-discovery)
8. [Mirror & Downloader](#mirror--downloader)
9. [Filesystem Layout](#filesystem-layout)
10. [Runtime Modes](#runtime-modes)
11. [Localization](#localization)
12. [Repository Index Schema](#repository-index-schema)
13. [Testing](#testing)
14. [Disclaimer](#disclaimer)
15. [License](#license)

***

## Quick Start

```bash
# 1. Build the single yai binary
make

# 2. Add the official yai software index (GitHub mirror on Gitee also available)
./yai repo add appimage https://github.com/yai-byte/yai-repo/raw/main/index.json
#  Or in regions with limited GitHub access:
# ./yai repo add appimage https://gitee.com/no11no16/yai-repo/raw/main/index.json
./yai repo update appimage

# 3. Browse and install
./yai search editor
./yai install obs-studio
./yai install --arch aarch64 applepie

# 4. Install straight from GitHub
./yai install AppImage/AppImageKit

# 5. Install any AppImage URL directly
./yai install https://example.com/releases/demo-x86_64.AppImage

# 6. Keep installed apps up to date
./yai update
./yai upgrade --all --yes
```

After installing `obs-studio`, the `obs-studio` wrapper is dropped into `~/.local/bin`
(put it on your `PATH`) and a `yai-obs-studio.desktop` entry is written to
`~/.local/share/applications`, so the app appears in your desktop menu.

***

## Building

### Requirements

* A C++17-capable compiler (GCC ≥ 8, Clang ≥ 6)

* GNU `make`

* `curl` (runtime dependency; also used for GitHub API calls and website probing)

* Optional, for faster downloads: `aria2c`, `wget2`, or `wget`

### Compile

```bash
make          # produces ./yai + libyai.a
make clean    # removes object files, libyai.a, and the binary
```

The default `CXXFLAGS` build with `-O2 -Wall -Wextra -Wpedantic`. Override variables as needed:

```bash
CXX=clang++ make
CXXFLAGS="-std=c++17 -O0 -ggdb -pthread" make
```

### Install

There is no system install step — `yai` is a single binary that you can copy anywhere on `PATH`,
for example:

```bash
install -m 0755 yai ~/.local/bin/yai
```

***

## Installing Applications

`yai install` accepts several kinds of targets:

| Target form       | Example                                   | Resolution path                                           |
| ----------------- | ----------------------------------------- | --------------------------------------------------------- |
| Package id (repo) | `obs-studio`                              | Lookup in the merged local repo index → source kind apply |
| `owner/repo`      | `AppImage/AppImageKit`                    | GitHub latest release, pick best AppImage asset by arch   |
| HTTP/HTTPS URL    | `https://example.com/foo-x86_64.AppImage` | Direct AppImage fetch with URL freshness update path      |
| `file://` URL     | `file:///tmp/release/demo.AppImage`       | Direct local copy with file-size freshness probing        |
| Local path        | `./build/my.AppImage`                     | Copies the file and matches repo package by filename stem |

Every install target supports these shared flags:

```
--arch  auto|x86_64|aarch64|x86|armv7|riscv64|ppc64le|s390x|loongarch64
--download  direct|mirror_first|direct_first
--mirror-template  <URL template>
--downloader  auto|curl|wget|wget2|aria2c
--jobs  <1..32>
--id  <id>    --name  <display name>   (single-target installs only)
```

When you pass multiple targets (e.g. `yai install foo bar baz`), they run in a bounded worker pool
(default 4 workers). Pass `--jobs 1` for strictly sequential execution.

### Wildcard (pattern) targets

Package ids, repo names, and installed ids accept shell-style `*` and `?` patterns — remember to
quote them so your shell does not expand them first:

```bash
./yai update 'obs*'        # preview updates for every id starting with obs
./yai remove 'kde-*'       # pattern-based removal (asks confirmation for multi match)
./yai upgrade 'kde-*' -y   # skip confirmation with --yes / -y
```

Wildcard-expanded batches run sequentially and stop on the first failure; explicitly enumerated
targets run in parallel. Zero matches still fails a pattern command.

***

## Command Reference

```text
Usage: yai <command> [args...] [options]

Query & search
  search <keyword>                 list packages whose id, name, or summary matches
  info <package>                   full package details from the repo index
  list                             list installed applications with versions & mode
  doctor                           diagnose runtime dependencies (FUSE, arch, wrappers)

Repository management
  repo list                        list configured repos with cache status
  repo add <name> [url-or-path]    register a JSON or AppImageHub-feed index
  repo update [name-or-pattern]    fetch latest index, rebuild combined index.json
  repo remove <name-or-pattern>    drop a repo and its cached file (no uninstall)
  repo resolve [--overwrite]       pre-resolve and cache download URLs for every package

Mirror & network
  mirror list                      show all pre-defined and custom GitHub proxies
  mirror use <name>                pick a pre-defined proxy policy
  mirror custom <template>         register a custom mirror URL template
  mirror off                       disable proxying (use upstream GitHub directly)

Download / install lifecycle
  download <target>...             fetch AppImage to cwd, do not install
  install  <target>...             download, probe, wrap, write metadata, drop desktop entry
  update   [id-or-pattern]         preview upgrades (no network AppImage body transfer)
  upgrade  <id|--all> [--yes]      actually download, commit, write rollback snapshot
  rollback <id>                    swap current.AppImage with the previous version snapshot
  repair   <id>                    re-run probe, re-install wrapper + desktop entry in place
  remove   <id-or-pattern>         rm wrapper, desktop entry, app dir under ~/.local/share/yai
```

### Behaviour of `download`

`download` never installs. It saves the AppImage file to the current working directory as
`{resolved package id}.AppImage` and skips the chmod / probe / metadata / wrapper / desktop-entry
steps entirely. Local default ids strip trailing version and architecture tokens so
`Demo-v1.2.3-x86_64.AppImage` becomes `demo.AppImage`.

***

## Update & Upgrade Semantics

`update` is a **read-only preview**. It never downloads an AppImage body, never probes a binary,
never writes metadata, and never replaces installed files. With no id argument it previews every
installed package; with an id or pattern it previews every matching package.

`upgrade` is a **mutating** command. It:

1. re-runs the same preview as `update`,
2. filters rows whose status is `upgradable`,
3. prints a confirmation prompt (unless `--yes` / `-y`),
4. downloads each candidate into an `update.AppImage` slot,
5. probes the candidate with `--appimage-version`,
6. atomically commits the new AppImage as `current.AppImage` and the previous AppImage into
   `versions/previous/` (rollback snapshot),
7. rewrites the wrapper and desktop entry,
8. writes a new JSON metadata entry and refreshes the repo index overlay so subsequent
   `upgrade --all` runs can take the fast path for GitHub releases.

### Source-kind update rules

| Source kind           | Preview (`update`) strategy                                             |
| --------------------- | ----------------------------------------------------------------------- |
| `github_release`      | Compare latest release tag + asset to installed metadata                |
| `repo_github_release` | Same, via the repo overlay cache first, live API as fallback            |
| `repo_direct_url`     | Index URL change wins; identical URL probes HTTP validators             |
| `repo_website_page`   | **Always re-resolves** via the website crawler (see below)              |
| `url`                 | Probes HTTP validators; on ambiguity → `download verification required` |
| `direct_url` (local)  | File-size comparison with the recorded sha256                           |

> **Consistency guarantee.** Equal `Content-Length` alone never declares a resource as
> `Unchanged` — the lack of ETag / Last-Modified always falls through to `Unknown` so the
> upgrade path can verify the bytes. This matches the "§13 Validator Semantics" design note
> in `docs/superpowers/specs/`.

***

## Repositories

Repositories are JSON feed files (or AppImageHub-style package listings) listed in
`~/.local/share/yai/repos/repos.conf`. Every `repo add` stores one name → URL/path mapping and
caches the downloaded content into `~/.local/share/yai/repos/<name>.json`. Then all cached repo
files are merged into `~/.local/share/yai/repos/index.json` (the authoritative merged index used
by search / info / install / update).

Repositories may also be supplied at runtime:

* `YAI_REPO_INDEX` — when set, overrides the default local merged index. Accepts a local path
  **or** a remote `http(s)://` URL (fetched once per invocation).

* `repo add appimage` — with no argument, pulls the well-known AppImageHub catalog feed and
  converts entries into `github_release` / `direct_url` / `website_page` packages.

### Remote index region auto-detect

yai ships with a default remote index at `https://github.com/yai-byte/yai-repo/raw/main/index.json`
and a Gitee mirror at `https://gitee.com/no11no16/yai-repo/raw/main/index.json`. On start,
`detect_index_region()` probes both endpoints with short timeouts and prefers the faster one so
Mainland China users hit the Gitee mirror without manual configuration.

***

## Website Discovery

When a package is declared `website_page` source kind — common for entries imported from the
AppImageHub feed that list an official site but no GitHub release or direct `.AppImage` URL —
yai runs a bounded parallel crawler to locate the asset without wandering the whole web.

### Host boundary

The crawler follows links on the package website that match at least one of:

* same host as the declared homepage (e.g. `musescore.org`),

* subdomain / sibling host of the homepage that matches the package name,

* trusted download-hint domains (`sourceforge.net`, `github.com/repo/releases`,
  `gitlab.com/repo/-/releases`, `codeberg.org`, `launchpad.net`, etc.),

* one catalog-to-official-site bridge hop (from `appimage.github.io/X` to the project site
  linked in the catalog entry).

It **never** enters community chat sites, forums, social platforms, generic CDNs, or unrelated
project domains; ordinary GitHub source-code pages (non-release) are also skipped to keep the
page count bounded.

### Crawl engine

* Speculative page probes with `--max-time 5` check common download pathname hints
  (`/download`, `/downloads`, `/en/download`, `/linux/download`, `/releases`, `/platforms`, …)
  in parallel and only promote pages that answer 2xx and contain an AppImage link
  or a form/element with an `.AppImage` target.

* A priority queue scores pages by their URL tokens, package-name overlap, and host closeness;
  the crawler stops after reaching a configurable cap of successful AppImage matches.

* Large downloads are **never** performed during HTML discovery; every AppImage-suspect URL is
  checked with a bounded `--max-filesize` range request (first 512 KiB) so misclassified links
  never pull a 200 MiB binary.

***

## Mirror & Downloader

### Built-in GitHub Release proxies

Running `yai mirror list` prints the pre-defined proxy policies. In interactive terminals,
every GitHub-backed download also offers a one-shot "use China network optimization" prompt
(you can also lock the policy permanently with `mirror use`):

```bash
./yai mirror use ghfast
./yai mirror use chengc
./yai mirror use fastgit
./yai mirror use yylx
./yai mirror use llkk
./yai mirror custom 'https://mirror.example.com/gh/{owner}/{repo}/releases/download/{tag}/{asset}'
./yai mirror off
```

Mirror templates accept these placeholders: `{url}`, `{raw_url}`, `{raw_url_noscheme}`,
`{owner}`, `{repo}`, `{tag}`, `{asset}`.

### Downloader selection

`--downloader auto` (the default) tries installed HTTP downloaders in this priority order:
`aria2c` → `wget2` → `wget` → `curl`. If an auto-selected downloader fails on a transfer,
yai falls through to the next available one. `file://` sources always use `curl` in auto mode
to keep local-release and test fixtures predictable.

For non-`curl` downloaders, yai performs a best-effort HTTP header probe with `curl` before
launching the real transfer, so progress lines can still report the total byte size and
recent transfer speed even on downloaders that do not emit native progress information.
For `aria2c`, yai also talks to aria2's local JSON-RPC socket (`completedLength`) so
multi-connection sparse writes do not inflate the downloaded byte count.

***

## Filesystem Layout

yai installs into the **current user's** home directory. There are no global writes, no
`setuid` binaries, and no kernel modules.

| Path (relative to `$HOME`)                                      | Purpose                                         |
| --------------------------------------------------------------- | ----------------------------------------------- |
| `.local/bin/<id>`                                               | Shell wrapper script invoking the right mode    |
| `.local/share/applications/yai-<id>.desktop`                    | Desktop entry for menu integration              |
| `.local/share/yai/apps/<id>/current.AppImage`                   | Installed AppImage binary                       |
| `.local/share/yai/apps/<id>/metadata.json`                      | JSON source / sha256 / mode metadata            |
| `.local/share/yai/apps/<id>/current.AppImage.headers`           | Captured HTTP headers for later freshness probe |
| `.local/share/yai/apps/<id>/versions/previous/current.AppImage` | Rollback snapshot (kept for one upgrade back)   |
| `.local/share/yai/apps/<id>/extracted/`                         | Pre-extracted FUSE tree (used by modes)         |
| `.local/share/yai/repos/repos.conf`                             | List of configured repos                        |
| `.local/share/yai/repos/<name>.json`                            | Per-repo cached index                           |
| `.local/share/yai/repos/index.json`                             | Merged index                                    |
| `.config/yai/network.conf`                                      | Persisted mirror policy                         |
| `.config/yai/github_blocklist.conf`                             | Exact `owner/repo` lines to block with 451      |

Install metadata (`metadata.json`) is always JSON — the legacy `metadata.conf` format is no
longer read. Running `yai repair <id>` cleans up any stray `metadata.conf` left by older
versions.

***

## Runtime Modes

After install, upgrade, or repair, yai probes the binary with `--appimage-version` and a short
launch attempt with captured output. When that output indicates a FUSE / libfuse / sandbox
problem, yai rewrites the wrapper to use the closest supported fallback mode:

| Mode              | Flag / behaviour                                                               |
| ----------------- | ------------------------------------------------------------------------------ |
| `direct`          | Execute `current.AppImage` directly (default when probe succeeds)              |
| `extract_and_run` | `APPIMAGE_EXTRACT_AND_RUN=1 ./current.AppImage` — tmpfs extraction, per-launch |
| `extracted`       | Pre-extract into `extracted/` once and run `extracted/AppRun`                  |

You can always re-probe and rewrite the mode with `yai repair <id>`.

***

## Localization

yai ships English and Simplified Chinese translations in `po/en.po` and `po/zh.po`. By default
it follows `LC_ALL`, `LC_MESSAGES`, then `LANG`: any Chinese locale variant selects Chinese,
everything else falls back to English. To force a specific locale regardless of system settings:

```bash
YAI_LANG=zh yai install obs-studio   # 中文输出
YAI_LANG=en yai update               # stable English for scripts
```

Package ids, file paths, JSON config values, and the columnar shape of `search` / `list` are
intentionally identical across translations so scripts can parse them portably.

***

## Repository Index Schema

All repo JSON files produced by `yai-repo` and accepted by `repo add` share version 1 of the
index schema. Minimal example:

```json
{
  "schema_version": 1,
  "updated_at": "2026-08-01T00:00:00Z",
  "packages": [
    {
      "id": "demo",
      "name": "Demo",
      "summary": "A demo AppImage",
      "homepage": "https://example.com/demo",
      "license": "MIT",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "demo",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    },
    {
      "id": "website-app",
      "name": "Website App",
      "summary": "Published only on the project site",
      "homepage": "https://example.org/app",
      "license": "Unknown",
      "source": {
        "type": "website_page",
        "url": "https://example.org/app/downloads"
      }
    }
  ]
}
```

Supported `source.type` values: `github_release`, `direct_url`, `website_page`. For
`github_release` you may also provide an `asset_pattern`; otherwise yai uses the per-arch
builtin defaults. For `direct_url` the field is `url` and may be any `http(s)://` or
`file://` URL.

***

## Testing

`tests/` contains 22 hermetic shell smoke tests that exercise every user-facing command, the
website crawler, validator freshness, mirror policy, and JSON parser with `fake-bin` interposed
`curl` wrappers so no real network access occurs during the suites.

```bash
make
# Run the whole suite:
for f in tests/*_smoke.sh; do bash "$f" || echo "FAIL: $f"; done
```

Smoke tests as of this release (all 22 passing):

| File                                                                               | Covers                                                       |
| ---------------------------------------------------------------------------------- | ------------------------------------------------------------ |
| `arch_smoke.sh`                                                                    | Architecture name normalisation + auto detection             |
| `json_smoke.sh`                                                                    | JSON scanner round-trips for metadata / repo / GitHub shapes |
| `progress_smoke.sh`                                                                | Single-task download progress line format                    |
| `batch_progress_smoke.sh`                                                          | Parallel batch footer + per-task prefixing                   |
| `download_smoke.sh`                                                                | Direct / URL / owner/repo downloads without install side fx  |
| `metadata_json_smoke.sh`                                                           | Install metadata JSON shape + repair legacy cleanup          |
| `url_freshness_smoke.sh`                                                           | ETag / LM / CL validator matrix                              |
| `url_update_smoke.sh`                                                              | Equal-length same-URL rewrite → Unknown path                 |
| `fetch_text_timeout_smoke.sh`                                                      | Website crawl timeouts on hang-sites stay under 20 s         |
| `website_mtime_smoke.sh`                                                           | Listing-page priority with HTTP-Equiv / Last-Mod hints       |
| `repo_smoke.sh`                                                                    | add / update / remove / list repos including wildcards       |
| `repo_resolve_index_smoke.sh`                                                      | `repo resolve` overlay write + fast-path URLs                |
| `appimage_apps_index_smoke.sh`                                                     | AppImageHub apps index markdown parsing                      |
| `appimage_data_resolve_smoke.sh`                                                   | AppImageHub `data/<name>` entries + GitHub API fallback      |
| `appimage_feed_smoke.sh`                                                           | Catalog → official-site bridge + install/update/upgrade v2   |
| `mirror_policy_smoke.sh`                                                           | Mirror templates + ghfast / fastgit proxy resolution         |
| `wildcard_multi_smoke.sh`                                                          | Wildcard multi-match confirmation + zero-match failure       |
| `stage1_smoke.sh` … `stage5_smoke.sh`                                              | End-to-end install/update/upgrade/rollback/repair flows      |
| `stage4_smoke.sh` also covers ANSI color stripping in non-interactive log captures | <br />                                                       |

***

## Disclaimer

* yai is a client-side tool; it does **not** host or redistribute any binary files. All
  repository entries point to upstream project URLs.

* Mirror / proxy templates route GitHub Release asset transfers through third-party servers.
  yai is not affiliated with those third-party providers; make sure your usage complies with
  local law and each proxy's terms of service.

* `github_blocklist.conf` entries return a 451-style error. Use this file in jurisdictions
  where redistributing a specific project's binaries is restricted.

***

## Authorship & License

This project was designed by the human author. The implementation was assisted by AI tools
under the author's guidance and supervision. All functional design, architecture decisions,
and all features were manually tested by the human author.

yai is distributed under the terms of the project's `LICENSE` file. Parts of the JSON scanner
and CLI template follow prior art from MIT-licensed single-file AppImage tools; see individual
source headers for exact attribution.

**AppImage** is a trademark of Simon Peter. yai is an independent client and is not
endorsed by the AppImage project.
