# AppImage 包管理器开发文档

## 1. 项目定位

本项目目标是开发一个类似 `apt` / `dnf` 的 AppImage 专用包管理器，用于在 Linux 桌面环境中统一完成 AppImage 应用的搜索、下载、安装、更新、运行修复、桌面菜单集成和卸载。

AppImage 的优势是应用本体通常只有一个文件，不强依赖发行版包格式。但实际使用中常见问题包括：

- 部分地区直接访问 GitHub Release 下载速度慢或连接不稳定。
- 下载后的 AppImage 没有执行权限，用户不知道需要 `chmod +x`。
- 某些发行版缺少 AppImage 运行所需的 FUSE 2 兼容库。
- 个别 AppImage 无法直接挂载运行，但可以通过解压后运行内部 `AppRun` 或实际可执行文件。
- 应用图标、`.desktop` 快捷方式、开始菜单分类需要手动处理。
- AppImage 更新、校验、来源记录和卸载缺少统一管理。

本项目应把这些重复操作封装为稳定命令和自动修复流程，让用户像使用系统包管理器一样管理 AppImage 应用。

## 2. 设计假设

- 第一阶段以单用户安装为主，不默认写入系统目录。
- 默认安装目录为 `~/.local/share/yai/apps/`。
- 默认命令包装器目录为 `~/.local/bin/`。
- 默认桌面入口目录为 `~/.local/share/applications/`。
- 默认图标目录为 `~/.local/share/icons/hicolor/` 或 `~/.local/share/yai/icons/`。
- 不默认以 `root` 权限运行第三方 AppImage。
- GitHub 加速代理只作为可配置下载通道，不作为可信来源。
- 包元数据必须记录原始下载地址、实际下载地址、校验信息和安装方式。
- 对于无法直接运行的 AppImage，允许降级为“解压后运行”模式。

项目暂定命名为 `yai`，含义可解释为 `Yes! AppImage`。如果后续要换名字，本文中的命令名和路径可以统一替换。

## 3. 核心目标

### 3.1 用户目标

- 一条命令安装 AppImage 应用。
- 自动处理下载慢、权限、FUSE、桌面快捷方式等常见问题。
- 可以查看已安装应用、更新、卸载和重新修复桌面入口。
- 对中国大陆等 GitHub 访问不稳定地区提供可配置加速下载策略。
- 失败时给出明确原因和可执行修复建议。

### 3.2 工程目标

- 下载、校验、安装、运行、集成、卸载流程可测试。
- 包状态使用结构化数据库或清晰 JSON 文件持久化。
- 所有自动修复行为可解释、可回滚。
- 不强绑定单一 Linux 发行版。
- 不把代理站点硬编码为唯一可用路径。

## 4. 非目标

- 不实现完整 Linux 系统包管理器能力，例如系统依赖解析、内核模块安装、服务管理。
- 不默认替代 `apt`、`dnf`、`pacman`。
- 不保证所有 AppImage 都能正常运行，因为应用自身可能依赖宿主系统库、显卡驱动、沙盒配置或内核能力。
- 不绕过应用上游授权、付费、登录或安全限制。
- 不默认信任第三方加速代理提供的内容。

## 5. 目标平台

优先支持：

- Ubuntu / Debian 及其衍生发行版
- Fedora
- Arch Linux / Manjaro
- openSUSE

桌面环境优先支持：

- GNOME
- KDE Plasma
- XFCE
- Cinnamon

依赖的桌面规范以 freedesktop.org 的 Desktop Entry / XDG Base Directory 生态为准。

## 6. 命令行设计

### 6.1 基础命令

```bash
yai search <keyword>
yai info <package>
yai install <package|path|url|owner/repo> [...] [--id <id>] [--name <name>] [--jobs <n>] [--downloader auto|curl|wget|wget2|aria2c] [--recrawl]
yai download <package|url|owner/repo> [...] [--jobs <n>] [--downloader auto|curl|wget|wget2|aria2c] [--recrawl]
yai remove <id>
yai update [id]
yai upgrade <id|--all> [--yes]
yai rollback <id>
yai list
yai repair <id>
yai doctor
```

`<package>` 和 `<id>` 参数支持 shell 风格 `*`、`?` 通配符，但只作为唯一匹配的快捷写法：精确 ID 优先；没有精确命中时，如果通配符匹配 0 个或多个包，命令必须失败并提示候选，不能自动批量安装、更新或卸载。用户应写成 `'obs*'` 这类带引号形式，避免 shell 在 yai 收到参数前先展开。

`install` 和 `download` 可以在同一条命令中接收多个目标，并通过 `--jobs <n>` 控制并发任务数。未指定时默认最多 4 个并发 worker，且不会超过目标数量；`--jobs` 接受 1 到 32。批量命令中的 `--arch`、`--download`、`--mirror-template`、`--downloader` 作用于所有目标。`--id` 和 `--name` 只允许用于单目标安装，因为批量安装需要为每个目标分别推导 package id 和显示名。

`--recrawl` 用于强制从原始包源重新解析，而非使用仓库索引中已记录的下载 URL。适用于索引中的下载链接过期或需要获取最新版本的场景。`repo resolve` 命令同样支持此选项。

`--downloader` 控制实际传输 AppImage 文件的外部下载器。默认值为 `auto`，HTTP(S) 下载按 `aria2c`、`wget2`、`wget`、`curl` 的优先级选择当前系统已安装的工具；如果 auto 选中的工具失败，会尝试下一个可用工具。用户也可以显式指定 `curl`、`wget`、`wget2` 或 `aria2c`。`file://` 下载在 auto 模式下继续使用 `curl`，以保证本地 Release fixture 和本地资产行为稳定。GitHub API 查询、仓库索引下载和网站发现仍使用 `curl`，下载器选项只影响 AppImage 文件本体的下载。非 curl 下载器开始传输前，yai 会用 `curl` 尝试预取响应头；如果服务器提供 `Content-Length`，进度行同样显示总大小。进度行中的速度使用最近约 1 秒窗口内的接收增量，而不是当前下载量除以总耗时；`aria2c` 下载会优先读取 aria2 控制文件中的完成量，避免分片稀疏写入时把 `.part` 文件的 apparent size 误报为真实已下载量。

### 6.2 仓库管理命令

```bash
yai repo list
yai repo add <name> [url-or-path]
yai repo update [name]
yai repo remove <name-or-pattern> [--yes]
yai repo resolve [--output <path>] [--arch <arch|all>] [--type <type>]
                 [--package <id>] [--overwrite] [--concurrency <n>] [--aggressive]
                 [--show <xyz>] [--summary|--no-summary]
```

`repo remove` 支持包源名精确匹配或 shell 风格通配符模式（如 `'app*'`），删除对应缓存并重建 `index.json`。不会卸载已安装应用。

`repo resolve` 用于批量解析仓库包的下载地址（GitHub Release、Website、Direct URL 等），并将结果写回本地索引或指定输出路径。主要选项：

- `--output <path>`：将解析后的完整索引写到指定路径
- `--arch <arch|all>`：指定目标架构，`all` 解析所有规范架构
- `--type <type>`：只解析指定类型的包源（`github_release`、`website_page`、`direct_url`）
- `--package <id>`：只解析指定包 ID，可多次指定
- `--overwrite`：强制覆盖已有下载 URL
- `--concurrency <n>`：手动指定并发线程数（1-32），默认自动检测
- `--aggressive`：启用激进并发模式（见下方并发策略说明）
- `--show <xyz>`：三位掩码控制输出详细度，`x`=成功、`y`=跳过、`z`=失败（默认 `001`）
- `--summary|--no-summary`：显示/隐藏汇总统计

#### 仓库解析并发策略

`repo resolve` 使用独立的并发模型，与 `install`/`download` 的 `--jobs`（默认 4）不同：

- **普通模式**：并发数 = `CPU 核心数`，上限 **8**
- **激进模式**（`--aggressive`）：并发数 = `2 × CPU 核心数`，上限 **16**
- **手动模式**（`--concurrency <n>`）：使用用户指定值，上限 32

