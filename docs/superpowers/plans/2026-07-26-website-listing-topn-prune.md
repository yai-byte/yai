# Website listing top-N follow prune Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On Apache-style directory listings with known mtimes, enqueue only the newest 3 non-stale follow dirs (plus at most 1 stale fallback) so multi-version website AppImage sniffing fetches fewer pages.

**Architecture:** Add pure helpers that decide whether listing prune applies and which follow `WebsiteLinkMeta`s survive. Wire them into `collect_appimage_candidates` so AppImage URLs are never pruned, while version-dir follows are capped. Keep trust, depth, concurrency, early-stop, and final selection unchanged.

**Tech Stack:** C++17, existing website resolver + `file://` fixtures, shell smokes under `tests/`, Makefile.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-26-website-listing-topn-prune-design.md`
- Non-stale follow cap = 3; stale follow cap = 1; AppImage direct links never pruned
- Freshness signal remains listing mtime + `stale_penalty` (not semver)
- Trust filters, depth &lt; 2, queue ≤ 128, ≤ 96 pages, concurrency 4, catalog bridge, GitHub selection unchanged
- msgid = English; sync `po/en.po` / `po/zh.po` only if new user-facing strings appear
- Do not commit unless the user explicitly asks; skip commit steps when commits are not requested

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | Declare prune helpers + optional `website_url_priority` if moved for tie-break |
| `src/resolver_url.cpp` | Implement follow comparator + prune select helpers |
| `src/resolver_website.cpp` | Use prune when enqueueing follows from a page; keep AppImage path unpruned |
| `tests/website_mtime_smoke.sh` | Unit cases for prune + integration fixture with ≥5 version dirs |
| `tests/appimage_feed_smoke.sh` | Update checked/queued assertions only if counts change |

---

### Task 1: Pure listing-follow prune helpers (unit smoke)

**Files:**
- Modify: `src/yai.hpp` (near `WebsiteLinkMeta` / listing helpers)
- Modify: `src/resolver_url.cpp`
- Modify: `tests/website_mtime_smoke.sh` (unit `main()` only)

**Interfaces:**
- Consumes: `WebsiteLinkMeta`, `website_link_stale_penalty` (already on metas), path priority logic
- Produces:
  - `int website_url_priority(const std::string& url);` — move or duplicate the existing logic from `resolver_website.cpp` (appimage=3, download=2, linux=1, else 0; path after host, case-insensitive). If moved, delete the anonymous-namespace copy in `resolver_website.cpp` and call the shared symbol.
  - `bool website_follow_meta_better(const WebsiteLinkMeta& a, const WebsiteLinkMeta& b);` — lower `stale_penalty`, then known mtime beats unknown, then newer `mtime`, then higher `website_url_priority(url)`, then `false` if equal
  - `bool website_listing_follow_prune_applies(const std::vector<WebsiteLinkMeta>& follow_metas);` — true iff ≥1 meta has `stale_penalty == 0` and `mtime.has_value()`
  - `std::vector<WebsiteLinkMeta> select_listing_follow_metas_for_enqueue(const std::vector<WebsiteLinkMeta>& follow_metas, std::size_t non_stale_max = 3, std::size_t stale_max = 1);`
    - If `!website_listing_follow_prune_applies(follow_metas)` → return `follow_metas` unchanged (all entries, original order)
    - Else:
      - Partition into non-stale and stale
      - From non-stale: keep only those with `mtime.has_value()`; sort with `website_follow_meta_better`; take first `non_stale_max`
      - From stale: sort with `website_follow_meta_better`; take first `stale_max`
      - Return non-stale survivors then stale survivors (stable within each group after sort)

- [ ] **Step 1: Write the failing unit cases**

Append to the unit `main()` in `tests/website_mtime_smoke.sh` before the final `std::cout`:

```cpp
    // Listing top-N follow prune
    require(website_url_priority("https://x.example/download/foo") == 2, "priority download");
    require(website_url_priority("https://x.example/AppImage/bar") == 3, "priority appimage");
    require(website_url_priority("https://x.example/other") == 0, "priority other");

    WebsiteLinkMeta d1{"file:///download/v1/", 100, 0};
    WebsiteLinkMeta d2{"file:///download/v2/", 200, 0};
    WebsiteLinkMeta d3{"file:///download/v3/", 300, 0};
    WebsiteLinkMeta d4{"file:///download/v4/", 400, 0};
    WebsiteLinkMeta d5{"file:///download/v5/", 500, 0};
    WebsiteLinkMeta no_mt{"file:///download/unknown/", std::nullopt, 0};
    WebsiteLinkMeta attic{"file:///download/older_versions_are_in_the_attic/", 50, 1};
    WebsiteLinkMeta attic2{"file:///download/old/archive/", 40, 1};

    require(website_follow_meta_better(d5, d4), "newer follow better");
    require(website_follow_meta_better(d1, attic), "non-stale follow better");
    require(website_follow_meta_better(d2, no_mt), "known mtime beats unknown");

    require(!website_listing_follow_prune_applies({no_mt, attic}), "no prune without non-stale mtime");
    require(website_listing_follow_prune_applies({d1, no_mt}), "prune applies with one timed non-stale");

    const auto kept = select_listing_follow_metas_for_enqueue(
        {d1, d2, d3, d4, d5, no_mt, attic, attic2}, 3, 1);
    require(kept.size() == 4, "3 non-stale + 1 stale");
    require(kept[0].url.find("v5") != std::string::npos, "first is newest");
    require(kept[1].url.find("v4") != std::string::npos, "second");
    require(kept[2].url.find("v3") != std::string::npos, "third");
    require(kept[3].stale_penalty == 1, "one stale kept");
    require(kept[3].url.find("older_versions") != std::string::npos, "best stale by mtime");
    for (const auto& m : kept) {
        require(m.url.find("unknown") == std::string::npos, "unknown mtime dropped when prune on");
        require(m.url.find("/old/archive") == std::string::npos, "second stale dropped");
    }

    const auto passthrough = select_listing_follow_metas_for_enqueue({no_mt, attic}, 3, 1);
    require(passthrough.size() == 2, "no-prune passthrough keeps all");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: FAIL — missing symbols / compile errors for undeclared prune helpers (or link errors).

