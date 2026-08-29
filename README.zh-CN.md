# yai — AppImage 包管理器

[![Smoke tests](https://img.shields.io/badge/%E6%B5%8B%E8%AF%95-22%20%2F%2022-brightgreen)](#%E6%B5%8B%E8%AF%95)
[![Language](https://img.shields.io/badge/C%2B%2B-17-blue)](#%E7%BC%96%E8%AF%91)
[![Platform](https://img.shields.io/badge/Linux-64bit%20%7C%2032bit%20%7C%20ARM%20%7C%20RISC--V%20%7C%20LoongArch-blue)](#%E6%94%AF%E6%8C%81%E6%9E%B6%E6%9E%84)
[![Repo](https://img.shields.io/badge/仓库-GitHub-yai--byte%2Fyai--repo-blueviolet)](https://github.com/yai-byte/yai-repo)
[![Mirror](https://img.shields.io/badge/%E9%95%9C%E5%83%8F-Gitee--no11no16%2Fyai--repo-c71d23)](https://gitee.com/no11no16/yai-repo)

**yai** 是一个用标准 C++17 编写、依赖极少、单文件可执行的 **AppImage 包管理器**。
它负责 AppImage 应用的安装、更新预览、升级、回滚、自修复，并能够从 GitHub Release、
自定义 JSON 仓库索引、AppImageHub 目录源，甚至在找不到发布页时从**官方站点自动爬取**下载地址。

- 🔎 **多源并行解析** —— GitHub Release API + 自定义 JSON 仓库 + AppImageHub 目录源
  + 官方网站启发式爬取，多路同时竞争；任一源暂时失效都不会阻塞安装。
- 🚀 **批量并行安装** —— 一次装多个包，每个任务独立流式日志，交互终端下带
  粘性进度底部条。
- 🔄 **严格的升级语义** —— HTTP ETag / Last-Modified / Content-Length 校验，
  在不确定时始终走 `Unknown` 保守路径，**绝不因为文件长度相同就静默认为没变**。
- 🪞 **镜像友好** —— 内置多个 GitHub Release 代理（`ghfast`、`fastgit`、`llkk` 等）
  以及自定义 URL 模板，国内用户也能顺畅获取 GitHub 上发布的资源。
- 🛡 **可原子回滚的安装** —— 每次提交都会写入一份完整的 JSON 元数据记录，
  回滚会原子地把 `current.AppImage` 与上一个版本的快照互换。
- 🧭 **自动运行模式探测** —— 安装后自动 `--appimage-version` 探测，遇到
  FUSE/libfuse 缺失或沙箱限制会透明地切换到 `extract_and_run` 或预解压模式。
- 🌏 **国际化** —— 命令行输出支持英文与简体中文，默认跟随 `LANG` / `LC_ALL`，
  也可通过 `YAI_LANG` 环境变量强制指定。

---

## 目录

1. [快速开始](#快速开始)
2. [编译](#编译)
3. [安装应用](#安装应用)
4. [命令参考](#命令参考)
5. [更新与升级语义](#更新与升级语义)
6. [仓库管理](#仓库管理)
7. [官方站点发现](#官方站点发现)
8. [镜像与下载器](#镜像与下载器)
9. [文件系统布局](#文件系统布局)
10. [运行模式](#运行模式)
11. [本地化](#本地化)
12. [仓库索引 Schema](#仓库索引-schema)
13. [测试](#测试)
14. [免责声明](#免责声明)
15. [许可证](#许可证)

---

## 快速开始

```bash
# 1. 编译出单个 yai 可执行文件
make

# 2. 添加官方软件索引（GitHub/Gitee 两源任选其一）
./yai repo add appimage https://github.com/yai-byte/yai-repo/raw/main/index.json
#  若 GitHub 访问较慢，可改用 Gitee 镜像：
# ./yai repo add appimage https://gitee.com/no11no16/yai-repo/raw/main/index.json
./yai repo update appimage

# 3. 浏览并安装
./yai search editor
./yai install obs-studio
./yai install --arch aarch64 applepie

# 4. 直接从 GitHub 仓库最新 Release 装
./yai install AppImage/AppImageKit

# 5. 直接给任意 AppImage URL
./yai install https://example.com/releases/demo-x86_64.AppImage

# 6. 保持已安装应用最新
./yai update
./yai upgrade --all --yes
```

比如 `./yai install obs-studio` 之后，一个 `obs-studio` 包装脚本会落到
`~/.local/bin`（请把它加进 `PATH`），同时 `yai-obs-studio.desktop` 会写到
`~/.local/share/applications`，桌面环境的菜单里就能直接看到了。

---

## 编译

### 依赖

- 支持 C++17 的编译器（GCC ≥ 8，Clang ≥ 6）
- GNU `make`
- `curl`（运行期必需；GitHub API 查询和网站探测也会用到）
- 可选（加速下载）：`aria2c`、`wget2` 或 `wget`

### 构建

```bash
make          # 生成 ./yai 和 libyai.a
make clean    # 清理目标文件、静态库和二进制
```

默认的 `CXXFLAGS` 使用 `-O2 -Wall -Wextra -Wpedantic`，必要时可覆盖：

```bash
CXX=clang++ make
CXXFLAGS="-std=c++17 -O0 -ggdb -pthread" make
```

### 安装

yai 是纯单文件二进制，**没有系统安装步骤**。把它拷到 `PATH` 任意位置即可，比如：

```bash
install -m 0755 yai ~/.local/bin/yai
```

---

## 安装应用

`yai install` 接受多种形式的安装目标：

| 目标形式              | 示例                                           | 解析路径                                                 |
| --------------------- | ---------------------------------------------- | -------------------------------------------------------- |
| 仓库包 id             | `obs-studio`                                   | 合并后的本地索引中查找，按对应 source kind 解析          |
| `owner/repo`          | `AppImage/AppImageKit`                         | GitHub 最新 Release，按架构挑最合适的 AppImage asset     |
| HTTP/HTTPS URL        | `https://example.com/foo-x86_64.AppImage`      | 直接拉取，后续用 URL 新鲜度策略升级                      |
| `file://` URL         | `file:///tmp/release/demo.AppImage`            | 直接本地拷贝，用文件大小做新鲜度探测                     |
| 本地路径              | `./build/my.AppImage`                          | 复制文件并按文件名主干尝试匹配仓库包                     |

所有安装类目标共用这些通用参数：

```
--arch  auto|x86_64|aarch64|x86|armv7|riscv64|ppc64le|s390x|loongarch64
--download  direct|mirror_first|direct_first
--mirror-template  <URL 模板>
--downloader  auto|curl|wget|wget2|aria2c
--jobs  <1..32>
--id  <id>    --name  <显示名>   仅适用于单目标 install
```

显式传多个目标（例如 `yai install foo bar baz`）时，默认用大小为 4 的工作池并行执行；
传 `--jobs 1` 就是纯串行。

### 通配符目标

包 id、仓库名、已安装 id 都支持 shell 风格的 `*` / `?` 通配符 —— 一定记得加引号，
避免 shell 先替你展开：

```bash
./yai update 'obs*'     # 预览所有以 obs 开头的 id 的升级情况
./yai remove 'kde-*'    # 按模式批量删除（多个匹配时会交互确认）
./yai upgrade 'kde-*' -y  # 传 --yes / -y 可跳过交互确认
```

通配符展开得到的批次会**串行执行**，首个失败即停；**显式枚举**的多个目标默认并行。
零匹配仍然视作失败返回非零。

---

## 命令参考

```text
用法: yai <命令> [参数...] [选项]

查询与搜索
  search <关键字>                   列出 id、名称、简介里命中的包
  info <包名>                       展示合并索引中该包的完整详情
  list                              列出所有已安装应用、版本与运行模式
  doctor                            体检：FUSE、架构、包装脚本完整性等

仓库管理
  repo list                         列出已配置仓库与缓存状态
  repo add <名字> [URL 或路径]      注册一个 JSON 索引或 AppImageHub Feed
  repo update [名字或模式]          拉取最新索引，重建合并 index.json
  repo remove <名字或模式>          移除仓库条目 + 其缓存（不会卸载应用）
  repo resolve [--overwrite]        预先解析全量包下载 URL 并写入 overlay

镜像与网络
  mirror list                       列出所有内置 / 自定义 GitHub 代理策略
  mirror use <名称>                 启用某个内置镜像策略
  mirror custom <模板>              注册自定义镜像 URL 模板
  mirror off                        关闭代理，直接使用上游 GitHub 原始地址

下载 / 安装生命周期
  download <目标>...                仅下载 AppImage 到当前目录，不安装
  install  <目标>...                下载、探测、包装脚本、元数据、desktop 一步到位
  update   [id 或模式]              升级预览（不会真的下载 AppImage 主体）
  upgrade  <id|--all> [--yes]      实际下载、原子提交、写入回滚快照
  rollback <id>                     把 current.AppImage 切回上一个版本快照
  repair   <id>                     重新探测、原地重装 wrapper 与 desktop entry
  remove   <id 或模式>              删除 wrapper/desktop entry/以及 yai 应用目录
```

### `download` 的行为

`download` **从不安装**。它把 AppImage 下载到**当前工作目录**，命名为
`{解析出的包 id}.AppImage`，并且完全跳过 chmod / 探测 / 元数据 / wrapper / desktop entry
等步骤。本地默认 id 还会去除尾部的版本号与架构标记，例如
`Demo-v1.2.3-x86_64.AppImage` 会变成 `demo.AppImage`。

---

## 更新与升级语义

`update` 是**只读预览**：从不下载 AppImage 主体、从不探测二进制、从不写元数据、
从不替换已安装文件。不传 id 时预览全部；传 id 或模式时预览匹配项。

`upgrade` 是**会落盘的变更**命令，流程如下：

1. 执行与 `update` 完全一致的预览逻辑；
2. 筛选出状态为 `upgradable` 的行；
3. 交互确认（`--yes` / `-y` 可跳过）；
4. 将候选包逐个下载到 `update.AppImage` 临时槽位；
5. 用 `--appimage-version` 探测新包可运行性；
6. 原子提交：新的 AppImage 正式成为 `current.AppImage`，旧版本落盘到
   `versions/previous/` 作为回滚快照；
7. 重写 wrapper 与 desktop entry；
8. 写入新的 JSON 元数据条目，并刷新仓库 overlay，让后续 `upgrade --all`
   对 GitHub Release 类的包可以走**索引快路径**。

### 不同 source kind 的升级规则

| Source kind            | 预览 (`update`) 的判定策略                                      |
| ---------------------- | --------------------------------------------------------------- |
| `github_release`       | 对比最新 Release tag + asset 与已安装元数据                     |
| `repo_github_release`  | 先查 overlay 缓存中的下载 URL，不命中再走活 API                 |
| `repo_direct_url`      | 索引 URL 变了就算升级；URL 相同再走 HTTP validator 对比         |
| `repo_website_page`    | **一定重新爬官方站点**（详见下一节）                            |
| `url`                  | 探测 HTTP validator；歧义时报告 `需要下载验证`                  |
| `direct_url`（本地）   | 与记录在 metadata 中的 sha256 / 文件大小对比                    |

> **一致性保证**：仅凭 `Content-Length` 相同**永远不会**断言资源 `Unchanged`。
> 缺少 ETag / Last-Modified 时一定落入 `Unknown` 保守分支，让升级路径真的去
> 校验字节。这对应 `docs/superpowers/specs/` 中的"§13 校验器语义"设计笔记。

---

## 仓库管理

仓库本质上是 JSON Feed 文件（或 AppImageHub 风格的目录页），登记在
`~/.local/share/yai/repos/repos.conf`。每次 `repo add` 会记录一条「名字 → URL / 路径」
映射，并把下载下来的内容缓存在 `~/.local/share/yai/repos/<name>.json`；随后所有
仓库缓存被合并成 `~/.local/share/yai/repos/index.json`，这是 search / info / install /
update 真正使用的权威合并索引。

也可以在运行期覆盖合并索引位置：

- `YAI_REPO_INDEX` —— 设置后替代默认本地合并索引，接受**本地路径**或**远程
  `http(s)://` URL**（每次命令都会拉一次）。
- `repo add appimage` 不传 URL 时，会拉取众所周知的 AppImageHub 目录 Feed，并把
  条目转换成 `github_release` / `direct_url` / `website_page` 三类包。

### 远端索引的地区自动探测

yai 自带两个官方远端索引：GitHub 源
`https://github.com/yai-byte/yai-repo/raw/main/index.json` 以及 Gitee 镜像
`https://gitee.com/no11no16/yai-repo/raw/main/index.json`。每次启动时
`detect_index_region()` 会用很短的超时同时探测两边，并自动挑更快的那一个，
所以大陆用户不用手动改也能用得很顺。

---

## 官方站点发现

当某个包被声明为 `website_page` 源 —— 这种情况常见于从 AppImageHub Feed 导入、
只有官网链接、既没有 GitHub Release 也没有直接 `.AppImage` URL 的条目 —— yai
会运行一个**有边界的并行爬虫**，在不让你流量乱跑的前提下把下载地址找出来。

### 主机边界

爬虫只会跟随满足**至少一条**的链接：

- 与声明的主页同 host（例如 `musescore.org`）；
- 主页的子域名 / 兄弟域名，且域名中包含与包名相匹配的特征 token；
- 可信下载提示站点（`sourceforge.net`、`github.com/.../releases`、
  `gitlab.com/.../-/releases`、`codeberg.org`、`launchpad.net` 等）；
- 目录页 → 官方站的一次桥接跳转（比如从 `appimage.github.io/X` 目录条目里的
  链接跳到真正的项目站）。

**绝对不会**进入社区聊天站、论坛、社交平台、通用 CDN、以及与当前项目无关的
其它域名；普通 GitHub 源代码页（非 release 页）也一律跳过，保证爬取页数可控。

### 抓取引擎

- 先用 `--max-time 5` 做**投机性探测页请求**，并行查一堆常见下载路径
  （`/download`、`/downloads`、`/en/download`、`/linux/download`、`/releases`、
  `/platforms` 等），只把返回 2xx 且页面里确实存在 AppImage 链接、或者带
  `.AppImage` 目标的表单 / 隐藏域的页升成正式候选。
- 用**优先队列**打分：URL token、包名重叠度、host 距离等，命中数到封顶阈值即停。
- 发现阶段**绝不下载大体量文件**：疑似 AppImage 的链接一律用带
  `--max-filesize` 的 range 请求（首 512 KiB）校验一下头，避免把误判的
  200 MiB 大二进制拉回来撑爆带宽。

---

## 镜像与下载器

### 内置 GitHub Release 代理

`yai mirror list` 会列出全部预置策略。交互终端下每个 GitHub 下载还会单次提示
"是否使用国内加速"，也可以用 `mirror use` 直接永久锁定：

```bash
./yai mirror use ghfast
./yai mirror use chengc
./yai mirror use fastgit
./yai mirror use yylx
./yai mirror use llkk
./yai mirror custom 'https://mirror.example.com/gh/{owner}/{repo}/releases/download/{tag}/{asset}'
./yai mirror off
```

镜像模板支持占位符：`{url}`、`{raw_url}`、`{raw_url_noscheme}`、`{owner}`、
`{repo}`、`{tag}`、`{asset}`。

### 下载器选择

`--downloader auto`（默认）按如下顺序尝试本机安装了的 HTTP 下载器：
`aria2c` → `wget2` → `wget` → `curl`。auto 选中的某个下载器一旦传输失败，
自动退到下一个；`file://` 源在 auto 模式下永远选 `curl`，保持本地发布与
测试夹具的行为可预测。

对非 `curl` 的下载器，yai 会在真正传输前先用 `curl` 做一次尽力而为的 HTTP 头
探测，这样进度条仍然能显示总大小和即时速度，即使下载器本身不输出原生进度。
对 `aria2c`，yai 还会去连 aria2 的本地 JSON-RPC 套接字读 `completedLength`，
避免多连接稀疏写入把"已下载字节数"虚高。

---

## 文件系统布局

yai 只会写到**当前用户**的 HOME 目录下，不写全局路径、不加 `setuid`、
不加载内核模块。

| 路径（相对 `$HOME`）                                               | 作用                                     |
| ------------------------------------------------------------------ | ---------------------------------------- |
| `.local/bin/<id>`                                                  | 调用正确运行模式的 shell 包装脚本        |
| `.local/share/applications/yai-<id>.desktop`                       | 出现在桌面菜单里的 desktop entry         |
| `.local/share/yai/apps/<id>/current.AppImage`                      | 实际安装的 AppImage 二进制               |
| `.local/share/yai/apps/<id>/metadata.json`                         | JSON 源信息 / sha256 / 运行模式等元数据  |
| `.local/share/yai/apps/<id>/current.AppImage.headers`              | 捕获的 HTTP 头，供后续新鲜度探测复用     |
| `.local/share/yai/apps/<id>/versions/previous/current.AppImage`    | 回滚快照（只保留上一次升级之前的版本）   |
| `.local/share/yai/apps/<id>/extracted/`                            | 预解压出的 FUSE 树                       |
| `.local/share/yai/repos/repos.conf`                                | 已注册仓库列表                           |
| `.local/share/yai/repos/<name>.json`                               | 各仓库的缓存索引                         |
| `.local/share/yai/repos/index.json`                                | 合并后的最终索引                         |
| `.config/yai/mirror.conf`                                          | 持久化的镜像策略                         |
| `.config/yai/github_blocklist.conf`                                | 每行一个 `owner/repo`，命中以 451 拒绝   |

安装元数据**固定使用 JSON** (`metadata.json`)；老版本遗留的 `metadata.conf`
格式已经不再读取。执行 `yai repair <id>` 会把遗落的旧 `metadata.conf` 一并清理掉。

---

## 运行模式

安装 / 升级 / 修复后，yai 会用 `--appimage-version` 和一次短时间的真实启动
来探测该 AppImage 的运行可行性。如果输出提示 FUSE / libfuse / 沙箱问题，
yai 会自动改写 wrapper，切换到最合适的降级模式：

| 模式                | 行为                                                                      |
| ------------------- | ------------------------------------------------------------------------- |
| `direct`            | 直接执行 `current.AppImage`（探测通过时的默认模式）                       |
| `extract_and_run`   | `APPIMAGE_EXTRACT_AND_RUN=1 ./current.AppImage` —— 每次启动都临时解压     |
| `extracted`         | 一次性预解压到 `extracted/`，以后直接跑 `extracted/AppRun`                |

任何时候都可以用 `yai repair <id>` 重新探测并刷新 wrapper。

---

## 本地化

yai 的中英翻译分别在 `po/en.po` 和 `po/zh.po`。默认依次读取 `LC_ALL`、
`LC_MESSAGES`、`LANG`：任何中文语言变种就输出中文，其余全回落到英文。
要强制覆盖系统设置，直接设环境变量：

```bash
YAI_LANG=zh yai install obs-studio   # 强制中文
YAI_LANG=en yai update               # 写脚本时用英文获得稳定输出
```

包 id、文件路径、JSON 配置值，以及 `search` / `list` 的列表列形状，在两种语言
下都**故意保持一致**，方便脚本跨语言解析。

---

## 仓库索引 Schema

所有 `yai-repo` 产出、`repo add` 接受的仓库 JSON 都沿用**第 1 版**索引 Schema。
最小示例：

```json
{
  "schema_version": 1,
  "updated_at": "2026-08-01T00:00:00Z",
  "packages": [
    {
      "id": "demo",
      "name": "Demo",
      "summary": "演示包",
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
      "summary": "只在官网发布的应用",
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

支持的 `source.type`：`github_release`、`direct_url`、`website_page`。对
`github_release` 还可以给 `asset_pattern` 正则，不给就按架构用内置的默认匹配；
对 `direct_url` 则直接填 `url` 字段，支持 `http(s)://` 与 `file://`。

---

## 测试

`tests/` 目录里放着 22 个 hermetic（封闭可复现）的 shell 冒烟用例，覆盖所有
用户态命令、网站爬虫、校验器新鲜度、镜像策略、JSON 解析器等。脚本都用
`fake-bin` 里的假 `curl` 拦截网络请求，所以**跑套件时不会有任何真实外网访问**。

```bash
make
# 跑全量：
for f in tests/*_smoke.sh; do bash "$f" || echo "FAIL: $f"; done
```

当前版本的全部 22 条 smoke 用例（全部通过）：

| 用例文件                          | 覆盖内容                                                    |
| --------------------------------- | ----------------------------------------------------------- |
| `arch_smoke.sh`                   | 架构名标准化 + 自动探测                                     |
| `json_smoke.sh`                   | metadata / repo / GitHub 各类 JSON 形状的扫描往返           |
| `progress_smoke.sh`               | 单任务下载进度行格式                                        |
| `batch_progress_smoke.sh`         | 并行批次粘性 footer + 任务前缀日志                          |
| `download_smoke.sh`               | 直链 / URL / owner/repo 下载，不含安装副作用                |
| `metadata_json_smoke.sh`          | 安装元数据 JSON 形状 + repair 清理旧 metadata.conf          |
| `url_freshness_smoke.sh`          | ETag / LM / CL 校验器矩阵                                   |
| `url_update_smoke.sh`             | 同 URL 同大小重写 → Unknown 保守路径                        |
| `fetch_text_timeout_smoke.sh`     | 挂住的网站在超时策略下安装耗时稳定 < 20s                    |
| `website_mtime_smoke.sh`          | 列表页优先级 + HTTP-Equiv / Last-Mod 启发                   |
| `repo_smoke.sh`                   | repo add / update / remove / list + 通配符                  |
| `repo_resolve_index_smoke.sh`     | `repo resolve` overlay 写入 + 快路径 URL                    |
| `appimage_apps_index_smoke.sh`    | AppImageHub apps 索引 Markdown 解析                         |
| `appimage_data_resolve_smoke.sh`  | AppImageHub `data/<name>` 条目 + GitHub API 回退            |
| `appimage_feed_smoke.sh`          | 目录页 → 官方站桥接 + 装/更新/升级到 v2 全流程              |
| `mirror_policy_smoke.sh`          | 镜像模板 + ghfast / fastgit 代理解析                        |
| `wildcard_multi_smoke.sh`         | 通配符多匹配交互确认 + 零匹配失败                           |
| `stage1_smoke.sh` … `stage5_smoke.sh` | 端到端 install/update/upgrade/rollback/repair 主流程    |
| `stage4_smoke.sh` 额外覆盖非交互日志捕获中的 ANSI 转义剥离处理 |

---

## 免责声明

- yai 是纯客户端工具，**不托管也不重新分发任何二进制文件**；所有仓库条目
  都直接指向上游项目的原始地址。
- 镜像 / 代理模板只是把 GitHub Release asset 的下载请求转接到第三方服务器；
  yai 与这些第三方服务商没有任何隶属关系，使用前请自行确认符合本地法律法规
  以及对应代理服务的使用条款。
- `github_blocklist.conf` 里列出来的 `owner/repo` 会被以 451 语义拒绝；在对
  某些项目二进制再分发有限制的法域，请善用此文件自行屏蔽。

> 本工具内置的镜像加速功能仅加速 GitHub 公开 Release 资源，供开发者合法获取
> 开源软件。代理服务由第三方提供，用户需自行确保符合所在地法律法规；yai
> 不提供、不托管任何二进制文件，仓库索引均指向 upstream 原链接。

---

## 许可证

yai 由 yai-byte 作者们共同开发，按仓库根目录下 `LICENSE` 文件中的条款分发。
部分 JSON 扫描器与 CLI 模板参考自 MIT 许可证的单文件 AppImage 工具，具体归属
请单独查看各源文件头部的声明。

**AppImage** 是 Simon Peter 的商标；yai 是独立客户端，与 AppImage 官方项目
之间不存在背书关系。