选择建议：2-4 适合慢网络，8-16 适合快速网络。当目标数量为 1 或显式指定 `--concurrency 1` 时，退化为串行模式。

### 6.3 下载源与代理命令

```bash
yai mirror list
yai mirror use <ghfast|chengc|fastgit|yylx|llkk>
yai mirror custom <xget-domain-or-template>
yai mirror off
```

示例代理模板：

```text
https://example-proxy.invalid/{url}
https://example-proxy.invalid/https://github.com/{owner}/{repo}/releases/download/{tag}/{asset}
```

说明：

- `{url}` 表示 URL 编码后的原始下载地址。
- `{raw_url}` 表示未编码的原始下载地址，适用于前缀式 GitHub Release 代理。
- `{raw_url_noscheme}` 表示去掉协议前缀后的原始下载地址，适用于部分 GitHub Release 代理路径。
- `{owner}`、`{repo}`、`{tag}`、`{asset}` 从 GitHub Release URL 中解析。
- 具体代理站点容易失效，也可能存在篡改风险，因此必须由配置或社区仓库维护，不应写死在核心代码中。
- 默认始终直连 GitHub。交互式终端中，每次 GitHub Release 下载都会询问本次是否使用中国网络优化；用户也可显式执行 `yai mirror use/custom` 配置偏好代理。
- 启用代理时使用 `mirror_first`，先尝试用户选择的代理，失败后才回退 upstream GitHub。
- 预置代理仅作为用户可选模板，支持 GHFast、ChengC、FastGit、YYLX、LLKK；用户也可以自填 Xget 域名或完整模板。
- 免责声明：本工具仅加速 GitHub 公开 Release 资源，供开发者合法获取开源软件；代理功能由第三方提供，用户需自行确保符合所在地法律法规；本工具不提供、不托管任何二进制文件，索引均指向 upstream 原链接。

### 6.4 多语言输出

- CLI 面向用户的帮助、提示、进度、安装结果、修复结果、更新结果、镜像配置和 doctor 输出支持英文和简体中文。
- `YAI_LANG=en` 强制英文，`YAI_LANG=zh` 强制中文。
- 未设置 `YAI_LANG` 时按 `LC_ALL`、`LC_MESSAGES`、`LANG` 自动判断；中文 locale 使用中文，其它 locale 使用英文。
- 需要稳定英文输出的脚本应显式设置 `YAI_LANG=en`。
- 包 ID、路径、配置值、metadata、以及 `search` / `list` 的制表符分隔行结构保持稳定，避免破坏脚本；`search` 的长摘要可缩短显示。
- 新增用户可见输出时应通过 i18n 辅助函数，除非该输出是机器可解析数据。

## 7. 用户工作流

### 7.1 安装仓库内应用

```bash
yai install obsidian
```

流程：

1. 从本地索引查找 `obsidian`。
2. 解析最新版本、资产文件和架构。
3. 选择下载通道。
4. 下载或复制到 yai 管理的应用目录。
5. 计算文件哈希并记录校验状态。
6. 设置执行权限。
7. 探测 AppImage 运行方式。
8. 安装到应用目录。
9. 生成启动包装器。
10. 提取图标和桌面元数据。
11. 写入 `.desktop` 文件。
12. 刷新桌面数据库。
13. 记录安装状态。

### 7.2 安装 URL

```bash
yai install https://github.com/owner/repo/releases/download/v1.0.0/App-x86_64.AppImage
yai install owner/repo --arch arm64
```

流程：

1. 解析 URL 类型。
2. 如果是 GitHub Release URL，尝试识别 owner、repo、tag、asset。
3. 如果用户启用代理，则生成候选加速 URL。
4. 下载文件。
5. 计算文件哈希并记录 `checksum_status=unknown`。
6. 执行常规安装流程。

### 7.3 仅下载 AppImage

```bash
yai download owner/repo
yai download package-id --arch riscv64
yai download owner/one owner/two --jobs 2
yai download owner/repo --downloader aria2c
```

`download` 命令只把解析出的 AppImage 文件下载到用户运行命令时的当前目录，保存为 `{解析后的包 id}.AppImage`。URL 与本地路径在未显式指定 `--id` 时，默认 id 会去掉文件名末尾的版本号和架构标记；本地 `install` 无 `--id` 时会尝试按文件名 stem 匹配仓库中的包。它不设置执行权限、不探测运行模式、不生成 wrapper、不写 metadata、不创建桌面入口，也不覆盖当前目录已有的同名文件。

### 7.3 修复应用

```bash
yai repair app-name
```

修复内容：

- 补充执行权限。
- 重新生成 wrapper。
- 重新生成 `.desktop` 文件。
- 重新提取图标。
- 重新探测 FUSE 状态。
- 如果直接运行失败，切换到解压运行模式。
- 清理损坏的旧解压目录。

### 7.4 诊断环境

```bash
yai doctor
```

诊断内容：

- 当前发行版和架构。
- `~/.local/bin` 是否在 `PATH` 中。
- FUSE 相关库是否存在。
- `xdg-desktop-menu`、`update-desktop-database` 是否可用。
- AppImage 临时目录是否可写。
- 下载代理是否可用。
- 已安装应用是否存在缺失文件或损坏快捷方式。

## 8. 包源与元数据设计

### 8.1 仓库索引格式

仓库可以是一个 JSON 文件或目录，第一阶段建议使用 JSON，便于实现和调试。

示例：

```json
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "example-app",
      "name": "Example App",
      "summary": "示例 AppImage 应用",
      "homepage": "https://example.com",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "owner",
        "repo": "repo",
        "asset_pattern": ".*x86_64.*\\.AppImage$",
        "include_prerelease": false
      },
      "desktop": {
        "categories": ["Utility"],
        "keywords": ["example"]
      }
    }
  ]
}
```

#### 远程索引与区域自动分流

当用户未通过 `YAI_REPO_INDEX` 指定本地索引时，yai 会自动从远程仓库 `yai-byte/yai-repo` 获取最新的 `index.json`：

- **默认源**：`https://raw.githubusercontent.com/yai-byte/yai-repo/main/index.json`（GitHub）
- **CN 分流**：当 `detect_index_region()` 判断当前环境为中国大陆（`LC_CTYPE` 或时区为 `Asia/Shanghai` 等）时，自动切换到 Gitee 镜像 `https://gitee.com/no11no16/yai-repo/raw/main/index.json`，以降低网络延迟。
- **策略枚举**：内部使用 `IndexStrategy { Auto, Trust, Live }`，Auto 模式根据区域自动选择，Trust 模式强制使用 GitHub 源，Live 模式强制实时远程拉取。详见下文。
- **环境变量覆盖**：`YAI_REPO_INDEX=<url-or-path>` 可强制指定索引来源；`YAI_GITHUB_API_BASE=<url>` 可覆盖 GitHub API 根地址，便于测试或接入兼容代理。

##### IndexStrategy 策略枚举

`IndexStrategy` 用于控制 `update` 命令在执行包版本比对时是信任本地索引、强制信任远程索引，还是完全绕过索引进行实时解析。对应 CLI 选项 `--index-strategy auto|trust|live`、`--trust-index`（等价于 `--index-strategy trust`）、`--live-resolve`（等价于 `--index-strategy live`）。内部由 `commands_update.cpp` 的 `update_app()` 实现：

| 策略 | 行为 |
|------|------|
| `Auto`（默认） | 远程拉取索引 → 用 `repo_index_is_fresh()` 检查 `updated_at` 时间戳（默认 7 天有效期，可通过 `--index-freshness <days>` 调整）。若索引新鲜则 `sync_remote_index_to_local()` 同步到本地并使用；若已过期则降级为实时解析（`use_index = false`）。远程拉取失败同样降级为实时解析，不抛错。 |
| `Trust` | 强制信任远程索引。即使 `updated_at` 已超出有效期阈值，也会调用 `sync_remote_index_to_local()` 覆盖本地索引，随后按本地索引中的版本号进行更新比对。适合用户知道远程索引刚刚更新、但时间戳字段还未刷新的情况。 |
| `Live` | 完全绕过索引机制。直接对每个已安装包执行实时解析（GitHub Release API / Website Crawl / AppImageHub 三级回退），适合调试索引差异或强制获取最新版本的场景。 |