- [ ] **Step 3: Declare and implement helpers**

In `src/yai.hpp`, declare the four APIs above.

In `src/resolver_url.cpp`, implement them. For `website_url_priority`, use the same path-extraction logic currently in `resolver_website.cpp` (strip fragment/query, take path after `://` host, `to_lower`, then substring checks).

Sketch for select:

```cpp
std::vector<WebsiteLinkMeta> select_listing_follow_metas_for_enqueue(
    const std::vector<WebsiteLinkMeta>& follow_metas,
    std::size_t non_stale_max,
    std::size_t stale_max) {
    if (!website_listing_follow_prune_applies(follow_metas)) {
        return follow_metas;
    }
    std::vector<WebsiteLinkMeta> non_stale;
    std::vector<WebsiteLinkMeta> stale;
    for (const WebsiteLinkMeta& meta : follow_metas) {
        if (meta.stale_penalty != 0) {
            stale.push_back(meta);
        } else if (meta.mtime.has_value()) {
            non_stale.push_back(meta);
        }
    }
    auto by_better = [](const WebsiteLinkMeta& a, const WebsiteLinkMeta& b) {
        return website_follow_meta_better(a, b);
    };
    std::stable_sort(non_stale.begin(), non_stale.end(), by_better);
    std::stable_sort(stale.begin(), stale.end(), by_better);
    if (non_stale.size() > non_stale_max) {
        non_stale.resize(non_stale_max);
    }
    if (stale.size() > stale_max) {
        stale.resize(stale_max);
    }
    non_stale.insert(non_stale.end(), stale.begin(), stale.end());
    return non_stale;
}
```

