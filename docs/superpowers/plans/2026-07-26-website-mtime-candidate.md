# Website AppImage mtime + stale demotion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prefer fresher AppImage candidates on non-GitHub website crawls using directory-listing `Last-Modified` (and bounded HEAD), while demoting `old`/`older`/`attic` paths instead of lexicographic folder tricks.

**Architecture:** Add pure helpers for stale-path detection, Apache autoindex row parsing, and mtime/stale-aware candidate comparison in the URL/website resolver layer. Extend website queue items and the AppImage candidate pool with optional mtime + stale penalty; remove KDE `std::reverse`; soften early-stop; select with arch → non-stale → mtime → basename score.

**Tech Stack:** C++17, existing `curl` HEAD via process helpers, `file://` HTML fixtures, shell smoke tests under `tests/`, Makefile.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-26-website-mtime-candidate-design.md`
- Freshness signal is listing/HEAD `Last-Modified`, **not** semver
- Stale paths are demoted (`stale_penalty = 1`), never hard-dropped
- Trust filters, depth &lt; 2, queue ≤ 128, ≤ 96 pages checked, catalog one-hop bridge unchanged
- GitHub release asset selection unchanged
- msgid = English; sync `po/en.po` / `po/zh.po` only if new user-facing strings appear
- Do not commit unless the user explicitly asks; skip commit steps when commits are not requested

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | Declare `WebsiteLinkMeta`, stale/mtime/parse/compare helpers, extend selection APIs as needed |
| `src/resolver_url.cpp` | Implement stale detection, autoindex parse, mtime parse, candidate compare; keep URL/HTML helpers |
| `src/resolver_website.cpp` | Queue fields, sort, candidate pool, early-stop, HEAD fill-in, remove KDE reverse |
| `tests/website_mtime_smoke.sh` | Unit + fixture integration for mtime/stale selection |
| `tests/appimage_feed_smoke.sh` | Adjust checked/queued counts only if early-stop changes them |

---

### Task 1: Stale path + listing mtime helpers (unit smoke)

**Files:**
- Modify: `src/yai.hpp` (declarations near other URL helpers)
- Modify: `src/resolver_url.cpp` (implementations)
- Test: `tests/website_mtime_smoke.sh` (new; start with unit section only)

**Interfaces:**
- Produces:
  - `bool website_url_looks_stale(const std::string& url);`
  - `std::optional<std::int64_t> parse_directory_listing_mtime(const std::string& text);` — parses `YYYY-MM-DD HH:MM` (and optionally `YYYY-MM-DD HH:MM:SS`) as UTC epoch seconds; empty/`nullopt` on failure
  - `std::optional<std::int64_t> parse_http_last_modified_mtime(const std::string& text);` — parses RFC 1123 `Last-Modified` values (reuse only if easy; otherwise implement minimal `strptime`-style parse for `Sun, 02 Jun 2026 09:22:00 GMT`)
  - `struct WebsiteLinkMeta { std::string url; std::optional<std::int64_t> mtime; int stale_penalty = 0; };`
  - `int website_link_stale_penalty(const std::string& url);` — returns `1` if `website_url_looks_stale`, else `0`

Stale rules (case-insensitive on path after stripping fragment/query):

- path contains `older`
- path has a `/attic/` segment or a segment equal to `attic`
- a path segment equals `old` (split on `/`; do **not** match substrings inside `download` / `threshold`)

- [ ] **Step 1: Write the failing unit smoke**

Create `tests/website_mtime_smoke.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

cat > "$TMP_DIR/mtime_unit.cpp" <<'CPP'
#include "yai.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool c, const char* m) {
    if (!c) throw std::runtime_error(m);
}

int main() {
    require(website_url_looks_stale("https://download.kde.org/stable/krita/older_versions_are_in_the_attic"),
            "older_versions");
    require(website_url_looks_stale("https://download.kde.org/Attic/krita/"), "Attic");
    require(website_url_looks_stale("https://example.invalid/old/pkg.AppImage"), "segment old");
    require(!website_url_looks_stale("https://example.invalid/download/pkg.AppImage"), "download false positive");
    require(!website_url_looks_stale("https://example.invalid/threshold/pkg.AppImage"), "threshold false positive");
    require(website_link_stale_penalty("https://x/older/y") == 1, "penalty");
    require(website_link_stale_penalty("https://x/y") == 0, "no penalty");

    const auto a = parse_directory_listing_mtime("2026-06-02 09:22");
    const auto b = parse_directory_listing_mtime("2026-05-26 14:33");
    require(a.has_value() && b.has_value() && *a > *b, "listing mtime order");
    require(!parse_directory_listing_mtime("not-a-date").has_value(), "bad listing mtime");

    std::cout << "website mtime unit smoke passed\n";
    return 0;
}
CPP