##### 远程索引的新鲜度检查

`repo_index_is_fresh()` 会解析 `index.json` 顶层 `updated_at` 字段（ISO-8601 UTC，格式 `YYYY-MM-DDTHH:MM:SSZ`），并与当前时间对比。当 `updated_at` 字段缺失或无法解析时，视为陈旧索引，`Auto` 模式下会降级为实时解析。

### 8.2 安装状态格式

每个已安装应用保存一份状态文件：

```json
{
  "id": "example-app",
  "name": "Example App",
  "version": "1.0.0",
  "arch": "x86_64",
  "install_mode": "direct",
  "installed_at": "2026-07-20T00:00:00Z",
  "source_kind": "github_release",
  "source_url": "https://github.com/owner/repo/releases/download/v1.0.0/App.AppImage",
  "download_url": "https://example-proxy.invalid/...",
  "github_owner": "owner",
  "github_repo": "repo",
  "github_asset": "App.AppImage",
  "sha256": "sha256-value",
  "checksum_status": "unknown",
  "files": {
    "appimage": "~/.local/share/yai/apps/example-app/current.AppImage",
    "wrapper": "~/.local/bin/example-app",
    "desktop": "~/.local/share/applications/yai-example-app.desktop",
    "extracted_dir": "~/.local/share/yai/apps/example-app/extracted"
  }
}
```

`install_mode` 可选值：

- `direct`：直接执行 AppImage。
- `extract_and_run`：运行时设置 `APPIMAGE_EXTRACT_AND_RUN=1` 再执行 AppImage。
- `extracted`：安装时解压，之后运行解压目录中的入口文件。
- `failed`：安装未完成或运行方式无法确认。

### 8.3 JSON 解析边界

当前 C++ 实现使用项目内轻量 JSON scanner，而不是完整 JSON 库。该 scanner 只服务 yai 当前已知输入形状：

- 安装状态 metadata。
- 仓库索引 schema v1。
- AppImage feed 中 yai 需要的字段。
- GitHub latest release 响应中 yai 需要的字段。

调用方只能依赖字符串、正整数、对象和数组的有限提取能力。布尔值、`null`、浮点数、完整 Unicode escape 语义、任意 JSONPath 查询和恶意构造输入不属于当前支持范围。若后续仓库格式继续扩展到签名、复杂依赖、布尔开关或更广泛的第三方 JSON 输入，应改用真实 JSON 库并同步更新构建依赖和回归测试。

## 9. 下载系统设计

### 9.1 下载策略

支持以下策略：

- `direct`：只使用原始 URL。
- `mirror_first`：优先使用代理 URL，失败后回退原始 URL。
- `direct_first`：优先原始 URL，慢或失败后切换代理。

当前实现默认是 `direct`。GitHub Release 下载可由用户显式启用 `mirror_first` 代理优先：

```bash
yai mirror list
yai mirror use ghfast
yai mirror custom xget.example.com
yai mirror off
```

该配置保存在 `~/.config/yai/network.conf`。交互式终端中，每次 GitHub Release 下载都会询问本次是否使用代理；非交互场景按配置执行，没有配置则默认直连且不弹出问题。

### 9.2 GitHub Release 解析

对于 GitHub Release 包源：

1. 通过 GitHub Release API 获取 release 列表或 latest release。
2. 跳过 draft。
3. 默认跳过 prerelease。
4. 根据当前架构匹配资产文件。
5. 优先选择 `.AppImage` 后缀文件。
6. 当前实现计算下载后文件的 SHA256 并写入 metadata，但尚不消费 release digest 或 `.sha256` 文件做可信校验。

### 9.3 下载暂存

当前实现没有全局下载缓存，也没有对应的缓存清理命令。下载会直接暂存到目标文件旁边的 `.part` 文件：

- `install` 写入应用目录中的 `current.AppImage.part`，成功后移动为 `current.AppImage`。
- `download` 写入用户当前目录中的 `{source.id}.AppImage`，成功前使用同目录 `.part` 文件。
- curl 响应头临时写入同目录 `.headers` 文件，用于读取 `Content-Length` 并渲染 TTY 进度。
- 下载失败时清理 `.part` 和 `.headers`；清理失败只作为 warning，不改变主要失败原因。

#### Landing-Page 重定向 AppImage 解析

`stage_appimage_source()` 在下载完成后会对响应体进行自动检测，以处理"下载链接指向中间 HTML 页面、实际 AppImage 需要跟随内嵌链接才能获取"的场景（常见于版本选择页、权限确认页等）：

1. 下载完成后，调用 `appimage_url_from_download_landing_page(target, downloaded_url, arch)` 检查 `.part` 文件内容
2. 如果响应不是 HTML（`file_looks_like_html` 返回 false），按正常 AppImage 处理流程继续
3. 如果响应是 HTML 但解析不出 AppImage 链接，清除临时文件并抛出错误，提示用户"下载 URL 返回了 HTML 页面"
4. 如果解析出的 AppImage URL 与原下载 URL 相同，视为直接下载完成，返回该 URL 的 HTTP 校验头
5. 如果解析出**新的** AppImage URL，清除临时 HTML 文件，记录日志，然后将 `source_url`/`download_url` 更新为解析出的新 URL，进入下一轮下载
6. 最多重定向 **3 层**（`for (int redirects = 0; redirects < 3; ++redirects)`），超过限制时抛出 `too many nested AppImage landing pages` 错误
7. 重定向过程中 `http_etag`/`http_last_modified`/`http_content_length` 始终取**最后一次**成功下载的校验头，`source_url` 则保留**最外层**原始 URL（用于 metadata 溯源）

HTML 解析由 `appimage_url_from_download_landing_html()` 实现，内部通过 `html_appimage_urls()` 提取所有 `<a href>` 中指向 `.AppImage` 的候选 URL，再用 `best_appimage_url_from_candidates()` 按架构匹配度评分选出最优候选。

### 9.4 安全要求

- 当前实现会计算 SHA256 用于记录和排查，但没有可信上游哈希校验。
- 未校验文件可以安装，但状态必须标记为 `checksum_status=unknown`。
- 后续如果接入上游哈希或签名，使用代理下载时必须按 upstream 提供的校验信息验证代理下载结果。
- 不执行下载目录中的临时文件。
- 不从代理站点读取“替代元数据”作为可信版本信息。

## 10. AppImage 运行与修复策略

### 10.1 运行前检查

安装后执行探测：

1. 文件是否存在。
2. 文件是否有执行权限。
3. 架构是否与系统匹配。
4. `--appimage-version` 是否可运行。
5. 是否可以短时间静默启动，捕获输出并在超时后结束探测进程。
6. 是否出现 FUSE / libfuse 相关错误。
7. 是否可以通过 `APPIMAGE_EXTRACT_AND_RUN=1` 运行。
8. 是否可以 `--appimage-extract` 后找到 `AppRun`。

### 10.2 权限修复

如果 AppImage 没有执行权限：

```bash
chmod u+x <app.AppImage>
```

只给当前用户增加执行权限，不默认修改 group / other 权限。

### 10.3 FUSE 问题处理

AppImage 通常依赖 FUSE 挂载自身内容。不同发行版的 FUSE 包名和默认版本不同，因此处理策略应分层：

1. 检测是否存在 `/dev/fuse`。
2. 检测当前用户是否可访问 FUSE。
3. 检测常见 FUSE 2 兼容库。
4. 给出发行版相关安装建议。
5. 如果用户不想安装 FUSE 或无权限安装，则切换解压运行。

示例提示：

```text
检测到此 AppImage 直接运行需要 FUSE，但当前系统不可用。
可选方案：
1. 安装发行版提供的 FUSE 2 兼容包。
2. 运行 yai repair <app>，yai 会自动探测并回退到 extract-and-run 或 extracted 模式。
```