If `website_url_priority` is moved out of `resolver_website.cpp`, update that file to call the shared function and remove the local duplicate.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: `website mtime unit smoke passed` and existing integration section still passes.

- [ ] **Step 5: Commit** (skip unless user asked)

---

### Task 2: Wire prune into website follow enqueue

**Files:**
- Modify: `src/resolver_website.cpp` (`collect_appimage_candidates`)
- Test: re-run `tests/website_mtime_smoke.sh` + `tests/appimage_feed_smoke.sh`

**Interfaces:**
- Consumes: `website_listing_follow_prune_applies`, `select_listing_follow_metas_for_enqueue`
- Produces: fewer queued version dirs on listing pages; AppImage candidates unchanged

Constants in `resolver_website.cpp` (file scope):

```cpp
constexpr std::size_t kWebsiteListingFollowNonStaleMax = 3;
constexpr std::size_t kWebsiteListingFollowStaleMax = 1;
```

- [ ] **Step 1: Refactor `collect_appimage_candidates` to prune follows**

Replace the single loop that both records AppImages and queues follows with a two-phase approach:

```cpp
void collect_appimage_candidates(
    const std::vector<WebsiteLinkMeta>& links,
    const RepoPackage& package,
    const WebsiteQueueItem& page,
    WebsiteSearchState& state,
    WebsiteSearchProgress& progress,
    const std::string& arch) {
    std::vector<WebsiteLinkMeta> follow_metas;
    for (const WebsiteLinkMeta& meta : links) {
        if (is_allowed_appimage_candidate(meta.url, package, state.allowed_hosts)) {
            WebsiteLinkMeta candidate = meta;
            candidate.url = resolved_appimage_candidate(meta.url, package, arch);
            record_appimage_candidate(std::move(candidate), page, state, progress);
            continue;
        }
        if (should_queue_website_link(meta.url, package, page, state)) {
            follow_metas.push_back(meta);
        }
    }

    const std::vector<WebsiteLinkMeta> to_queue =
        select_listing_follow_metas_for_enqueue(
            follow_metas,
            kWebsiteListingFollowNonStaleMax,
            kWebsiteListingFollowStaleMax);
    for (const WebsiteLinkMeta& meta : to_queue) {
        // Re-check should_queue: queue growth / seen set may have changed while
        // recording AppImages; also prune may keep URLs that later fail host rules
        // only if should_queue already passed — still re-check depth/seen/queue cap.
        if (should_queue_website_link(meta.url, package, page, state)) {
            queue_website_link(meta, package, page, state, progress);
        }
    }
}
```

Notes:

- Do **not** apply prune to AppImage candidates.
- `select_listing_follow_metas_for_enqueue` already no-ops when prune does not apply, so non-listing / no-mtime pages keep full follow enqueue.
- Catalog handoff inside `queue_website_link` stays as-is.

- [ ] **Step 2: Build**

Run: `make -C /home/fsx/yai -j"$(nproc)"`

Expected: success

- [ ] **Step 3: Regression smokes**

Run:

```bash
bash /home/fsx/yai/tests/website_mtime_smoke.sh
YAI_LANG=en bash /home/fsx/yai/tests/appimage_feed_smoke.sh
```

Expected: both pass. If `appimage_feed_smoke` hard-codes checked/queued counts that change, update those assertions to the new deterministic counts only.

- [ ] **Step 4: Commit** (skip unless user asked)

---

### Task 3: Integration fixture proving top-N prune

**Files:**
- Modify: `tests/website_mtime_smoke.sh` (new integration block at end)

**Interfaces:**
- Consumes: wired prune from Task 2
- Produces: end-to-end proof that dirs beyond top-3 are not checked

- [ ] **Step 1: Add prune integration fixture**

After the existing integration section (or as a second package in the same repo index), build:

```text
$TMP/download/prune/index.html   # autoindex: v5..v1 (mtimes 500..100) + older_versions attic
$TMP/download/prune/v5/index.html  # → prune-v5 AppImage
$TMP/download/prune/v4/index.html  # → prune-v4 AppImage
$TMP/download/prune/v3/index.html  # → prune-v3 AppImage
$TMP/download/prune/v2/index.html  # → prune-v2 AppImage (must NOT be checked if prune works)
$TMP/download/prune/v1/index.html  # → prune-v1 AppImage (must NOT be checked)
$TMP/download/prune/older_versions_are_in_the_attic/index.html  # stale AppImage OK as unused fallback
```

Root listing rows (path under `.../download/prune/` so `should_follow_download_page` accepts children). Use `href="vN/index.html"` like the existing Krita fixture. Mtimes:

| Entry | Last modified |
|-------|---------------|
| v5/ | 2026-06-05 12:00 |
| v4/ | 2026-06-04 12:00 |
| v3/ | 2026-06-03 12:00 |
| v2/ | 2026-06-02 12:00 |
| v1/ | 2026-06-01 12:00 |
| older_versions_are_in_the_attic/ | 2018-01-01 00:00 |

Package id e.g. `mtime-listing-prune` with `source.type=website_page` and `url=file://$PRUNE/index.html`.

Install with `HOME=$TMP_HOME`, capture stderr to `$TMP_DIR/prune.err`.

Assertions:

1. `metadata.json` `download_url` contains `v5` (or the v5 AppImage basename).
2. stderr / progress must **not** contain `v2/index.html` or `v1/index.html` as checked pages (match the same progress wording used by `WebsiteSearchProgress` today — inspect a failing run if unsure; typically the checked URL appears on the progress line).
3. Optional: stderr **may** contain `v5` / `v4` / `v3`; at least `v5` must have been checked or selected.

Also add a **no-mtime no-prune** mini case: listing table **without** date columns / without parseable `YYYY-MM-DD HH:MM` on rows, with a single child `deep/index.html` that has the AppImage; install must still succeed (proves prune did not drop unknown-mtime follows). Can be package `mtime-listing-nomtime` in the same index.

- [ ] **Step 2: Run to verify fail before Task 2 wiring** (if executing TDD strictly before Task 2, run after Task 1 only)

If Task 2 is already done, skip “fail first” and go to Step 3. If executing in order before wiring: expect install still checks `v2`/`v1` (assertion 2 fails).

- [ ] **Step 3: Ensure Task 2 is complete; re-run full smoke**

Run: `make -C /home/fsx/yai -j"$(nproc)" && bash /home/fsx/yai/tests/website_mtime_smoke.sh`

Expected: unit + old integration + prune integration + nomtime case all pass; print a clear `website listing prune integration smoke passed` (or fold into the existing final echo).

- [ ] **Step 4: Regression**

Run: `YAI_LANG=en bash /home/fsx/yai/tests/appimage_feed_smoke.sh`

Expected: pass

- [ ] **Step 5: Commit** (skip unless user asked)

---

## Placeholder / consistency self-review

1. **Spec coverage:** trigger (non-stale+mtime), N=3, stale max 1, AppImage never pruned, no-mtime passthrough, wiring, integration proving v1/v2 unchecked, regressions — mapped to Tasks 1–3.
2. **Placeholders:** none; concrete APIs, code sketches, fixture layout included.
3. **Type consistency:** `select_listing_follow_metas_for_enqueue` / `website_listing_follow_prune_applies` / `website_follow_meta_better` / `website_url_priority` names shared across tasks; caps `kWebsiteListingFollowNonStaleMax=3`, `kWebsiteListingFollowStaleMax=1`.
4. **Commits:** skipped by default per Global Constraints / user rules.

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-07-26-website-listing-topn-prune.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — execute tasks in this session with executing-plans checkpoints  

Which approach?