# Link the same resolver/core pieces other smokes need once symbols resolve.
# Start minimal; add sources until link succeeds.
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/mtime_unit" \
  "$TMP_DIR/mtime_unit.cpp" \
  "$ROOT/src/resolver_url.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/process.cpp" \
  "$ROOT/src/i18n.cpp" \
  -lpthread

"$TMP_DIR/mtime_unit"
```

Make executable: `chmod +x tests/website_mtime_smoke.sh`

- [ ] **Step 2: Run test to verify it fails**

Run: `bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: FAIL — missing symbols / compile errors for undeclared helpers.

- [ ] **Step 3: Declare and implement helpers**

In `src/yai.hpp`, declare the functions/struct listed above.

In `src/resolver_url.cpp`, implement:

```cpp
bool website_url_looks_stale(const std::string& url) {
    const std::string lower = to_lower(strip_url_fragment_query(url));
    std::string path = lower;
    const std::size_t scheme = path.find("://");
    if (scheme != std::string::npos) {
        const std::size_t path_start = path.find('/', scheme + 3);
        path = path_start == std::string::npos ? "" : path.substr(path_start);
    }
    if (path.find("older") != std::string::npos) {
        return true;
    }
    // split segments on '/'
    std::size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        const std::size_t start = i;
        while (i < path.size() && path[i] != '/') {
            ++i;
        }
        const std::string seg = path.substr(start, i - start);
        if (seg == "old" || seg == "attic") {
            return true;
        }
    }
    return false;
}

int website_link_stale_penalty(const std::string& url) {
    return website_url_looks_stale(url) ? 1 : 0;
}
```

Implement `parse_directory_listing_mtime` with `sscanf`/`std::tm` + `timegm` (or portable UTC conversion used elsewhere if present). Return `nullopt` on parse failure.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: `website mtime unit smoke passed`

- [ ] **Step 5: Commit** (skip unless user asked)

---

### Task 2: Autoindex HTML → `WebsiteLinkMeta` list

**Files:**
- Modify: `src/yai.hpp`
- Modify: `src/resolver_url.cpp`
- Modify: `tests/website_mtime_smoke.sh` (extend unit cases)

**Interfaces:**
- Consumes: `resolve_href_url`, `parse_directory_listing_mtime`, `website_link_stale_penalty`
- Produces:
  - `bool html_looks_like_directory_listing(const std::string& html);` — true if HTML contains Apache-style cues (`Parent Directory` / `Last modified` / `[DIR]` is enough)
  - `std::vector<WebsiteLinkMeta> html_directory_listing_links(const std::string& html, const std::string& base_url);`
    - For each table row with an `href` that is not `Parent Directory` / `?C=` sort links, resolve absolute URL
    - Attach mtime from the same row when a `YYYY-MM-DD HH:MM` token is present
    - Set `stale_penalty` from the resolved URL
  - Keep `html_href_urls` unchanged for non-listing pages

Representative row shape (KDE/Apache):

```html
<tr><td valign="top"><a href="5.3.2.1/"><img ... alt="[DIR]" /></a></td>
<td><a href="5.3.2.1/">5.3.2.1/</a></td>
<td align="right">2026-06-02 09:22  </td>
<td align="right">  - </td><td>&nbsp;</td></tr>
```

- [ ] **Step 1: Extend failing unit cases**

Append to the unit `main()` in `tests/website_mtime_smoke.sh`:

```cpp
    const std::string listing =
        "<html><body><table>"
        "<tr><th><a href=\"?C=N;O=D\">Name</a></th>"
        "<th><a href=\"?C=M;O=A\">Last modified</a></th></tr>"
        "<tr><td><a href=\"/stable/\">Parent Directory</a></td><td></td></tr>"
        "<tr><td><a href=\"5.3.2.1/\">5.3.2.1/</a></td>"
        "<td align=\"right\">2026-06-02 09:22  </td></tr>"
        "<tr><td><a href=\"6.0.2/\">6.0.2/</a></td>"
        "<td align=\"right\">2026-05-26 14:33  </td></tr>"
        "<tr><td><a href=\"older_versions_are_in_the_attic\">older</a></td>"
        "<td align=\"right\">2018-05-24 14:14  </td></tr>"
        "</table></body></html>";
    require(html_looks_like_directory_listing(listing), "detect listing");
    const auto links = html_directory_listing_links(
        listing, "file:///tmp/krita/");
    require(links.size() == 3, "three data rows");
    // Find 5.3.2.1 and 6.0.2 metas
    const WebsiteLinkMeta* v53 = nullptr;
    const WebsiteLinkMeta* v60 = nullptr;
    const WebsiteLinkMeta* old = nullptr;
    for (const auto& link : links) {
        if (link.url.find("5.3.2.1") != std::string::npos) v53 = &link;
        if (link.url.find("6.0.2/") != std::string::npos &&
            link.url.find("6.0.2.1") == std::string::npos) v60 = &link;
        if (link.url.find("older_versions") != std::string::npos) old = &link;
    }
    require(v53 && v60 && old, "rows present");
    require(v53->mtime.has_value() && v60->mtime.has_value() && *v53->mtime > *v60->mtime,
            "mtime newer for 5.3.2.1");
    require(old->stale_penalty == 1, "attic/older penalty");
    require(v53->stale_penalty == 0, "version dir not stale");
```

- [ ] **Step 2: Run to verify fail**

Run: `bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: FAIL — missing `html_directory_listing_links` / detect helper.

- [ ] **Step 3: Implement listing parse**

Prefer a robust approach:

1. If `!html_looks_like_directory_listing(html)`, return `{}` from `html_directory_listing_links` (caller will fall back).
2. Iterate `<tr>...</tr>` blocks with regex or string scan.
3. Inside each row, collect first meaningful `href` via existing href regex; skip empty, `?C=`, and parent-directory labels.
4. Search the row text for `(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2})`.
5. Push `WebsiteLinkMeta{resolve_href_url(base, href), mtime, stale_penalty}`.

Also add a thin wrapper used by website crawl later:

```cpp
std::vector<WebsiteLinkMeta> website_page_link_metas(
    const std::string& html,
    const std::string& base_url);
```

Behavior:

- If listing detection succeeds and `html_directory_listing_links` non-empty → return that.
- Else map each `html_href_urls(html, base_url)` entry to `WebsiteLinkMeta{url, nullopt, website_link_stale_penalty(url)}`.

- [ ] **Step 4: Run to verify pass**

Run: `bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: `website mtime unit smoke passed`

- [ ] **Step 5: Commit** (skip unless user asked)

---

### Task 3: Candidate comparator + selection helper

**Files:**
- Modify: `src/yai.hpp`
- Modify: `src/resolver_url.cpp`
- Modify: `tests/website_mtime_smoke.sh`

**Interfaces:**
- Consumes: `appimage_asset_score`, `basename_from_url`, `WebsiteLinkMeta`
- Produces:
  - `bool website_candidate_better(const WebsiteLinkMeta& a, const WebsiteLinkMeta& b, const std::string& arch);`
    - Reject either side with `appimage_asset_score < 0` as never better than a valid peer (caller filters invalid first)
    - Order: higher arch score → lower `stale_penalty` → newer known mtime (known beats unknown) → higher arch score already applied → stable false if equal
  - `std::string best_website_appimage_url(
        const std::vector<WebsiteLinkMeta>& candidates,
        const std::string& arch);` — returns best URL or `""`

Keep existing `best_appimage_url_from_candidates(vector<string>)` for landing-page HTML helpers (GitHub-unrelated but non-website-pool paths). Website crawl switches to the meta-aware API.

- [ ] **Step 1: Add failing comparator cases**

```cpp
    WebsiteLinkMeta newer{"file:///a/krita-5.3.2.1-x86_64.AppImage", 200, 0};
    WebsiteLinkMeta older{"file:///b/krita-6.0.2-x86_64.AppImage", 100, 0};
    WebsiteLinkMeta stale{"file:///old/krita-9.0.0-x86_64.AppImage", 300, 1};
    require(website_candidate_better(newer, older, "x86_64"), "mtime wins");
    require(website_candidate_better(newer, stale, "x86_64"), "non-stale wins");
    require(
        best_website_appimage_url({stale, older, newer}, "x86_64")
            .find("5.3.2.1") != std::string::npos,
        "best picks newer non-stale");
    require(
        best_website_appimage_url({stale}, "x86_64").find("9.0.0") != std::string::npos,
        "stale-only fallback");
```

- [ ] **Step 2: Run to verify fail**