包管理器本身不应在未确认的情况下自动执行 `sudo apt install` 或 `sudo dnf install`。

### 10.4 直接运行模式

wrapper 示例：

```bash
#!/usr/bin/env bash
exec "$HOME/.local/share/yai/apps/example-app/example-app.AppImage" "$@"
```

适用情况：

- 文件有执行权限。
- FUSE 工作正常。
- AppImage 可直接启动。

### 10.5 extract-and-run 模式

wrapper 示例：

```bash
#!/usr/bin/env bash
export APPIMAGE_EXTRACT_AND_RUN=1
exec "$HOME/.local/share/yai/apps/example-app/example-app.AppImage" "$@"
```

适用情况：

- AppImage 支持 `APPIMAGE_EXTRACT_AND_RUN=1`。
- 直接运行失败但临时解压运行成功。
- 用户不想长期占用解压后的磁盘空间。

缺点：

- 每次启动可能变慢。
- 某些旧 AppImage 不支持。

### 10.6 预解压运行模式

安装时执行：

```bash
./app.AppImage --appimage-extract
```

然后将 `squashfs-root` 移动到：

```text
~/.local/share/yai/apps/<id>/extracted/
```

wrapper 示例：

```bash
#!/usr/bin/env bash
exec "$HOME/.local/share/yai/apps/example-app/extracted/AppRun" "$@"
```

适用情况：

- FUSE 不可用。
- `extract-and-run` 不可用或太慢。
- 用户确认接受额外磁盘占用。

注意：

- 解压目录必须视为应用本体，卸载时一并删除。
- 升级时应删除旧解压目录并重新解压。
- 如果 `AppRun` 不存在，可以尝试读取 `.desktop` 文件中的 `Exec`，但必须限制在解压目录内部，避免执行任意外部命令。

## 11. 桌面集成设计

### 11.1 `.desktop` 文件位置

单用户安装写入：

```text
~/.local/share/applications/yai-<package-id>.desktop
```

### 11.2 `.desktop` 文件来源

安装、升级或修复时，yai 优先通过 `--appimage-extract` 或已解压目录读取 AppImage 内部的 `.desktop` 文件，并复用其中安全的桌面元数据，例如 `Name`、`Comment`、`Icon`、`Categories`、`StartupWMClass` 等。

如果无法提取 `.desktop` 文件，则使用 yai 的最小模板作为回退。

要求：

- `Exec` 指向 yai 生成的 wrapper，而不是直接指向下载缓存。
- 不保留 AppImage 内部 `.desktop` 的 `Exec`、`TryExec`、`Path`、`Actions` 和 `DBusActivatable`。
- `Icon` 优先使用安装时提取或包元数据指定的图标。
- `Name` 优先使用上游桌面文件中的名称。
- `Categories` 优先使用上游值，否则使用包元数据或 `Utility`。
- 不把未经转义的用户输入直接写入 `.desktop`。

### 11.3 图标提取

提取顺序：

1. 解压 AppImage 后查找 `.DirIcon`。
2. 查找 `*.desktop` 中的 `Icon` 字段。
3. 在解压目录中匹配 `*.png`、`*.svg`、`*.xpm`。
4. 使用包元数据提供的图标 URL。
5. 使用 yai 默认图标。

图标安装建议：

```text
~/.local/share/icons/hicolor/<size>x<size>/apps/yai-<package-id>.png
```

如果无法判断尺寸，可放入：

```text
~/.local/share/yai/icons/<package-id>/
```

并在 `.desktop` 的 `Icon` 字段使用绝对路径。

### 11.4 刷新桌面缓存

安装、更新、卸载后尝试执行：

```bash
update-desktop-database ~/.local/share/applications
```

如果命令不存在，不应视为安装失败，只记录警告。

## 12. 卸载设计

```bash
yai remove example-app
```

卸载流程：

1. 读取安装状态。
2. 删除 wrapper。
3. 删除 `.desktop` 文件。
4. 删除图标文件。
5. 删除 AppImage 文件。
6. 删除解压目录。
7. 删除安装状态文件。
8. 刷新桌面数据库。

默认不删除：

- 用户配置目录。
- 应用自身创建的数据目录。
- yai 当前不存在的全局下载缓存。

当前实现没有深度清理选项，也不会删除应用私有配置或 yai 之外的用户数据。

## 13. 更新预览与升级设计

```bash
yai update
yai update example-app
yai upgrade example-app
yai upgrade --all
yai upgrade --all --yes
```

`yai update [id]` 只做更新预览，不下载候选 AppImage、不执行运行探测、不写 metadata、不替换当前安装、不生成 rollback 版本。无参数时预览所有已安装应用；带 `<id>` 或唯一通配符时只预览指定应用。预览结果按行输出包 ID、当前版本、最新版本、状态和原因。状态和原因是一等输出列：不可升级的包必须输出 `unsupported` 或 `error`，并说明不能升级的具体原因。

`yai upgrade <id>` 执行单包真实升级事务。`yai upgrade --all` 先输出同样的更新预览，只对状态为 `upgradable` 的包执行升级，并默认要求用户确认；`yai upgrade --all --yes` / `-y` 用于非交互批量升级。状态为 `current`、`unsupported` 或 `error` 的包会被跳过，原因保留在预览输出中。

GitHub Release 来源升级流程：

1. 读取已安装 metadata 中的 GitHub owner/repo、版本和架构。
2. 查询 GitHub latest release。
3. 比较 latest tag 和本地 `version`。
4. 下载候选 AppImage 到应用目录内的临时位置。
5. 对候选文件执行运行模式探测。
6. 保留当前 `current.AppImage` 和 `metadata.json` 到 `versions/previous/`。
7. 激活候选文件。
8. 重新生成 wrapper、desktop 和 metadata。
9. 清理临时候选；保留最近一个 `previous` 用于回滚。

repo `direct_url` 和 `website_page` 来源没有 release API。它们的更新预览和升级会重新解析当前 repo index 中同 ID 的包，并比较解析得到的 AppImage 文件名或 URL；发生变化时才把候选文件下载到临时位置并复用同一套运行探测、事务提交和回滚流程。

`source_kind=url` 的普通 URL 直装使用 metadata 中记录的 `source_url` / `download_url`。`yai update` 通过 HTTP 校验头（`http_etag`、`http_last_modified`、`http_content_length`）或 `file://` 的文件大小探测远程内容是否变化，必要时标记为 `download verification required`，但不下载 AppImage 正文。`yai upgrade` 会重新下载并比较 sha256；安装或升级成功后会写回新的 `sha256` 和 `http_*` 字段。

`source_kind=repo_direct_url` 仍优先跟随 repo index 中的 URL 变化；当 index URL 与已安装 `source_url` 相同时，复用上述 URL freshness 探测，而不是仅凭版本字符串或文件名判断。

`source_kind=local_path` 仍不支持更新预览和升级。

失败回滚：

- 新版本下载失败：不改变当前安装。
- 新版本探测失败：删除候选文件，不改变当前安装。
- 新版本提交阶段失败：尝试恢复 `versions/previous/` 中的 AppImage 和 metadata。

## 14. 数据目录结构

建议结构：

```text
~/.local/share/yai/
  apps/
    <package-id>/
      current.AppImage
      versions/
        previous/
          current.AppImage
          metadata.json
      extracted/
      metadata.json
  repos/
    repos.conf
    index.json
    <name>.json

~/.config/yai/
  network.conf
  github_blocklist.conf
```

## 15. 配置文件设计

当前实现只落地两个简单配置文件，不读取 TOML：

示例 `~/.config/yai/network.conf`：

```text
china_network_prompted=1
provider=ghfast
download_strategy=mirror_first
mirror_template=https://ghfast.top/{url}
```

示例 `~/.config/yai/github_blocklist.conf`：

```text
owner/repo
```

## 16. 安全模型

### 16.1 信任边界

