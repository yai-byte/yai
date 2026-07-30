# AppImage GitHub 仓库资源集成改进设计文档

## 1. 背景与动机

### 1.1 现状

yai 当前的包解析依赖单一数据源——AppImage 官方 `feed.json`（约 1407 个包）。该 feed 存在以下局限：

- **覆盖不全**：许多 AppImage 应用未被收录，或已被移除
- **信息有限**：缺少应用描述、作者、架构标签等元数据
- **更新滞后**：feed 的维护频率较低，部分应用的下载链接已过时

### 1.2 可用的补充数据源

AppImage GitHub 仓库 (`AppImage/appimage.github.io`) 提供了两个有价值的数据源，但**用途不同**：

| 数据源       | 路径               | 格式                          | 数量      | 用途定位         |
| --------- | ---------------- | --------------------------- | ------- | ------------ |
| **apps/** | `apps/<Name>.md` | Markdown + YAML frontmatter | ~1000+  | **索引构建**（repo add） |
| **data/** | `data/<Name>`    | 纯文本                         | ~1000+  | **下载解析**（repo resolve） |

- **`apps/`**：提供丰富的元数据（描述、架构、版本、GitHub 仓库），适合补充 index.json
- **`data/`**：仅提供下载 URL（且经常过时），不适合写入索引，但可作为下载解析时的兜底

#### `apps/` 目录示例（86Box.md）：

```yaml
---
layout: app
permalink: /86Box/
description: x86/x64 PC emulator
license: GPL-2.0
authors:
  - name: 86Box
    url: https://github.com/86Box
links:
  - type: GitHub
    url: 86Box/86Box
  - type: Download
    url: https://github.com/86Box/86Box/releases
desktop:
  X-AppImage-Arch: x86_64
  X-AppImage-Version: 3.2.1-b3602
---
```

#### `data/` 目录示例：

```
# 86Box → GitHub Release 直接下载链接
https://github.com/86Box/86Box/releases/download/v3.2.1/86Box-Linux-x86_64-b3602.AppImage

# 4KWALL → GitHub 仓库 URL
https://github.com/rishabh3354/4KWALL

# Audacity → 带注释的下载链接
https://github.com/audacity/audacity/releases/download/Audacity-3.2.0-alpha-1/audacity-linux-3.2.0-alpha-1-x64.AppImage
# https://github.com/audacity/audacity
```

### 1.3 改进目标

1. **扩充 index.json**：将 `apps/` 中的应用纳入 yai 的包索引，作为 feed.json 的补充（**不涉及 `data/`**）
2. **提升解析成功率**：在下载链接解析时，引入 `apps/` 和 `data/` 作为兜底数据源
3. **改善优先级链**：建立清晰的多源解析优先级

---

## 2. 数据源分析

### 2.1 `apps/` 目录结构（索引构建数据源）

每个 `.md` 文件提供以下关键字段：

| 字段                           | 说明                         | 用途           |
| ---------------------------- | -------------------------- | ------------ |
| `permalink`                  | 应用的 URL slug               | 作为包 ID 的辅助匹配 |
| `description`                | 应用描述                       | 直接展示给用户      |
| `license`                    | 许可证信息                      | 信息展示         |
| `authors[].url`              | 作者 GitHub URL              | 提取 GitHub 仓库 |
| `links[].type=GitHub`        | `owner/repo` 格式的 GitHub 仓库 | **核心解析数据源**  |
| `links[].type=Download`      | 下载页面 URL                   | 辅助下载解析       |
| `links[].type=Web`           | 项目官网                       | 官网嗅探的入口      |
| `desktop.X-AppImage-Arch`    | AppImage 架构标签              | 架构匹配         |
| `desktop.X-AppImage-Version` | AppImage 版本号               | 版本信息         |

### 2.2 `data/` 目录结构（仅用于下载解析）

纯文本文件，每行一个 URL，支持注释（`#` 开头）。**不写入 index.json**，仅在 `repo resolve` 时作为兜底使用。

**模式 A：GitHub 仓库 URL**

```
https://github.com/owner/repo
```

→ 解析为 `github_release` 源类型

**模式 B：直接下载 URL**

```
https://github.com/owner/repo/releases/download/tag/Name.AppImage
```

→ 解析为 `direct_url` 源类型

**模式 C：注释 + 链接**

```
# https://github.com/owner/repo
https://example.com/downloads/Name.AppImage
```

→ 优先提取有效 URL，注释作为 GitHub 仓库的备选

**模式 D：空文件或 404**
→ 跳过此条目

### 2.3 与 feed.json 的对比

| 特性          | feed.json                  | apps/                           | data/     |
| ----------- | -------------------------- | ------------------------------- | --------- |
| 应用数量        | ~1407                      | ~1000+                          | ~1000+   |
| GitHub 仓库信息 | ✓（`links[].type=github`）   | ✓（`links[].type=GitHub`）        | ✓（直接 URL） |
| 直接下载链接      | ✓（`links[].type=download`） | ✓（`links[].type=Download`）      | ✓（直接 URL） |
| 应用描述        | ✓（`description`）           | ✓（`description`）                | ✗         |
| 架构信息        | ✗                          | ✓（`desktop.X-AppImage-Arch`）    | ✗         |
| 版本信息        | ✗                          | ✓（`desktop.X-AppImage-Version`） | ✗         |
| 作者信息        | ✗                          | ✓                               | ✗         |
| 维护状态        | 官方维护                       | 社区维护                            | 社区维护      |
| 下载链接新鲜度     | 无下载链接                      | 需验证                             | 经常过时      |
| **用途**      | **索引构建**                  | **索引构建**                        | **仅下载解析** |

---

## 3. 集成架构设计

### 3.1 整体流程

#### `yai repo add appimage`（仅使用 apps/）

```
yai repo add appimage
    │
    ├─ 1. 下载 feed.json → 规范化为 schema-v1（现有逻辑）
    │
    ├─ 2. 下载 apps/ 目录列表 → 逐个解析 .md 文件
    │       ├─ 提取 GitHub 仓库、下载链接、描述、架构等
    │       └─ 与 feed.json 包合并（feed 优先）
    │
    └─ 3. 写入合并后的 index.json
```

> **注意**：`repo add` 阶段**不涉及 `data/`**，因为 `data/` 仅提供可能过时的下载 URL，不适合作为索引的一部分。

#### `yai repo resolve <package>`（使用 apps/ + data/ 兜底）

```
yai repo resolve <package>
    │
    ├─ 1. 检查 index.json 中的 download_urls（现有逻辑）
    │
    ├─ 2. 按 source_type 分发：
    │       ├─ github_release → GitHub API 解析最新 Release
    │       ├─ direct_url → 直接使用 URL
    │       └─ website_page → 网站嗅探
    │
    ├─ 3. 兜底回退链：
    │       ├─ 3a. 官网嗅探（resolve_website_appimage_download）
    │       ├─ 3b. AppImageHub catalog 页面解析（fetch_appimage_catalog_sources）
    │       ├─ 3c. AppImage GitHub data/ 查找 ← 下载链接兜底
    │       └─ 3d. AppImage GitHub apps/ 查找 ← GitHub 仓库兜底
    │
    └─ 4. 缓存结果到 index.json 的 download_urls
```

### 3.2 数据源优先级

#### 3.2.1 index.json 构建时的包合并优先级（repo add）

当 feed.json 与 apps/ 存在重叠时：

```
feed.json > apps/
```

- **feed.json 优先**：feed 中的包信息最权威，apps/ 仅用于补充缺失的包
- **apps/ 次之**：apps 有更丰富的元数据（描述、架构、版本），但仅用于 feed 未收录的包

> `data/` 不参与索引构建。

#### 3.2.2 下载链接解析优先级（repo resolve）

当一个包需要解析下载链接时：

```
GitHub Release > 官网嗅探 > AppImage data/ > AppImage apps/ > index.json 已有链接
```

| 优先级 | 数据源                 | 说明                                                                         |
| --- | ------------------- | -------------------------------------------------------------------------- |
| 1   | **GitHub Release**  | 若包有 `source_type: github_release`，通过 GitHub API 获取最新 Release 的 AppImage 资源 |
| 2   | **官网嗅探**            | 若包有 `source_type: website_page`，爬取官网寻找 AppImage 下载链接                       |
| 3   | **AppImage data/**  | 若以上均失败，查找 `data/<Name>` 文件中的 URL                                           |
| 4   | **AppImage apps/**  | 若 data/ 也失败，查找 `apps/<Name>.md` 中的 GitHub 仓库或下载链接                          |
| 5   | **index.json 已有链接** | 使用之前缓存的 `download_urls`                                                    |

### 3.3 新增的数据获取模块

#### 3.3.1 常量定义（yai.hpp）

```cpp
// AppImage GitHub 仓库 API 基础 URL
constexpr const char* kAppImageGithubRepoApiBase = 
    "https://api.github.com/repos/AppImage/appimage.github.io";

// AppImage GitHub 原始内容基础 URL
constexpr const char* kAppImageGithubRawBase = 
    "https://raw.githubusercontent.com/AppImage/appimage.github.io/master";

// 超时时间常量
constexpr int kFetchAppImageGithubListTimeoutMs = 15000;   // 目录列表获取
constexpr int kFetchAppImageGithubEntryTimeoutMs = 10000;   // 单个条目获取
constexpr int kFetchAppImageGithubResolveTimeoutMs = 15000; // 解析时获取
```

#### 3.3.2 新增结构体

```cpp
// 索引构建时从 apps/ 解析出的条目
struct AppImageAppsEntry {
    std::string name;              // 应用名称（如 "86Box"）
    std::string github_repo;       // GitHub 仓库 (owner/repo)
    std::string direct_url;        // 直接下载 URL（来自 Download link）
    std::string homepage;          // 项目官网
    std::string description;       // 应用描述
    std::string license;           // 许可证
    std::string arch;              // 架构标签（来自 X-AppImage-Arch）
    std::string version;           // 版本号（来自 X-AppImage-Version）
};

// 下载解析时从 data/ 解析出的条目
struct AppImageDataEntry {
    std::string name;              // 应用名称
    std::string github_repo;       // GitHub 仓库 (owner/repo)
    std::string direct_url;        // 直接下载 URL
};
```

#### 3.3.3 新增函数声明（yai.hpp）

```cpp
// === apps/ 索引构建相关 ===
std::vector<std::string> fetch_appimage_apps_list(int timeout_ms = kFetchAppImageGithubListTimeoutMs);
std::optional<AppImageAppsEntry> parse_appimage_apps_entry(const std::string& name);

// === data/ 下载解析相关（repo resolve 时使用）===
std::optional<AppImageDataEntry> parse_appimage_data_entry(const std::string& name);

// === 解析时的回退查找 ===
std::optional<AppImageDataEntry> lookup_appimage_data_entry(
    const std::string& package_name,
    int timeout_ms = kFetchAppImageGithubResolveTimeoutMs);

std::optional<AppImageAppsEntry> lookup_appimage_apps_entry(
    const std::string& package_name,
    int timeout_ms = kFetchAppImageGithubResolveTimeoutMs);

// === 包合并逻辑（仅 apps/ 参与）===
RepoPackage merge_apps_entry_into_package(
    const AppImageAppsEntry& entry,
    const RepoPackage& existing);
```

### 3.4 实现模块划分

#### 3.4.1 `repo_appimage_github.cpp` — 新增文件

**包含功能：**

- `fetch_appimage_apps_list()`：通过 GitHub API 分页获取 `apps/` 目录列表
- `parse_appimage_apps_entry()`：通过 GitHub API 获取单个 `.md` 文件内容并解析 YAML frontmatter
- `parse_appimage_data_entry()`：获取单个 `data/<Name>` 文件内容并解析 URL
- `lookup_appimage_data_entry()`：在 `repo resolve` 时实时查找 data/ 条目
- `lookup_appimage_apps_entry()`：在 `repo resolve` 时实时查找 apps/ 条目

**关键实现细节：**
- GitHub API `/contents/apps` 每次最多返回 100 条，需分页获取（`?per_page=100&page=N`）
- `.md` 文件通过 GitHub API 的 base64 编码响应获取，需解码后解析 YAML frontmatter
- 对于 YAML 解析，采用轻量级字符串处理（正则 + 行解析），无需引入 YAML 解析库
- `data/` 条目仅在 `repo resolve` 时按需获取（非预加载），因为数量大且大部分已过时

#### 3.4.2 `repo_feed.cpp` — 扩展 feed 归一化

**修改 `normalize_appimage_feed_index()`：**

在 feed.json 规范化后，追加 `apps/` 中的补充包（**不涉及 data/**）。

```
normalize_appimage_feed_index():
    1. 解析 feed.json → 获取现有 packages
    2. 获取 apps/ 列表 → 逐个解析 .md
    3. 对每个 apps 条目：
        - ID 匹配：与 feed 包匹配
        - 已存在 → 补充元数据（description, arch, version）
        - 不存在 → 创建新 RepoPackage
    4. 返回合并后的 index
```

**合并规则：**
```
if (package_id 已存在 (feed)):
    保留已有包，补充 apps/ 中的额外元数据（description, arch, version）
else:
    从 apps/ 创建新包：
    - 有 github_repo → source_type = "github_release"
    - 有 direct_url → source_type = "direct_url"
    - 都有 → source_type = "github_release"
    - 都没有 → source_type = "website_page"
```

#### 3.4.3 `resolver.cpp` — 扩展解析回退链

**修改 `resolve_repo_package_install_source_impl()`：**

在现有的 GitHub Release 解析失败回退（AppImageHub catalog）之后，增加两个新的回退步骤：

```
// 现有逻辑：GitHub Release → AppImageHub catalog
// 新增逻辑：
// Step 1: 官网嗅探（已有）
// Step 2: AppImageHub catalog 页面解析（已有）
// Step 3: AppImage GitHub data/ 查找 ← 新增（下载 URL 兜底）
// Step 4: AppImage GitHub apps/ 查找 ← 新增（GitHub 仓库兜底）
// Step 5: 使用 index.json 中已有的 download_urls
```

**回退实现伪代码：**

```cpp
// 在 AppImageHub catalog 回退之后：
try {
    // Step 3: data/ 查找（优先获取直接下载 URL）
    auto data_entry = lookup_appimage_data_entry(package.name);
    if (data_entry.has_value()) {
        if (!data_entry->direct_url.empty()) {
            // 直接使用下载 URL
            return make_direct_url_source(options, data_entry->direct_url);
        }
        if (!data_entry->github_repo.empty()) {
            // 使用 GitHub 仓库进行 Release 解析
            RepoPackage github_pkg = package;
            github_pkg.source_owner = extract_owner(data_entry->github_repo);
            github_pkg.source_repo = extract_repo(data_entry->github_repo);
            github_pkg.source_type = "github_release";
            return repo_github_release_source(options, github_pkg);
        }
    }
    
    // Step 4: apps/ 查找（获取 GitHub 仓库）
    auto apps_entry = lookup_appimage_apps_entry(package.name);
    if (apps_entry.has_value()) {
        if (!apps_entry->github_repo.empty()) {
            // 使用 apps 中的 GitHub 仓库
            RepoPackage github_pkg = package;
            github_pkg.source_owner = extract_owner(apps_entry->github_repo);
            github_pkg.source_repo = extract_repo(apps_entry->github_repo);
            github_pkg.source_type = "github_release";
            return repo_github_release_source(options, github_pkg);
        }
        if (!apps_entry->direct_url.empty()) {
            return make_direct_url_source(options, apps_entry->direct_url);
        }
    }
} catch (const std::exception&) {
    // 继续抛出异常
}
```

#### 3.4.4 `commands_query.cpp` — 改善搜索/信息展示

**修改内容：**
- 当包来自 apps/ 时，在 `info` 输出中标注来源（如 `Source: AppImage GitHub (apps/)`）
- 展示从 apps/ 解析出的架构和版本信息
- 当包标记为 `unavailable` 但有 GitHub 仓库信息时，显示 GitHub 链接作为手动下载指引

#### 3.4.5 `repo.cpp` — 扩展索引构建

**修改内容：**
- `RepoPackage` 新增字段（见 8.4）
- 新增 `merge_apps_entry_into_package()` 合并逻辑

---

## 4. 数据获取与解析细节

### 4.1 apps/ 目录列表获取（repo add 时）

```
GET https://api.github.com/repos/AppImage/appimage.github.io/contents/apps?per_page=100&page=N
```

响应解析：
```json
[
  {
    "name": "86Box.md",
    "path": "apps/86Box.md",
    "download_url": "https://raw.githubusercontent.com/...",
    "size": 970
  },
  ...
]
```

提取文件名 → 去除 `.md` 后缀 → 获得应用 ID。

### 4.2 apps/ 单条目解析（repo add 时）

```
GET https://api.github.com/repos/AppImage/appimage.github.io/contents/apps/86Box.md
```

响应中 `content` 字段为 base64 编码的文件内容，解码后解析 YAML frontmatter。

**YAML frontmatter 解析规则（轻量级）：**

```
1. 提取 `---` 之间的内容作为 frontmatter
2. 逐行解析：
   - `key: value` → 简单键值对
   - `links:` 开头 → 进入 links 数组解析模式
   - `  - type: GitHub` → 识别为 link 条目
   - `    url: owner/repo` → 提取 URL
   - `  - type: Download` → 识别为下载链接
   - `    url: https://github.com/.../releases` → 提取下载页面
   - `desktop:` 开头 → 进入 desktop 块解析
   - `  X-AppImage-Arch: x86_64` → 提取架构
   - `  X-AppImage-Version: 3.2.1` → 提取版本
```

### 4.3 data/ 单条目解析（repo resolve 时）

**按需获取**（不在 repo add 时预加载），通过 GitHub API 获取单个文件内容：

```
GET https://api.github.com/repos/AppImage/appimage.github.io/contents/data/<Name>
```

或直接获取原始文本：

```
GET https://raw.githubusercontent.com/AppImage/appimage.github.io/master/data/<Name>
```

**解析规则：**
```
1. 忽略以 `#` 开头的注释行
2. 提取第一个有效 URL
3. 判断 URL 类型：
   - 包含 `/releases/download/` → direct_url
   - 匹配 `github.com/owner/repo` → github_repo
   - 其他 → 跳过
```

### 4.4 分页处理

GitHub API 默认最多返回 100 条，`apps/` 有 1000+ 条目，需分页获取（仅在 `repo add` 时）：

```cpp
std::vector<std::string> fetch_apps_directory_listing() {
    std::vector<std::string> all_names;
    int page = 1;
    while (true) {
        std::string url = std::string(kAppImageGithubRepoApiBase) 
                        + "/contents/apps?per_page=100&page=" + std::to_string(page);
        auto response = fetch_text_limited(url, kFetchAppImageGithubListTimeoutMs, 1024 * 1024);
        auto items = parse_json_array(response);
        for (auto& item : items) {
            all_names.push_back(item["name"]);
        }
        if (items.size() < 100) break;
        page++;
    }
    return all_names;
}
```

> **data/ 不分页预加载**：在 `repo resolve` 时，仅按需获取单个 `data/<Name>` 文件。

---

## 5. 包合并策略（仅 apps/ 参与）

### 5.1 ID 匹配

apps/ 中的文件名（去除 `.md` 后缀）即应用 ID，需与 feed.json 中的 ID 进行匹配：

```
apps/86Box.md → ID: "86Box"
feed.json 中 "name": "86 Box" → sanitize_id("86 Box") → ID: "86-Box"

匹配规则：
1. 精确匹配：ID 完全相同
2. 归一化匹配：sanitize_id(name) 与 apps_filename 比较
3. 低置信度匹配：在 repo add 时提示用户确认
```

### 5.2 字段合并

当 apps/ 中的包已存在于 feed.json 中时：

| 字段                 | 合并策略                                        |
| ------------------ | ------------------------------------------- |
| id                 | 保留 feed 的 ID                                |
| name               | 保留 feed 的 name                              |
| summary            | 保留 feed 的 summary，若为空则使用 apps 的 description |
| homepage           | 保留 feed 的 homepage                          |
| license            | 保留 feed 的 license，若为空则使用 apps 的 license     |
| source\_type       | 保留 feed 的 source\_type                      |
| source\_owner/repo | 保留 feed 的，但若 feed 缺失则补充 apps 的              |
| source\_url        | 保留 feed 的                                   |
| asset\_pattern     | 保留 feed 的                                   |
| download\_urls     | 保留已有                                        |
| arch               | 新增字段，来自 apps 的 `X-AppImage-Arch`            |
| version            | 新增字段，来自 apps 的 `X-AppImage-Version`         |

### 5.3 新包创建

当 apps/ 中的包不存在于 feed.json 中时：

```
if (有 github_repo):
    source_type = "github_release"
    source_owner = github_repo.owner
    source_repo = github_repo.repo
    asset_pattern = ".*\\.AppImage$"
elif (有 direct_url):
    source_type = "direct_url"
    source_url = direct_url
elif (有 homepage):
    source_type = "website_page"
    source_url = homepage
    source_reason = "AppImage GitHub (apps/) entry: no release or direct download link"
else:
    source_type = "website_page"
    source_url = appimage_catalog_page_url(name)
    source_reason = "AppImage GitHub entry with no usable source"
```

---

## 6. 性能与缓存策略

### 6.1 apps/ 列表缓存

`apps/` 目录列表在短期内不会频繁变化。建议：

- 列表结果缓存到本地文件（`~/.local/share/yai/repos/appimage-apps-list-cache.json`）
- 缓存有效期：24 小时
- 带 `--no-cache` 选项可强制刷新

### 6.2 apps/ 条目缓存

apps/ 单条目的解析结果也应缓存：

- 每个 apps 条目的解析结果缓存 7 天
- 存储在 `~/.local/share/yai/repos/appimage-apps-cache/` 目录下
- 文件名即应用 ID

### 6.3 data/ 按需缓存

data/ 条目**不预缓存列表**，在 `repo resolve` 时按需获取：

- 单个 data 条目的解析结果缓存 1 天（因为内容经常过时）
- 存储在 `~/.local/share/yai/repos/appimage-data-cache/` 目录下
- 文件名即应用 ID

### 6.4 增量更新

`yai repo update appimage` 时：

1. 检查 feed.json 是否更新（ETag / Last-Modified）
2. 检查 apps/ 列表是否有变化（与本地缓存比较）
3. 仅解析新增或变更的 apps 条目
4. 合并增量结果到现有 index.json

### 6.5 并发控制

GitHub API 对未认证请求限制为 60 次/小时。需采用：

- apps/ 列表获取：串行（分页获取，最多 ~10 次请求）
- apps/ 条目获取：限并发 2-3 个，避免触发速率限制
- data/ 条目获取：按需单次请求，无需并发控制
- 建议在 `YAI_GITHUB_TOKEN` 存在时使用认证请求（5000 次/小时）

---

## 7. 错误处理

### 7.1 网络错误

| 场景              | 处理方式                   |
| --------------- | ---------------------- |
| apps/ 列表获取超时      | 使用本地缓存，日志警告            |
| apps/ 条目获取 404    | 跳过该条目，标记为不可用           |
| apps/ 条目获取超时      | 跳过该条目，日志警告             |
| data/ 条目获取 404    | 在 resolve 时跳过，继续下一个回退    |
| GitHub API 速率限制 | 暂停请求，等待速率窗口重置或使用 token |
| 网络连接失败          | 整批跳过，提示用户稍后重试          |

### 7.2 数据异常

| 场景                    | 处理方式              |
| --------------------- | ----------------- |
| .md 文件为空              | 跳过                |
| YAML frontmatter 格式错误 | 记录警告，尝试提取文本中的 URL |
| data 文件为空或格式错误        | 在 resolve 时跳过        |
| URL 无效                | 跳过该 URL，尝试下一个     |
| apps/ 条目冲突            | 保留 feed 信息，补充 apps 元数据 |

### 7.3 用户可见反馈

**`yai repo add appimage` 示例：**

```
yai repo add appimage
  yai: downloading feed.json (1407 packages)
  yai: fetching AppImage GitHub apps/ listing...
  yai:   found 1023 app entries
  yai: merging packages with feed.json...
  yai:   added 156 new packages from apps/
  yai:   enriched 238 packages with metadata (arch, version, description)
  yai:   12 apps failed to parse
  yai: repo index ready (1563 packages total)
```

**`yai repo resolve 86Box` 示例（data/ 回退成功）：**

```
yai repo resolve 86Box
  yai: GitHub release resolution failed for 86Box. Trying fallback...
  yai: AppImageHub catalog search failed.
  yai: trying AppImage GitHub data/ lookup...
  yai:   found direct download URL in data/86Box
  yai: resolved https://github.com/86Box/86Box/releases/download/v3.2.1/86Box-Linux-x86_64-b3602.AppImage
```

---

## 8. 文件修改清单

### 8.1 新增文件

| 文件                             | 说明                                              |
| ------------------------------ | ----------------------------------------------- |
| `src/repo_appimage_github.cpp` | AppImage GitHub 数据源解析（apps/ 索引构建 + data/ 下载解析） |

### 8.2 修改文件

| 文件                              | 修改内容                                                                |
| ------------------------------- | --------------------------------------------------------------------- |
| `src/yai.hpp`                   | 新增常量、结构体声明、函数声明                                       |
| `src/repo_feed.cpp`             | 扩展 `normalize_appimage_feed_index()` 仅合并 apps/ 包                      |
| `src/repo.cpp`                  | 新增包合并逻辑，支持新字段（arch, version, source_origin）                |
| `src/resolver.cpp`              | 扩展回退链，增加 data/ 和 apps/ 下载解析查找                           |
| `src/commands_query.cpp`        | 改善 info 展示，标注 apps/ 来源，显示 arch/version                       |
| `src/commands_repo_resolve.cpp` | 支持回退链中的新查找步骤                                                         |

### 8.3 `src/yai.hpp` 新增声明

```cpp
// 新增常量
constexpr const char* kAppImageGithubRepoApiBase = "https://api.github.com/repos/AppImage/appimage.github.io";
constexpr const char* kAppImageGithubRawBase = "https://raw.githubusercontent.com/AppImage/appimage.github.io/master";
constexpr int kFetchAppImageGithubListTimeoutMs = 15000;
constexpr int kFetchAppImageGithubEntryTimeoutMs = 10000;

// apps/ 索引构建条目
struct AppImageAppsEntry {
    std::string name;
    std::string github_repo;
    std::string direct_url;
    std::string homepage;
    std::string description;
    std::string license;
    std::string arch;
    std::string version;
};

// data/ 下载解析条目
struct AppImageDataEntry {
    std::string name;
    std::string github_repo;
    std::string direct_url;
};

// apps/ 索引构建
std::vector<std::string> fetch_appimage_apps_list(int timeout_ms = kFetchAppImageGithubListTimeoutMs);
std::optional<AppImageAppsEntry> parse_appimage_apps_entry(const std::string& name);

// data/ 下载解析（按需）
std::optional<AppImageDataEntry> parse_appimage_data_entry(const std::string& name);

// 解析回退查找
std::optional<AppImageDataEntry> lookup_appimage_data_entry(
    const std::string& package_name,
    int timeout_ms = kFetchAppImageGithubResolveTimeoutMs);
std::optional<AppImageAppsEntry> lookup_appimage_apps_entry(
    const std::string& package_name,
    int timeout_ms = kFetchAppImageGithubResolveTimeoutMs);

// 包合并（仅 apps/）
RepoPackage merge_apps_entry_into_package(
    const AppImageAppsEntry& entry,
    const RepoPackage& existing);
```

### 8.4 `src/repo.cpp` `RepoPackage` 新增字段

```cpp
struct RepoPackage {
    // ... 现有字段 ...
    
    // 新增字段（来自 apps/）
    std::string arch;              // 架构标签（X-AppImage-Arch）
    std::string version;           // 版本号（X-AppImage-Version）
    std::string source_origin;     // 来源标识（"feed" / "appimage_apps"）
};
```

---

## 9. 测试计划

### 9.1 单元测试

| 测试项                 | 测试内容                                 |
| ------------------- | ------------------------------------ |
| YAML frontmatter 解析 | 解析合法/非法/空的 .md 文件                    |
| apps/ 列表获取          | 分页处理、缓存逻辑                            |
| data/ 文件解析          | GitHub URL / 直接下载 URL / 注释 / 空文件       |
| 包合并逻辑               | feed + apps 合并、冲突处理、元数据补充               |
| ID 匹配               | 精确匹配、归一化匹配、不匹配场景                     |
| 错误处理                | 网络超时、404、速率限制                        |

### 9.2 集成测试

| 测试项                        | 测试内容                                          |
| -------------------------- | --------------------------------------------- |
| `yai repo add appimage`    | feed + apps 合并流程，不涉及 data/                      |
| `yai repo update appimage` | 增量更新、apps/ 缓存验证                              |
| `yai repo resolve <app>`   | 验证回退链：GitHub Release → data/ → apps/          |
| `yai search <keyword>`     | 搜索 apps/ 新增的包                                  |
| `yai info <package>`       | 信息展示包含 arch、version、source_origin             |
| `yai install <app>`        | 通过 GitHub Release 安装、通过 data/ 回退安装           |

### 9.3 冒烟测试脚本

建议新增：
- `tests/appimage_apps_index_smoke.sh`：验证 apps/ 集成到索引的流程
- `tests/appimage_data_resolve_smoke.sh`：验证 data/ 回退解析链

### 9.4 边界测试

- 网络不可用时的降级行为
- GitHub API 速率限制触发后的行为
- apps/ 条目与 feed 冲突时的保留策略
- data/ 文件为空或格式错误时的跳过行为
- 特殊字符和非 ASCII 应用名的处理

---

## 10. 实施路线图

### Phase 1：索引构建集成（apps/ only）

**目标：** 将 `apps/` 的包纳入 index.json，**不涉及 data/**

1. 新增 `repo_appimage_github.cpp`，实现 apps/ 列表获取和条目解析
2. 修改 `repo_feed.cpp`，在 `normalize_appimage_feed_index()` 中追加 apps/ 包
3. 修改 `repo.cpp`，支持新字段（arch, version, source_origin）
4. 单元测试 + 冒烟测试

**产出：** `yai repo add appimage` 生成包含 feed + apps 的完整 index.json

### Phase 2：下载解析回退链（data/ + apps/）

**目标：** 在下载链接解析时使用 AppImage GitHub 资源作为兜底

1. 修改 `resolver.cpp`，增加 data/ 和 apps/ 回退步骤
2. 实现 `lookup_appimage_data_entry()` 按需查找
3. 实现 `lookup_appimage_apps_entry()` 按需查找
4. 集成测试

**产出：** 无 feed 链接的包也能通过 data/ 或 apps/ 资源安装

### Phase 3：优化与完善

**目标：** 性能优化和用户体验改善

1. 实现 apps/ 列表缓存和条目缓存
2. 实现增量更新
3. 并发控制优化
4. 完善错误消息和用户反馈
5. 文档更新

**产出：** 高效、可靠的 AppImage GitHub 资源集成

---

## 11. 风险与缓解

| 风险              | 影响            | 缓解措施                              |
| --------------- | ------------- | --------------------------------- |
| GitHub API 速率限制 | 大量请求被拒绝       | 使用 `YAI_GITHUB_TOKEN`、串行/限并发、本地缓存 |
| 网络不稳定           | apps/ 列表获取超时   | 已有本地缓存时使用缓存、分阶段重试                 |
| data/ 数据过时     | 解析出过时的下载链接    | 仅在回退链末尾使用、缓存短期（1天）            |
| YAML 解析复杂性      | 解析错误          | 采用轻量级字符串处理 + 容错回退                 |
| 索引膨胀            | index.json 变大 | 仅追加 apps/ 独有包、支持增量更新             |
| 新增文件大小          | 编译时长增加        | 模块化拆分、预编译头优化                      |

---

## 12. 后续可能的扩展

1. **AppImage GitHub Issues 集成**：抓取 AppImage GitHub 仓库的 Issues 页面，获取被废弃包的状态
2. **AppImage Hub API 集成**：使用 `https://appimage.github.io/api/` 获取实时下载统计
3. **用户贡献回传**：允许用户向 AppImage GitHub 仓库提交缺失的应用条目
4. **多架构优化**：利用 apps/ 中的 `X-AppImage-Arch` 实现更精确的架构匹配
5. **版本追踪**：利用 apps/ 中的 `X-AppImage-Version` 实现版本对比和升级提示
6. **data/ 预加载模式**：对于离线场景，可选择性预加载 data/ 条目到本地缓存