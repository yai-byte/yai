# yai

`yai` is a small AppImage manager MVP written in C++.

Current scope: stage five repository search, package installation, GitHub
installation, update preview, upgrade, rollback, and runtime repair.

## Build

```bash
make
```

Requires a C++17 compiler and `curl`. AppImage downloads can also use `aria2c`,
`wget2`, or `wget` when installed.

## Commands

```bash
./yai search <keyword>
./yai info <package>
./yai repo list
./yai repo add <name> [url-or-path]
./yai repo update [name]
./yai repo remove <name-or-pattern>
./yai mirror list
./yai mirror use <ghfast|chengc|fastgit|yylx|llkk>
./yai mirror custom <xget-domain-or-template>
./yai mirror off
./yai download <package|url|owner/repo> [...]
             [--arch auto|x86_64|aarch64|x86|armv7|riscv64|ppc64le|s390x|loongarch64]
             [--download direct|mirror_first|direct_first]
             [--mirror-template <template>]
             [--downloader auto|curl|wget|wget2|aria2c]
             [--jobs <n>]
./yai install <package|path|url|owner/repo> [...] [--id <id>] [--name <name>]
             [--arch auto|x86_64|aarch64|x86|armv7|riscv64|ppc64le|s390x|loongarch64]
             [--download direct|mirror_first|direct_first]
             [--mirror-template <template>]
             [--downloader auto|curl|wget|wget2|aria2c]
             [--jobs <n>]
./yai update [id]
./yai upgrade <id|--all> [--yes] [--download direct|mirror_first|direct_first]
             [--mirror-template <template>]
             [--downloader auto|curl|wget|wget2|aria2c]
./yai rollback <id>
./yai repair <id>
./yai doctor
./yai list
./yai remove <id>
```

Package and installed-id arguments accept shell-style `*` and `?` patterns when
the pattern matches exactly one package. Quote patterns such as `'obs*'` so the
shell does not expand them before yai receives the argument. Commands with side
effects reject patterns that match zero or multiple packages.

`download` saves the selected AppImage into the current working directory using
the upstream file name. It does not install, chmod, probe, write metadata,
create wrappers, or overwrite an existing file.

`install` and `download` accept multiple targets in one command. yai runs them
in parallel with up to four workers by default, capped by the number of targets;
use `--jobs <n>` to choose 1 to 32 workers. The same `--arch`, `--download`, and
`--mirror-template`, and `--downloader` options apply to every target. `--id`
and `--name` are available only for a single `install` target because batch
installs derive each package id from its source.

Use `--downloader <tool>` to choose the tool that transfers AppImage bytes.
`auto` is the default and tries installed tools in this priority for HTTP(S)
downloads: `aria2c`, `wget2`, `wget`, then `curl`. If a selected auto downloader
fails, yai tries the next available one. `file://` downloads use `curl` in auto
mode so local fixtures and local-release assets remain predictable. GitHub API
queries and website discovery still use `curl`; the downloader option affects
AppImage file transfers only. For non-curl downloaders, yai makes a best-effort
header request with `curl` before the transfer so the progress line can show the
total size when the server provides `Content-Length`. The progress line reports
recent transfer speed instead of lifetime average speed; for `aria2c`, yai reads
aria2's control file so sparse piece writes do not make the downloaded byte
count jump to the apparent file size.

`install` copies a local AppImage path or downloads a remote AppImage, makes it
executable, detects a runnable mode, and writes the wrapper plus desktop entry.
When the AppImage exposes an internal `.desktop` file, yai reuses its safe
metadata such as `Name`, `Comment`, `Icon`, and `Categories`, while forcing
`Exec` to the yai wrapper.
A target in `owner/repo` form is resolved through GitHub's latest release API.
Use `--arch <arch>` to select a release asset for a specific AppImage
architecture. Supported values are `auto`, `x86_64`/`amd64`/`x64`,
`aarch64`/`arm64`, `x86`, `armv7`/`armhf`, `riscv64`, `ppc64le`/`ppc64el`,
`s390x`, and `loongarch64`; `auto` uses the current machine architecture.
This only chooses the downloaded asset and does not make non-native AppImages
runnable on the host. `upgrade` reuses the installed package's recorded
architecture from metadata.

`update [id]` previews available upgrades without downloading, probing, writing
metadata, or replacing installed files. With no id, it previews all installed
packages; with an id or unique pattern, it previews only that package. GitHub
Release installs compare the latest release tag. Repository `direct_url` and
`website_page` installs re-resolve the current repo package and compare the
selected AppImage file name or URL; plain URL and local-path installs remain
unsupported for update because yai has no stable package source to query.
The preview output always includes status and reason columns so unsupported
packages explain why they cannot be upgraded.

`upgrade --all` first prints the same preview, then upgrades only rows with
status `upgradable`. It asks for confirmation by default; use `--yes` or `-y`
for non-interactive batch upgrades. Rows with `current`, `unsupported`, or
`error` status are skipped and keep their reason in the preview output.

## Language

yai supports English and Simplified Chinese CLI output. By default, it follows
`LC_ALL`, `LC_MESSAGES`, and `LANG`; Chinese locales use Chinese, and other
locales use English. Set `YAI_LANG=zh` or `YAI_LANG=en` to override the system
language. Scripts that need stable English output should set `YAI_LANG=en`.
Package ids, paths, config values, and tabular `search`/`list` row shapes stay
stable for scripts. `search` may shorten long summaries for readability.

Mirror templates can use these placeholders:

- `{url}`: percent-encoded original asset URL.
- `{raw_url}`: original asset URL without percent-encoding.
- `{raw_url_noscheme}`: original asset URL without the leading scheme.
- `{owner}`: GitHub owner.
- `{repo}`: GitHub repository.
- `{tag}`: release tag.
- `{asset}`: release asset file name.

GitHub Release downloads use direct GitHub URLs by default. In an interactive
terminal, every GitHub-backed download asks whether to use China network
optimization for that download. Users can also configure a preferred proxy
explicitly:

```bash
./yai mirror use ghfast
./yai mirror use chengc
./yai mirror use fastgit
./yai mirror use yylx
./yai mirror use llkk
./yai mirror custom xget.example.com
./yai mirror off
```

This only affects GitHub public Release asset downloads. yai does not host or
rewrite package metadata into third-party binaries.
When enabled, yai tries the selected proxy first and falls back to upstream
GitHub if the proxy fails.

Disclaimer:

```text
本工具仅加速 GitHub 公开 Release 资源，供开发者合法获取开源软件；
代理功能由第三方提供，用户需自行确保符合所在地法律法规；
本工具不提供、不托管任何二进制文件，索引均指向 upstream 原链接。
```

Repositories can be blocked with 451 by adding exact `owner/repo` lines to:

```text
~/.config/yai/github_blocklist.conf
```

Supported runtime modes:

- `direct`: run the AppImage directly.
- `extract_and_run`: run the AppImage with `APPIMAGE_EXTRACT_AND_RUN=1`.
- `extracted`: pre-extract the AppImage and run `extracted/AppRun`.

After install, upgrade, or repair, yai silently probes the AppImage with
`--appimage-version` and a short captured launch attempt. If the runtime output
shows a FUSE/libfuse problem, yai automatically selects `extract_and_run` or
`extracted` and writes the wrapper for that mode.

The app installs into the current user's home directory:

- AppImage: `~/.local/share/yai/apps/<id>/current.AppImage`
- install metadata: `~/.local/share/yai/apps/<id>/metadata.json`
- rollback version: `~/.local/share/yai/apps/<id>/versions/previous/`
- extracted app: `~/.local/share/yai/apps/<id>/extracted/`
- wrapper: `~/.local/bin/<id>`
- desktop entry: `~/.local/share/applications/yai-<id>.desktop`

The desktop entry uses the wrapper as `Exec`, so removing the package can cleanly
delete the files managed by `yai`.
Install metadata is JSON and records source fields, file paths, `sha256`, and
`checksum_status`. When yai can compute the downloaded file hash but has no
trusted upstream hash to compare against, it records the hash with
`checksum_status` set to `unknown`.

yai's JSON scanner is deliberately small. It is intended for the current
metadata, repository index, feed, and GitHub release shapes where the fields
needed by yai are strings, positive integers, objects, and arrays. It does not
aim to be a full JSON implementation for booleans, null, floats, complete
Unicode escape semantics, JSONPath-style queries, or general adversarial input.

## Repository Index

Stage four reads a JSON repository index from:

- `$YAI_REPO_INDEX`, when set. This can be a local path or URL.
- `~/.local/share/yai/repos/index.json`, by default.

Repository commands manage `~/.local/share/yai/repos/repos.conf`.
`repo add` stores a named index URL/path, caches it as
`~/.local/share/yai/repos/<name>.json`, and rebuilds the default
`index.json`. `repo update` refreshes all configured repos, while
`repo update <name>` refreshes one configured repo. Both rebuild the combined
index from cached repo files. `repo remove <name-or-pattern>` drops a configured
repo, deletes its cache file, and rebuilds `index.json` without uninstalling
apps. Name/pattern arguments follow the same single-match `*`/`?` rules as
other side-effect commands.

The AppImage project feed can be used directly:

```bash
./yai repo add appimage
./yai repo update appimage
./yai search keyword
```

When importing that feed, yai converts entries with a GitHub link into
`github_release` packages and entries with a direct `.AppImage` link into
`direct_url` packages. Feed descriptions are stored as short summaries instead
of full long descriptions. Entries with an official website but no GitHub
release or direct asset are kept as `website_page`; installing them searches
likely download pages for an AppImage. If the feed URL is an AppImageHub/AppImage
catalog page instead of the real project website, yai may follow one
package-name-matching link from that catalog page to the project site, then
keeps later crawling inside that project's allowed hosts. In an interactive
terminal, that website search updates one status line in place. When stderr is
redirected, yai writes only a short start line and final summary instead of
every checked URL. Website search follows only the package website, same-site
download pages, built-in trusted download hint domains, and that catalog-to-site
bridge. It skips ordinary GitHub pages plus community and social discussion
links such as Reddit, forums, Discord, Twitter/X, Facebook, and YouTube, and
ignores AppImages from unrelated project domains.

Supported schema version:

```json
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "demo",
      "name": "Demo",
      "summary": "Demo AppImage",
      "homepage": "https://example.com",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "demo",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    }
  ]
}
```

## Test

```bash
make
tests/arch_smoke.sh
tests/json_smoke.sh
tests/progress_smoke.sh
tests/download_smoke.sh
tests/stage1_smoke.sh
tests/stage2_smoke.sh
tests/stage3_smoke.sh
tests/stage4_smoke.sh
tests/stage5_smoke.sh
tests/repo_smoke.sh
tests/appimage_feed_smoke.sh
tests/mirror_policy_smoke.sh
tests/metadata_json_smoke.sh
```