- yai 信任本地配置和官方包源索引。
- yai 不默认信任下载代理。
- yai 不默认信任 AppImage 内部 `.desktop` 文件的 `Exec` 内容。
- yai 不默认信任应用运行时行为。
- yai 不把 GitHub 代理作为可信来源；代理只用于获取 upstream 公开 Release 资源。

### 16.2 必须防范的问题

- 代理站点返回被篡改文件。
- 包元数据写入恶意 `.desktop Exec`。
- AppImage 内部 `.desktop` 使用危险命令。
- 文件名包含换行、引号、分号等特殊字符。
- 解压路径穿越。
- 旧版本残留 wrapper 指向不存在文件。

### 16.3 基本策略

- 所有路径使用绝对路径或规范化后的用户目录路径。
- `.desktop` 文件的启动入口只写入 yai 生成的 wrapper 路径。
- 只复用 AppImage 内部 `.desktop` 的安全元数据字段。
- wrapper 使用固定模板。
- 下载文件先完整暂存到目标旁边的 `.part` 文件，成功后再移动到正式路径；当前仅计算 SHA256 并记录 `checksum_status=unknown`。
- 解压只允许写入 yai 控制的临时目录。
- 升级使用原子替换，避免半安装状态。
- GitHub 仓库在访问 API 或生成代理 URL 前先执行 451 检查。
- 用户可在 `~/.config/yai/github_blocklist.conf` 中添加精确 `owner/repo` 黑名单；命中时返回 `451 Unavailable For Legal Reasons`。
- **内置 blocklist**：除用户自定义黑名单外，yai 还内置了基于仓库名关键词的合规性过滤。任何仓库 owner/repo 包含 `crack`、`keygen`、`warez`、`piracy`、`pirated`、`ransomware`、`malware`、`phishing`、`botnet` 等关键词时，会在 `enforce_github_release_policy()` 中被直接拒绝，返回同样的 451 错误。内置过滤不依赖用户配置，始终生效。
- **GitHub Token 环境变量**：`YAI_GITHUB_TOKEN` 环境变量用于提升 GitHub API 速率限制（从 60 req/h 提升到 5000 req/h），并允许访问私有仓库。当未设置 Token 时遇到速率限制或 403，yai 会在错误消息中提示用户设置该变量。Token 通过 `Authorization: Bearer` 请求头传递，仅发往 `api.github.com`，不会被写入配置文件或日志。

## 17. 错误处理与用户提示

错误信息应包含：

- 失败阶段。
- 原因。
- yai 已经做过什么。
- 用户可以执行的下一步。

示例：

```text
升级失败：候选版本无法通过运行探测
原因：AppImage 启动输出显示 FUSE 不可用，且解压运行也失败。
已处理：已删除候选文件，未替换当前安装。
建议：运行 yai doctor 检查 FUSE 环境，或稍后重试升级。
```

## 18. 语言和实现建议

### 18.1 推荐语言

推荐使用 C++。

C++ 优点：

- 用户熟悉，早期开发和调试成本低。
- 可以直接使用 POSIX 文件权限、进程调用和 XDG 目录生态。
- C++17 标准库提供 `std::filesystem`，足够支撑阶段一的文件安装、卸载和状态管理。
- 后续可以按需要引入 libcurl、SQLite、TOML/JSON 库，而不是 MVP 阶段过早绑定复杂依赖。

阶段一建议保持依赖很少：

- C++17 编译器。
- `make`。
- 系统 `curl` 命令，用于完成 URL 下载。

后续阶段如果需要更稳定的下载控制、进度条、代理策略和错误分类，可以把下载模块从调用 `curl` 子进程替换为 libcurl。

### 18.2 模块划分

当前实现采用扁平布局，所有源文件直接放在 `src/` 根目录下，按文件名前缀归类职责，不再使用子目录拆分：

```text
src/
  main.cpp
  core.cpp                # 配置、路径、信号、通用工具
  commands.cpp            # 命令分发
  commands_lifecycle.cpp  # install / download / repair / rollback 生命周期
  commands_query.cpp      # search / info / list / remove
  commands_update.cpp     # update 预览
  commands_upgrade.cpp    # upgrade 事务
  commands_repo.cpp       # repo list / add / update / remove
  commands_repo_resolve.cpp  # repo resolve
  commands_doctor.cpp     # doctor 诊断
  cli_download.cpp        # CLI 解析、下载器选择、curl 封装
  download_progress.cpp   # 进度条、aria2 RPC、批量进度
  batch_progress_event.cpp
  batch_ui.cpp
  terminal_color.cpp
  arch.cpp                # 架构归一化与 asset 评分
  process.cpp             # 子进程执行
  json.cpp                # 轻量 JSON scanner
  i18n.cpp                # tr() / tr_format() / PO 加载
  repo.cpp                # 仓库索引读写、合并
  repo_feed.cpp           # AppImage feed 导入、catalog 解析
  repo_appimage_github.cpp  # apps/ 与 data/ 数据源解析
  repo_index_urls.cpp     # 仓库索引 URL 字段操作
  resolver.cpp            # 解析入口、并行 fallback 框架
  resolver_github.cpp     # GitHub Release 解析、blocklist
  resolver_gitlab.cpp     # GitLab CI artifact 解析
  resolver_url.cpp        # URL 校验、allowed_hosts、官网域策略
  resolver_website.cpp    # 网站流水线抓取
  url_freshness.cpp       # HTTP 校验头探测
  appimage.cpp            # metadata 读写、sha256
  appimage_desktop.cpp    # .desktop 重写、图标提取
  appimage_runtime.cpp    # 运行模式探测
  yai.hpp                 # 公共头文件
```

文件名前缀对应原设计中的模块职责：

- `main.cpp` / `commands*.cpp`：入口与命令分发，对应原 `cli` / `installer` / `doctor`。
- `core.cpp`：配置文件、路径、信号处理，对应原 `config` / `fsutil` / `state`。
- `cli_download.cpp` / `download_progress.cpp` / `batch_*.cpp`：命令解析与下载进度，对应原 `cli` / `downloader`。
- `repo*.cpp`：包源索引读取与更新，对应原 `repo`。
- `resolver*.cpp`：包名、版本、架构和资产解析，对应原 `resolver`。
- `resolver_github.cpp` 内含 blocklist 与代理 URL 生成，对应原 `mirror`。
- `appimage*.cpp`：AppImage 探测、解压、运行模式、`.desktop` 与图标，对应原 `appimage` / `desktop`。
- `arch.cpp` / `process.cpp` / `json.cpp` / `i18n.cpp` / `terminal_color.cpp`：底层工具，对应原 `fsutil` / `checksum`。
- SHA256 校验直接由 `appimage.cpp` 中的 `sha256_file` 提供，不再单独拆分 `checksum` 模块。

## 19. 工程约定与实现优化

### 19.1 超时与错误截断

- AppImage feed 下载使用 **120 秒超时**（`kFetchTextFeedTimeoutMs = 120000`），以处理约 825KB 的大 feed 文件
- 普通 HTTP 请求默认 **15 秒超时**（`kFetchTextDefaultTimeoutMs`），推测性抓取 **5 秒超时**（`kFetchTextSpeculativeTimeoutMs`）
- Feed 下载错误响应截断至 **500 字符**，防止终端输出被长错误信息淹没
- 并发 fallback 等待间隔 **3 秒**（`kFetchTextResolveParallelWaitMs`），fallback 超时 **5 秒**（`kFetchTextFallbackTimeoutMs`）
- 网站落地页下载最大 **512KB**（`kFetchTextLandingMaxBytes`），防止误下载 AppImage 文件体

### 19.2 并发控制

`install`/`download` 的 `--jobs` 与 `repo resolve` 的 `--concurrency` 是两套独立模型：

- **install/download 批处理**：默认 `min(CPU 核心数, 4)`，上限 32，受 `--jobs` 控制
- **repo resolve**：默认 `min(CPU 核心数, 8)`，`--aggressive` 模式 `min(2×CPU 核心数, 16)`，受 `--concurrency` 或 `--aggressive` 控制
- **Website Crawl**：固定并发 **4**（`kMaxWebsiteCrawlConcurrency = 4`），基于流水线模型，消除批次间等待