Run: `bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: FAIL — missing comparator symbols.

- [ ] **Step 3: Implement comparator + best picker**

```cpp
bool website_candidate_better(
    const WebsiteLinkMeta& a,
    const WebsiteLinkMeta& b,
    const std::string& arch) {
    const std::string effective = arch.empty() ? current_arch() : normalize_arch(arch);
    const int sa = appimage_asset_score(basename_from_url(a.url), effective);
    const int sb = appimage_asset_score(basename_from_url(b.url), effective);
    if (sa != sb) {
        return sa > sb;
    }
    if (a.stale_penalty != b.stale_penalty) {
        return a.stale_penalty < b.stale_penalty;
    }
    if (a.mtime.has_value() != b.mtime.has_value()) {
        return a.mtime.has_value();
    }
    if (a.mtime.has_value() && b.mtime.has_value() && *a.mtime != *b.mtime) {
        return *a.mtime > *b.mtime;
    }
    return false;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: pass

- [ ] **Step 5: Commit** (skip unless user asked)

---

### Task 4: Wire website crawl queue + remove KDE reverse

**Files:**
- Modify: `src/resolver_website.cpp`
- Test: `tests/website_mtime_smoke.sh` (integration section added in Task 5; for this task re-run unit smoke + `tests/appimage_feed_smoke.sh` after compile)

**Interfaces:**
- Consumes: `website_page_link_metas`, `website_link_stale_penalty`, queue comparator fields
- Produces: updated crawl behavior

Changes in `resolver_website.cpp`:

1. Extend `WebsiteQueueItem`:

```cpp
struct WebsiteQueueItem {
    std::string url;
    int depth = 0;
    int priority = 0;
    bool speculative = false;
    std::uint64_t seq = 0;
    std::optional<std::int64_t> mtime;
    int stale_penalty = 0;
};
```

2. Update `queue_item_better`:

```cpp
bool queue_item_better(const WebsiteQueueItem& a, const WebsiteQueueItem& b) {
    if (a.stale_penalty != b.stale_penalty) {
        return a.stale_penalty < b.stale_penalty;
    }
    if (a.mtime.has_value() != b.mtime.has_value()) {
        return a.mtime.has_value();
    }
    if (a.mtime.has_value() && b.mtime.has_value() && *a.mtime != *b.mtime) {
        return *a.mtime > *b.mtime;
    }
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    return a.seq < b.seq;
}
```

3. Change `fetch_website_links` to return `std::optional<std::vector<WebsiteLinkMeta>>` (or a small struct `{metas}`). Remove:

```cpp
if (is_kde_stable_download_url(page_url)) {
    std::reverse(links.begin(), links.end());
}
```

Implementation sketch:

```cpp
std::optional<std::vector<WebsiteLinkMeta>> fetch_website_links(
    const std::string& page_url,
    bool speculative) {
    try {
        const int timeout_ms =
            speculative ? kFetchTextSpeculativeTimeoutMs : kFetchTextDefaultTimeoutMs;
        return website_page_link_metas(fetch_text(page_url, timeout_ms), page_url);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
```

4. When enqueueing discovered links, copy `mtime` / `stale_penalty` from the `WebsiteLinkMeta` (recompute penalty if missing). Seeds keep `mtime=nullopt`, `stale_penalty=website_link_stale_penalty(url)`.

5. Keep `state.appimage_urls` as `std::vector<WebsiteLinkMeta>` (rename to `appimage_candidates` in this file).

6. When recording an AppImage candidate from a meta link, inherit listing mtime/penalty; if the AppImage URL itself looks stale, set penalty to at least 1.

- [ ] **Step 1: Apply the wiring above** (still OK to early-return on first candidate temporarily so `appimage_feed_smoke` keeps working until Task 5)

- [ ] **Step 2: Build**

Run: `make -C /home/fsx/yai -j"$(nproc)"`

Expected: success

- [ ] **Step 3: Regression smoke**

Run: `YAI_LANG=en bash /home/fsx/yai/tests/appimage_feed_smoke.sh`

Expected: pass (update exact `after checking N page(s)` strings only if counts change)

- [ ] **Step 4: Commit** (skip unless user asked)

---

### Task 5: Candidate pool, early-stop, bounded HEAD, integration fixture

**Files:**
- Modify: `src/resolver_website.cpp`
- Modify: `src/yai.hpp` / `src/resolver_url.cpp` if HEAD helper is shared
- Modify: `tests/website_mtime_smoke.sh` (full integration)
- Possibly modify: `tests/appimage_feed_smoke.sh` (counts)

**Interfaces:**
- Consumes: `best_website_appimage_url`, `probe_url_freshness` or a thin HEAD helper
- Produces: final selection matching the spec

**HEAD helper** (add near website helpers or reuse freshness):

```cpp
std::optional<std::int64_t> probe_url_last_modified_mtime(const std::string& url);
```

Behavior:

- `file://` → `nullopt` (or file mtime if cheap; prefer `nullopt` to keep fixtures listing-driven)
- else `curl --head` like `probe_url_freshness`, parse `Last-Modified` via `parse_http_last_modified_mtime`
- on failure → `nullopt`

In `resolve_website_appimage_download`:

1. After processing each page’s metas, append AppImage candidates into the pool (do not return immediately).
2. Early-stop when:
   - `pages_checked >= kWebsiteMaxPages`, or
   - non-stale candidate count ≥ 8, or
   - there is ≥1 non-stale candidate **and** no remaining unseen queue item with `stale_penalty==0` and known `mtime` **newer** than the best non-stale candidate’s known mtime
3. Before final pick, among the top contenders lacking mtime (max 4), call `probe_url_last_modified_mtime` and fill meta.mtime when present.
4. Return `best_website_appimage_url(pool, arch)`.

- [ ] **Step 1: Write integration fixture section** at the end of `tests/website_mtime_smoke.sh`

Build a local tree:

```text
$TMP/krita/index.html          # autoindex with 5.3.2.1 (newer), 6.0.2 (older), older_versions_are_in_the_attic
$TMP/krita/5.3.2.1/index.html  # link to krita-5.3.2.1-x86_64.AppImage
$TMP/krita/6.0.2/index.html    # link to krita-6.0.2-x86_64.AppImage
$TMP/krita/older_versions_are_in_the_attic/index.html  # link to krita-old-x86_64.AppImage
$TMP/assets/*.AppImage         # tiny bash AppImages like other smokes
```

Register a `website_page` package whose `source.url` is `file://$TMP/krita/index.html`, with allowed host effectively local via `file://`. Install with `HOME=$TMP_HOME` and assert metadata `download_url` contains `5.3.2.1`.

Also assert stale-only package (source pointing only at attic index) still installs successfully.

Minimal index HTML for root (mtime columns required):

```html
<html><body><table>
<tr><th>Name</th><th>Last modified</th></tr>
<tr><td><a href="5.3.2.1/">5.3.2.1/</a></td><td align="right">2026-06-02 09:22  </td></tr>
<tr><td><a href="6.0.2/">6.0.2/</a></td><td align="right">2026-05-26 14:33  </td></tr>
<tr><td><a href="older_versions_are_in_the_attic/">older_versions_are_in_the_attic/</a></td>
    <td align="right">2018-05-24 14:14  </td></tr>
</table></body></html>
```

Child pages can be simple:

```html
<html><body><a href="file://$ASSETS/krita-5.3.2.1-x86_64.AppImage">AppImage</a></body></html>
```

Because depth is &lt; 2 from the package source, root (depth 0) → version dirs (depth 1) → AppImage links are candidates without needing another HTML hop when the child page is fetched at depth 1.

- [ ] **Step 2: Run integration to verify fail**

Run: `make -C /home/fsx/yai -j"$(nproc)" && bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: FAIL — still picks wrong version or early-exits on 6.0.2 / attic depending on crawl order.

- [ ] **Step 3: Implement pool + early-stop + HEAD fill-in**

Replace `selected_website_candidate` usage that returns on first hit with pool + stop predicates described above. Progress `selected(...)` fires once at the real return.

- [ ] **Step 4: Run all relevant smokes**

```bash
make -C /home/fsx/yai -j"$(nproc)"
bash /home/fsx/yai/tests/website_mtime_smoke.sh
YAI_LANG=en bash /home/fsx/yai/tests/appimage_feed_smoke.sh
```

Expected: all pass; Krita-like fixture selects `5.3.2.1`.

- [ ] **Step 5: Commit** (skip unless user asked)

---

## Placeholder / consistency self-review

1. **Spec coverage:** stale demotion, listing mtime parse, queue ordering, remove KDE reverse, candidate pool/early-stop, HEAD ≤4, stale-only fallback, fixture tests — mapped to Tasks 1–5.
2. **Placeholders:** none intentionally left; concrete APIs and fixture shapes included.
3. **Type consistency:** `WebsiteLinkMeta` / `website_candidate_better` / `best_website_appimage_url` / `website_page_link_metas` names are shared across tasks.
4. **Commits:** skipped by default per Global Constraints / user rules.

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-07-26-website-mtime-candidate.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — execute tasks in this session with executing-plans checkpoints  

Which approach?