### 19.3 Website Crawl 流水线抓取

网站发现 AppImage 使用流水线（Pipeline-based）抓取模型：

- 维护 `kMaxWebsiteCrawlConcurrency = 4` 个并发 in-flight 请求
- 每个请求完成后**立即**处理结果并派发下一个排队 URL
- 消除了传统批处理模型中"慢请求阻塞同批次其他请求"的问题
- 使用 `FetchTask` 结构体封装抓取任务，`process_fetch_results()` 处理完成回调
- 单包最多检查 **96** 个页面（`kWebsiteMaxPages`），AppImage 候选软上限 **8** 个（`kWebsiteCandidateSoftCap`）
- 抓取深度上限 **5**（`kWebsiteMaxDepth`），用于支持 `homepage → download → version → platform` 等多层导航结构
- 下载相关 URL 采用启发式优先级队列，具体限制包括：
  - `kWebsiteHeadFillMax = 4`：初始种子 URL 的填充上限
  - `kWebsiteListingFollowNonStaleMax = 3`：从目录列表中跟随时效最近链接的上限
  - `kWebsiteListingFollowStaleMax = 1`：从目录列表中跟随陈旧链接的上限
- 下载 timeout 分为两档：推测性 `kFetchTextSpeculativeTimeoutMs = 5000` 与正常 `kFetchTextDefaultTimeoutMs = 15000`
- 并行 fallback 框架使用 `kFetchTextResolveParallelWaitMs = 3000` 的首次轮询间隔，超时后进入 `kFetchTextFallbackTimeoutMs = 5000` 的第二轮等待，最后阻塞等待所有 future 完成
- 落地页响应体最大 `kFetchTextLandingMaxBytes = 512KB`，防止误将 AppImage 文件体当作 HTML 解析

### 19.4 缓存策略

website_page 包源的解析结果通过 `yai repo resolve` 写入仓库索引，后续 install/download/upgrade 直接从索引中读取已记录的下载 URL，无需重复抓取官网。`--recrawl` 选项可强制绕过索引 URL，从原始包源重新解析。

### 19.5 回退解析链

包源解析遵循分层回退策略：

1. **主解析**：按 `source_type` 分派（`github_release` → GitHub API，`website_page` → Website Crawl，`direct_url` → 直接 URL）
2. **GitHub Release 失败回退**：并行尝试 AppImageHub catalog → `data/` → `apps/`，三者竞争，最先成功的结果被采用
3. **Website Crawl 失败回退**：同样并行尝试 AppImageHub catalog → `data/` → `apps/`
4. **Unavailable 包回退**：直接并行尝试 `data/` → `apps/`
5. **并行框架**：`resolve_parallel_fallback()` 统一封装多 fallback 并行竞争，先完成的成功结果胜出

### 19.6 AppImage GitHub 数据源

除 AppImageHub catalog 页面外，yai 还直接查询 AppImage 项目 GitHub 仓库的两个数据源（实现位于 [repo_appimage_github.cpp](file:///home/fsx/yai/src/repo_appimage_github.cpp)）。两者作为 catalog 页面的补充，提供更结构化的元数据。

#### `data/` 源解析

`data/<Name>` 文件（无扩展名，纯文本）为 AppImage 项目维护的"原始数据源"，通常一行 URL、注释行以 `#` 开头。`parse_appimage_data_entry()` 的处理流程：

1. 优先从 `raw.githubusercontent.com/AppImage/appimage.github.io/contents/data/<Name>` 拉取原文
2. 若 `raw.githubusercontent.com` 不可达，降级到 GitHub API `api.github.com/repos/AppImage/appimage.github.io/contents/data/<Name>`，返回 base64 编码内容并解码
3. 逐行解析：
   - 跳过空行
   - `#` 开头的行视为注释；若注释中包含 `http(s)://` URL，记录为 `comment_urls` 供后续补充
   - 非注释行首个 URL 记为 `primary_url`
4. 类型识别（按 `primary_url` 判定，注释 URL 作为补充）：
   - `.AppImage` 结尾、GitHub Release 资产、或含 `download`/`release` 关键词 → `direct_url`
   - 含 `github.com/owner/repo` 模式 → `github_repo`
   - 含 `gitlab.com/group/project` → `gitlab_project`；若 GitLab URL 本身指向 `.AppImage`，同时记为 `direct_url`
5. 若主 URL 没有提供有用信息，回退到 `comment_urls` 中寻找 GitHub 仓库或直接下载 URL
6. 若最后 `github_repo`、`direct_url`、`gitlab_project` 均为空，返回 `nullopt`（该条目视为不存在）

查找时先按包名原文查询，失败后再按包名小写查询（`lookup_appimage_entry` 模板），以兼容大小写不一致的条目。

#### `apps/` 源解析

`apps/<Name>.md` 文件为带 YAML frontmatter 的 Markdown，包含 AppImage 社区维护的应用元数据。`parse_appimage_apps_entry()` 的处理流程：

1. 通过 `fetch_apps_markdown()` 拉取 Markdown 文件内容（先尝试 `raw.githubusercontent.com`，失败回退到 GitHub API base64 解码）
2. `extract_yaml_frontmatter()` 提取 `---` 与 `---` 之间的 YAML 头
3. 使用 `parse_yaml_frontmatter()` 将 YAML 列表展开为带索引子键的扁平字典（如 `links.0.type`、`links.0.url`），保留原始结构
4. 遍历所有 `links.N.type`/`links.N.url` 配对：
   - `type=github` 或 `repository` → 用 `extract_github_repo()` 解析为 `github_repo`
   - `type=download` → 记为 `direct_url`
   - `type=web` 或 `homepage` → 记为 `homepage`
5. 回退扫描：若上一步未识别到 `github_repo` / `direct_url` / `homepage`，遍历所有含 `url`/`type` 键的字段，分别按 GitHub URL 模式、下载 URL 模式、web/homepage 类型匹配
6. 标量字段解析：
   - `description` → 摘要
   - `license` → 许可证
   - `desktop.X-AppImage-Arch` → 架构（如 `x86_64`、`aarch64`）
   - `desktop.X-AppImage-Version` → 版本号

Apps 条目解析完成后，通过 `merge_apps_entry_into_package()` 合并到已有的 `RepoPackage` 中，补充 feed/catalog 未提供的描述、许可证、架构和版本字段，并标记 `source_origin = "appimage_apps"`。

#### 解析失败回退

`data/` 与 `apps/` 在解析链中作为 GitHub Release 和 Website Crawl 的后级回退，`resolve_parallel_fallback()` 会让两者并行竞争，最先成功的结果被采用。

### 19.7 国际化（i18n）

- 所有面向用户的字符串通过 `tr()` / `tr_format()` 函数包裹
- 翻译文件存放在 `po/` 目录下（`en.po`、`zh.po`）
- `YAI_LANG=en` / `YAI_LANG=zh` 强制指定语言
- 未设置时按 `LC_ALL`、`LC_MESSAGES`、`LANG` 自动判断

## 20. MVP 范围

第一版只做必要闭环：

1. `yai install <url>`
2. 设置执行权限。
3. 直接运行失败时尝试 `APPIMAGE_EXTRACT_AND_RUN=1`。
4. 再失败时尝试预解压并运行 `AppRun`。
5. 生成 wrapper。
6. 生成 `.desktop` 文件。
7. `yai list`
8. `yai remove`
9. `yai doctor`

MVP 暂不做：

- 完整社区仓库。
- 自动更新所有 AppImage。
- 复杂依赖解析。
- GUI 前端。
- 系统级安装。

## 21. 后续阶段规划

### 阶段一：URL 安装器

验收标准：

- 可以从 URL 下载 AppImage。
- 可以自动 chmod。
- 可以生成 wrapper。
- 可以生成开始菜单入口。
- 可以卸载干净。

当前阶段一实现范围：

- `yai install <url>` 会通过 `curl` 下载 AppImage，支持 `file://`、HTTP(S) 等 curl 可处理的 URL。
- `yai install <path>` 支持本地 AppImage 文件路径，包括绝对路径、`./` / `../` 相对路径，以及当前目录中的 `*.AppImage` 文件。
- 本地 AppImage 安装不会走 `curl`，会直接复制到 `~/.local/share/yai/apps/<id>/current.AppImage`。
- 本地路径安装的 metadata 会记录 `source_kind=local_path`、`source_url=<absolute path>` 和 `download_url=<absolute path>`。

### 阶段二：运行修复

验收标准：

- 能识别常见 FUSE 错误。
- 能自动切换 `extract-and-run`。
- 能预解压到固定目录并运行。
- `yai doctor` 能给出清晰诊断。

当前阶段二实现范围：

- `yai install <url>` 安装后会用 `--appimage-version` 和短时间静默启动探测直接运行是否可用。
- 直接探测输出包含 FUSE / libfuse 相关错误时，会自动尝试 fallback 方案。
- 如果 `APPIMAGE_EXTRACT_AND_RUN=1` 探测成功，wrapper 会切换到 `extract_and_run` 模式。
- 如果仍失败，会尝试 `--appimage-extract`，将 `squashfs-root` 移动到 `~/.local/share/yai/apps/<id>/extracted/`，并让 wrapper 执行其中的 `AppRun`。
- `yai repair <id>` 会重新 chmod、重新探测运行模式、重写 wrapper、重写基于上游安全元数据的 `.desktop` 和 metadata。
- `yai doctor` 会检查 `PATH`、`curl`、`update-desktop-database`、`/dev/fuse` 和已安装应用的关键文件。

阶段二暂不做：

- 不启动 GUI 应用做完整运行测试，避免安装过程拉起窗口。
- 不自动执行 `sudo apt install` / `sudo dnf install` 安装 FUSE。
- 不解析 AppImage 内部 `.desktop` 的 `Exec` 作为入口，预解压模式只使用 `AppRun`。

### 阶段三：GitHub Release 支持

验收标准：

- 能从 `owner/repo` 查询最新 release。
- 能按架构选择 AppImage asset。
- 能支持直连和代理下载。
- 能记录原始 URL 与实际下载 URL。

当前阶段三实现范围：

- `yai install owner/repo` 会请求 GitHub latest release API：`/repos/<owner>/<repo>/releases/latest`。
- 可通过环境变量 `YAI_GITHUB_API_BASE` 覆盖 API 根地址，便于测试或接入兼容代理。
- release asset 选择只关注 `browser_download_url` 中的 `.AppImage` 文件。
- 当前架构默认通过 `uname -m` 获取，也可通过 `yai install ... --arch <arch>` 指定目标 AppImage 资产架构。
- `--arch auto` 使用当前机器架构。
- 支持的规范架构为 `x86_64`、`aarch64`、`x86`、`armv7`、`riscv64`、`ppc64le`、`s390x`、`loongarch64`。
- 常见别名会归一化：`amd64` / `x64` -> `x86_64`，`arm64` -> `aarch64`，`armhf` / `armv7l` -> `armv7`，`ppc64el` / `powerpc64le` -> `ppc64le`。
- `--arch` 只影响下载资产选择，不保证宿主系统可以运行非本机架构 AppImage。
- metadata 中的 `arch` 记录安装时选择的 AppImage 资产架构。
- `--download direct` 只使用原始 asset URL。
- `--download mirror_first --mirror-template <template>` 先尝试代理 URL，失败后回退原始 URL。
- `--download direct_first --mirror-template <template>` 先尝试原始 URL，失败后尝试代理 URL。
- 代理模板支持 `{url}`、`{raw_url}`、`{raw_url_noscheme}`、`{owner}`、`{repo}`、`{tag}`、`{asset}`。
- metadata 会记录 `source_kind`、`version`、`source_url`、`download_url`、`github_owner`、`github_repo`、`github_asset`。

阶段三暂不做：

- 不解析完整 GitHub release JSON 语义，只提取阶段三需要的字符串字段。
- 不处理 GitHub API 分页，因为 latest release 单次响应已包含当前 release 的 assets。
- 不自动配置具体加速代理站点，代理必须由用户或后续配置文件提供。
- 不做 SHA256 校验；校验和签名验证放到后续安全增强阶段。

### 阶段四：包源仓库

验收标准：

- `yai search` 可用。
- `yai info` 可用。
- `yai install <package>` 可用。
- 仓库索引格式稳定。

当前阶段四实现范围：

- 仓库索引使用 schema v1 JSON 格式，默认读取 `~/.local/share/yai/repos/index.json`。
- 可通过环境变量 `YAI_REPO_INDEX` 指定本地索引路径或 URL，便于测试和临时包源接入。
- `yai search <keyword>` 会按包 `id`、`name`、`summary` 做大小写不敏感搜索，并在结果中缩短过长摘要。
- `yai info <package>` 会展示包基础信息、主页、许可证、GitHub Release 来源和 asset 匹配规则。
- `yai install <package>` 会从索引查找包，并复用阶段三 GitHub latest release 解析、架构选择、代理下载、运行模式探测和 metadata 写入流程。
- 当前包源支持 `source.type = "github_release"`、`"direct_url"`、`"website_page"` 和 `"unavailable"`；`github_release` 支持可选 `asset_pattern` 正则过滤 release asset。
- metadata 会用 `source_kind=repo_github_release` 标记来自仓库索引的安装。
- `yai repo add <name> [url-or-path]` 会保存命名包源，缓存其 JSON 文件，并重建默认 `index.json`；省略 URL/path 目前只支持内置 `appimage` 包源。
- `yai repo list` 会列出已配置包源、源地址和本地缓存状态。
- `yai repo update` 会刷新所有已配置包源并合并生成 `~/.local/share/yai/repos/index.json`。
- `yai repo update <name>` 会只刷新指定包源，再用所有已缓存包源合并生成 `~/.local/share/yai/repos/index.json`。
- `yai repo remove <name>` 或模式参数会移除已配置包源、删除对应缓存 JSON，并重建 `index.json`；不会卸载已安装应用。
- 包源配置保存在 `~/.local/share/yai/repos/repos.conf`，每个包源缓存为 `~/.local/share/yai/repos/<name>.json`。
- `yai repo add appimage` 会使用内置地址 `https://appimage.github.io/feed.json`，直接接入 AppImage 项目公开 feed。
- 导入 AppImage feed 时，`description` 会提取为短 `summary`，不会把完整长描述写入仓库索引。
- 读取 AppImage feed 时，会把包含 GitHub 链接的条目转换为 yai schema v1 的 `github_release` 包源。
- 读取 AppImage feed 时，包含直接 `.AppImage` 下载链接的条目会转换为 `direct_url` 包源。
- 读取 AppImage feed 时，缺少 GitHub 和直接下载链接但有官网链接的条目会转换为 `website_page` 包源，安装时会在官网的下载相关页面中搜索 AppImage；搜索可能并行抓取少量同站页面，并使用较短超时以便死链快速失败，探测 HTML 落地页时不会下载完整 AppImage 文件体。
- 如果 feed 给出的 `website_page` 实际是 AppImageHub/AppImage catalog 详情页，而不是真实项目官网，官网搜索只允许从该 catalog 页跟随一个明显匹配当前包名的链接进入项目官网；进入后再把该官网域名作为允许域继续搜索下载页。
- 官网搜索只跟随当前包官网、同站下载页、内置可信下载提示域，以及上述 catalog 到官网的跳转；普通 GitHub 页面、Reddit、论坛、Discord、Twitter/X、Facebook、YouTube 等非官方下载页面一律跳过。
- `.AppImage` 候选文件必须来自允许域，或 URL / 文件名明显匹配当前包名；其它项目域名上的 AppImage 不作为候选。
- 官网搜索在交互式终端中使用单行状态覆盖过时进度；stderr 被重定向时只输出开始和最终摘要，避免打印大量 URL 明细。
- 读取 AppImage feed 时，没有任何来源链接的条目会作为 `unavailable` 目录条目保留，`search/info` 可见但 `install` 会说明缺少可安装来源。

阶段四暂不做：

- 不实现完整 JSON 语义解析，只解析阶段四 schema v1 需要的字段。
- 不实现社区仓库同步、签名校验、SHA256 校验或包源审核。
- 不支持依赖解析、分类浏览、分页搜索或多仓库优先级。

### 阶段五：更新和回滚

验收标准：

- `yai update [id]` 可用，且只做更新预览。
- `yai upgrade <id>` 可用。
- `yai upgrade --all` 可用，默认要求确认。
- 不可升级原因作为预览输出的独立状态和原因列。
- 升级失败不破坏旧版本。
- 保留至少一个可回滚版本。

当前阶段五实现范围：

- `yai update [id]` 会查询 GitHub latest release 并输出预览结果；对 repo `direct_url` 和 `website_page` 安装会重新解析当前 repo index 并比较 AppImage 文件名或 URL；对 `repo_direct_url` 在 index URL 不变时会探测 HTTP 校验头或 `file://` 文件大小；对 `source_kind=url` 的普通 URL 直装使用记录的 `source_url` / `download_url` 做 freshness 探测，必要时标记 `download verification required` 但不下载正文；对 `source_kind=local_path` 输出 `unsupported`。
- `yai upgrade <id>` 支持升级通过 GitHub Release 安装的应用，包括直接 `owner/repo` 安装和来自阶段四包源索引的 `repo_github_release` 安装；也支持升级 repo `direct_url` 和 `website_page` 安装，只要当前 repo index 解析出的候选 AppImage 发生变化；也支持 `source_kind=url` 和 same-URL `repo_direct_url` 安装，通过重新下载并比较 sha256 确认内容变化。
- `yai upgrade --all` 会复用 `yai update` 的预览结果，只升级 `upgradable` 项；执行前默认提示确认，除非传入 `--yes` 或 `-y`。
- 批量升级遇到单个包失败时继续尝试剩余可升级包，最后汇总失败数量并返回失败。
- 升级时先下载到应用目录内的 `update.AppImage`，并先完成运行模式探测；探测失败不会替换当前版本。
- 升级提交前会把当前 `current.AppImage` 和 `metadata.json` 保存到 `versions/previous/`，至少保留一个可回滚版本。
- 升级提交阶段如果替换 AppImage、解压目录、wrapper、desktop 或 metadata 时失败，会尝试自动恢复 `versions/previous/`。
- `yai rollback <id>` 会恢复 `versions/previous/current.AppImage` 和 `versions/previous/metadata.json`，然后复用 repair 流程重新生成 wrapper、desktop 和运行模式 metadata。
- `yai upgrade` 支持阶段三已有的 `--download` 和 `--mirror-template` 下载策略。
- `yai upgrade` 默认沿用已安装 metadata 中的 `arch` 选择新版 AppImage asset。

阶段五暂不做：

- 不实现完整版本比较，只比较 GitHub latest release 的 tag 是否和本地 `version` 相同。
- 不保留多历史版本，只保留最近一次更新前的 `previous`。
- 不对 `source_kind=local_path` 安装做升级。
- 不实现 release digest 或 `.sha256` sidecar 的可信校验；URL 升级路径使用安装 metadata 中的 sha256 比较。

### 阶段六：GUI 或 TUI

验收标准：

- 可以浏览应用。
- 可以安装、更新、卸载。
- 可以查看修复建议。
- GUI/TUI 调用同一套核心库，不复制业务逻辑。

## 22. 测试计划

### 22.1 单元测试

- GitHub URL 解析。
- 代理 URL 模板替换。
- 包元数据解析。
- `.desktop` 字段转义。
- wrapper 生成。
- 安装状态读写。
- 版本比较。
- 架构归一化与 asset 评分。
- `repo resolve` 并发计算（`calculate_default_concurrency` 普通模式/aggressive 模式/手动模式）。
- Website Crawl 流水线调度（`FetchTask` + `process_fetch_results`）。
- 并行 fallback 框架（`resolve_parallel_fallback`）竞争逻辑。
- AppImageHub catalog 页面解析（`fetch_appimage_catalog_sources`）。
- AppImage GitHub `data/` 和 `apps/` 数据源解析。
- 不可用包（unavailable）回退解析链。
- 下载器链式回退（auto → curl/wget/wget2/aria2c 降级）。
- aria2 RPC 进度解析与合并。
- 进度条渲染（单包/批量 TTY 进度）。
- 安全：GitHub blocklist 451 检查。
- i18n：`tr()` / `tr_format()` PO 文件加载与回退。

### 22.2 集成测试

- 从本地 HTTP 测试服务器下载 AppImage 样例。
- 模拟下载中断并断点续传。
- 模拟哈希不匹配。
- 模拟缺少执行权限。
- 模拟 FUSE 失败输出。
- 模拟 `--appimage-extract` 生成目录。
- 安装后检查 wrapper、desktop、icon、state 是否一致。
- 卸载后检查 yai 管理文件是否清理。
- `repo resolve`：多包多架构并发解析，验证结果写入索引。
- `repo resolve`：`--aggressive` 模式并发数验证。
- `repo resolve`：`--concurrency 1` 串行模式验证。
- `repo resolve`：`--output` 输出到指定路径。
- `repo resolve`：`--overwrite` 强制覆盖已有 URL。
- `repo remove`：通配符模式匹配与批量删除。
- 回退解析链：GitHub Release 失败 → AppImageHub catalog 回退 → data/ 回退 → apps/ 回退。
- 回退解析链：Website Crawl 失败 → 并行 fallback 竞争。
- 不可用包（unavailable）安装：data/ 和 apps/ 并行回退。
- Feed 下载 120 秒超时：模拟慢网络验证超时行为。
- Feed 下载错误截断：验证错误响应截断至 500 字符。
- aria2c 下载：RPC 进度上报、分片完成量读取。
- 多语言：`YAI_LANG=en` / `YAI_LANG=zh` 切换验证。
- 批量安装/下载：`--jobs` 并发与顺序退化（通配符展开）。

### 22.3 手工测试

覆盖发行版：

- Ubuntu 24.04
- Debian stable
- Fedora 最新稳定版
- Arch Linux
- openSUSE Tumbleweed

覆盖桌面环境：

- GNOME
- KDE Plasma
- XFCE

## 23. 关键风险

### 23.1 加速代理不稳定

风险：

- 代理站点失效。
- 下载速度波动。
- 文件被替换或污染。

缓解：

- 代理可配置。
- 自动回退直连。
- 强制记录原始 URL。
- 优先使用上游哈希校验。

### 23.2 AppImage 格式和运行时差异

风险：

- 新旧 AppImage 行为不同。
- 部分应用不支持 `extract-and-run`。
- 解压后入口不标准。

缓解：

- 多层运行策略。
- 对每个应用记录实际可用模式。
- `repair` 可重新探测。

### 23.3 桌面环境差异

风险：

- 不同桌面对 `.desktop` 缓存刷新行为不同。
- 图标加载规则不同。

缓解：

- 遵循 freedesktop 规范。
- 写入用户级标准目录。
- 刷新失败只警告，不中断安装。

### 23.4 安全和供应链

风险：

- AppImage 本身可能是恶意程序。
- 第三方代理可能篡改文件。
- 社区包源可能提交恶意元数据。

缓解：

- 包源审核。
- 校验哈希。
- `.desktop` 和 wrapper 由 yai 生成。
- 对未知校验状态给出明显提示。

## 24. 参考资料

- AppImage FUSE 与解压运行说明：<https://docs.appimage.org/user-guide/troubleshooting/fuse.html>
- AppImage 用户指南：<https://docs.appimage.org/user-guide/>
- freedesktop Desktop Entry Specification：<https://specifications.freedesktop.org/desktop-entry/latest-single/>
- GitHub REST API Releases：<https://docs.github.com/en/rest/releases/releases>
