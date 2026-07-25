# Website Search Speed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Speed up `website_page` AppImage discovery with timed fetches, capped landing probes (no full AppImage body download), priority queuing, and bounded parallel page fetches—without changing trust boundaries or update/selection semantics.

**Architecture:** Extend `fetch_text` with timeout/`--max-time` (and a capped-body helper for landing probes). Teach `resolver_website.cpp` to tag speculative common-path URLs, score queue priority, skip unbounded probes of `.AppImage` URLs (use capped prefix fetch only when the old landing-probe heuristic matches), and fetch up to 4 unseen queue pages concurrently via `std::thread` before deterministic per-page processing and early exit.

**Tech Stack:** C++17, process `curl`, existing `run_process_capture_timeout`, `std::thread`, shell smoke tests under `tests/`, Makefile.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-25-website-search-speed-design.md`
- **Interpretation of “do not fetch_text `.AppImage`”:** never download an unbounded AppImage body during website search. URLs matching the existing landing heuristic (`is_appimage_download_url` + `download` in path + package name match) may use a **capped prefix fetch** (Range / `--max-filesize` ≤ 512 KiB, 15s timeout) so HTML landings like MuseScore still resolve before install/update identity compares; non-matching `.AppImage` URLs are never fetched during search
- Trust filters, depth &lt; 2, queue ≤ 128, ≤ 96 pages checked, catalog one-hop bridge unchanged
- Concurrency = 4; default text timeout = 15000 ms; speculative common-path timeout = 5000 ms
- `stage_appimage_source` install-time landing follow unchanged
- No resolve-result disk cache; no libcurl
- msgid = English; sync `po/en.po` / `po/zh.po` only if new user-facing strings appear
- Do not commit unless the user explicitly asks; skip commit steps when commits are not requested

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | Declare timed `fetch_text` overloads / `fetch_text_limited` |
| `src/cli_download.cpp` | Implement timed + size-capped curl text fetch; treat `timed_out` as failure |
| `src/resolver_website.cpp` | Priority queue, speculative flag, capped landing probe, parallel batch crawl |
| `tests/appimage_feed_smoke.sh` | Update counts; assert `.AppImage` assets are not full-fetched during search |
| `README.md` / `AppImage包管理器开发文档.md` | One short note on parallel website search + timeouts |

---

### Task 1: Timed and size-capped `fetch_text`

**Files:**
- Modify: `src/yai.hpp` (around the `fetch_text` declaration)
- Modify: `src/cli_download.cpp` (`fetch_text` implementation)
- Test: `tests/fetch_text_timeout_smoke.sh` (new)

**Interfaces:**
- Consumes: `run_process_capture_timeout`, `ProcessResult::timed_out`
- Produces:
  - `std::string fetch_text(const std::string& url);` — default timeout 15000 ms
  - `std::string fetch_text(const std::string& url, int timeout_ms);`
  - `std::string fetch_text_limited(const std::string& url, int timeout_ms, std::uintmax_t max_bytes);` — curl `--max-time` (seconds, ceil), `--max-filesize max_bytes`, and for non-`file://` also `-r 0-(max_bytes-1)` when `max_bytes > 0`

- [ ] **Step 1: Write the failing smoke test**

Create `tests/fetch_text_timeout_smoke.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
trap 'rm -rf "$TMP_HOME"' EXIT
FAKE_BIN="$TMP_HOME/bin"
mkdir -p "$FAKE_BIN"
# Log argv; hang on --max-time 1 targets that include /hang
cat > "$FAKE_BIN/curl" <<'SH'
#!/usr/bin/env bash
printf 'curl\t%s\n' "$*" >> "${FAKE_CURL_LOG:?}"
for arg in "$@"; do
  if [[ "$arg" == *"/hang"* ]]; then
    sleep 30
    exit 0
  fi
done
# Echo tiny HTML for anything else
echo '<html><body><a href="x">x</a></body></html>'
exit 0
SH
chmod +x "$FAKE_BIN/curl"
export PATH="$FAKE_BIN:$PATH"
export FAKE_CURL_LOG="$TMP_HOME/curl.log"
# Drive fetch via a tiny helper binary is heavy; instead assert yai website path
# uses --max-time by installing a website_page package whose only page hangs.
# Simpler: unit-level check that compiled yai passes --max-time when resolving.
# Use repo package pointing at https://example.invalid/hang (fake curl sleeps).
mkdir -p "$TMP_HOME/.local/share/yai/repos"
cat > "$TMP_HOME/.local/share/yai/repos/index.json" <<'JSON'
{
  "version": 1,
  "updated_at": "test",
  "packages": [
    {
      "id": "hang-site",
      "name": "Hang Site",
      "summary": "timeout probe",
      "homepage": "https://example.invalid/hang",
      "license": "MIT",
      "source": {
        "type": "website_page",
        "url": "https://example.invalid/hang",
        "reason": "test"
      }
    }
  ]
}
JSON
# Ensure config lists default repo so index is used — match how other smokes set HOME-only index.
# If hang-site install fails quickly (timeout/skip then "no AppImage"), that proves timeout works.
set +e
HOME="$TMP_HOME" "$ROOT/yai" install hang-site 2>"$TMP_HOME/err"
code=$?
set -e
grep -q -- '--max-time' "$FAKE_CURL_LOG"
# Must not sleep the full 30s: wall clock under 20s is enough signal when combined with --max-time in log
test "$code" -ne 0
```

Wire the test into whatever target runs smokes (if `Makefile` lists smoke scripts explicitly, add this file next to `appimage_feed_smoke.sh`).

- [ ] **Step 2: Run test to verify it fails**

Run: `make -C /home/fsx/yai && bash /home/fsx/yai/tests/fetch_text_timeout_smoke.sh`

Expected: FAIL — current `fetch_text` has no `--max-time`, and/or hang lasts too long / missing log.

- [ ] **Step 3: Implement timed + limited fetch**

In `yai.hpp` replace `std::string fetch_text(const std::string& url);` with:

```cpp
constexpr int kFetchTextDefaultTimeoutMs = 15000;
constexpr int kFetchTextSpeculativeTimeoutMs = 5000;
constexpr std::uintmax_t kFetchTextLandingMaxBytes = 512ull * 1024ull;

std::string fetch_text(const std::string& url);
std::string fetch_text(const std::string& url, int timeout_ms);
std::string fetch_text_limited(
    const std::string& url,
    int timeout_ms,
    std::uintmax_t max_bytes);
```

In `cli_download.cpp`:

```cpp
namespace {

int curl_max_time_seconds(int timeout_ms) {
    if (timeout_ms <= 0) {
        return 1;
    }
    return (timeout_ms + 999) / 1000;
}

std::vector<std::string> fetch_text_curl_args(
    const std::string& url,
    int timeout_ms,
    std::optional<std::uintmax_t> max_bytes) {
    std::vector<std::string> args = {
        "curl",
        "--fail",
        "--location",
        "--silent",
        "--show-error",
        "--max-time",
        std::to_string(curl_max_time_seconds(timeout_ms)),
        "--header",
        "Accept: application/vnd.github+json",
    };
    if (max_bytes.has_value() && *max_bytes > 0) {
        args.push_back("--max-filesize");
        args.push_back(std::to_string(*max_bytes));
        if (!is_file_url(url)) {
            args.push_back("-r");
            args.push_back("0-" + std::to_string(*max_bytes - 1));
        }
    }
    args.push_back(url);
    return args;
}

}  // namespace

std::string fetch_text(const std::string& url) {
    return fetch_text(url, kFetchTextDefaultTimeoutMs);
}

std::string fetch_text(const std::string& url, int timeout_ms) {
    return fetch_text_limited(url, timeout_ms, 0);
}

std::string fetch_text_limited(
    const std::string& url,
    int timeout_ms,
    std::uintmax_t max_bytes) {
    const std::optional<std::uintmax_t> limit =
        max_bytes == 0 ? std::nullopt : std::optional<std::uintmax_t>{max_bytes};
    const ProcessResult result = run_process_capture_timeout(
        fetch_text_curl_args(url, timeout_ms, limit),
        timeout_ms + 2000);
    if (result.timed_out || result.exit_code != 0) {
        throw std::runtime_error(tr("failed to fetch ") + url + tr(": ") + result.output);
    }
    return result.output;
}
```

Note: when `max_bytes == 0`, do not pass `--max-filesize` / `-r` (unlimited body, still timed).

Ensure `is_file_url` is visible from `cli_download.cpp` (already declared in `yai.hpp`).

- [ ] **Step 4: Run test to verify it passes**

Run: `make -C /home/fsx/yai && bash /home/fsx/yai/tests/fetch_text_timeout_smoke.sh`

Expected: PASS (`--max-time` logged; install fails without hanging ~30s).

- [ ] **Step 5: Commit** (only if user asked)

```bash
git add src/yai.hpp src/cli_download.cpp tests/fetch_text_timeout_smoke.sh Makefile
git commit -m "$(cat <<'EOF'
Add timed and size-capped fetch_text for website search.

EOF
)"
```

---

### Task 2: Safe landing probe + priority queue + speculative timeouts

**Files:**
- Modify: `src/resolver_website.cpp`
- Test: extend `tests/appimage_feed_smoke.sh` (AppImage not full-fetched)

**Interfaces:**
- Consumes: `fetch_text(url, timeout_ms)`, `fetch_text_limited(...)`, `kFetchText*`
- Produces: updated `WebsiteQueueItem{url, depth, priority, speculative, seq}`; `website_url_priority(url)`; capped `resolved_appimage_candidate`

- [ ] **Step 1: Extend smoke to detect full AppImage GET during website search**

In `tests/appimage_feed_smoke.sh`, before `install no-github-app`, install a PATH wrapper that logs curl URLs (preserve real curl for non-log path). Pattern:

```bash
REAL_CURL="$(command -v curl)"
FAKE_BIN="$TMP_HOME/fake-bin"
mkdir -p "$FAKE_BIN"
cat > "$FAKE_BIN/curl" <<SH
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$TMP_HOME/website-curl.log"
exec "$REAL_CURL" "\$@"
SH
chmod +x "$FAKE_BIN/curl"
PATH="$FAKE_BIN:$PATH" HOME="$TMP_HOME" "$ROOT/yai" install no-github-app 2>"$TMP_HOME/website_install.err"
# Direct AppImage asset must not appear as a curl URL argument during search
# (staging download may still fetch it afterward — assert only before "Downloading" or
# check that WebsiteFeed asset URL never appears with fetch_text-style args before install stages).
if grep -F "$ORIGINAL_ROOT/$WEBSITE_ASSET" "$TMP_HOME/website-curl.log"; then
  # Allow later download_file/aria lines; require that no fetch_text-style invocation
  # listed the asset WITHOUT an output -o/--output (fetch_text has no -o).
  if grep -F "$ORIGINAL_ROOT/$WEBSITE_ASSET" "$TMP_HOME/website-curl.log" | grep -v -- '-o' | grep -v -- '--output' | grep -q .; then
    echo "website search full-text-fetched AppImage candidate" >&2
    exit 1
  fi
fi
```

Also keep / adjust the existing `after checking …` assertion after implementing Task 3 (may change in Task 3); for this task, primarily add the AppImage fetch guard and ensure MuseScore still ends with real asset `download_url`.

- [ ] **Step 2: Run relevant smoke; expect AppImage probe failure or still-pass depending on current code**

Run: `make -C /home/fsx/yai && bash /home/fsx/yai/tests/appimage_feed_smoke.sh`

Expected: likely FAIL on new AppImage full-fetch assertion if MuseScore/no-github probes fire; or FAIL only when probe hits. If `no-github-app` asset path lacks `download`, assertion may already pass for that package—still run MuseScore path. Prefer forcing failure by temporarily relying on MuseScore landing probe: assert `musescore-x86_64.AppImage` landing path may be Range/`--max-filesize` fetched but `$MUSESCORE_ASSET` real binary must not appear in fetch_text-style curl lines during `install musescore` search phase.

- [ ] **Step 3: Implement queue item fields, priority, speculative common URLs, capped probe**

Replace `WebsiteQueueItem` and helpers in `resolver_website.cpp` (anonymous namespace):

```cpp
struct WebsiteQueueItem {
    std::string url;
    int depth = 0;
    int priority = 0;
    bool speculative = false;
    std::uint64_t seq = 0;
};

int website_url_priority(const std::string& url) {
    const std::string lower = to_lower(strip_url_fragment_query(url));
    if (lower.find("appimage") != std::string::npos) {
        return 3;
    }
    if (lower.find("download") != std::string::npos) {
        return 2;
    }
    if (lower.find("linux") != std::string::npos) {
        return 1;
    }
    return 0;
}

constexpr int kWebsiteSeedPriority = 100;

bool queue_item_better(const WebsiteQueueItem& a, const WebsiteQueueItem& b) {
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    return a.seq < b.seq;
}
```

Add `std::uint64_t next_seq = 0;` to `WebsiteSearchState`.

Update `queue_website_url` to accept `priority`, `speculative`, assign `seq = state.next_seq++`, and insert so the vector stays unordered but selection sorts—or insert maintaining order. Selection in Task 3 will sort; for Task 2 serial loop, when picking `state.queue[index]` change to “pick best pending unseen” OR keep serial index scan but **re-sort queue** after each enqueue of discovered links:

After any discovered enqueue, `std::stable_sort` is wrong for mixed processed prefix. Better: keep `queue` as a bag; main loop (Task 3) selects best unused. For Task 2 keep serial but change loop to:

```cpp
while (checked_total < 96) {
  // find best not-seen item in queue
  ...
}
```

Implement that selection helper now (reuse in Task 3):

```cpp
std::optional<std::size_t> next_website_queue_index(const WebsiteSearchState& state) {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < state.queue.size(); ++i) {
        if (vector_contains(state.seen, state.queue[i].url)) {
            continue;
        }
        if (!best.has_value() || queue_item_better(state.queue[i], state.queue[*best])) {
            best = i;
        }
    }
    return best;
}
```

`initial_website_search_state`: enqueue hints/source/homepage with `priority = kWebsiteSeedPriority`, `speculative = false`, preserving current order via increasing `seq`.

`queue_common_project_download_urls`: enqueue with `speculative = true`, `priority = website_url_priority(url)` (not seed priority).

`queue_website_link` / `queue_website_url` for discovered pages: `priority = website_url_priority(url)`, `speculative = false`.

`fetch_website_links(page_url, progress, speculative)`:

```cpp
const int timeout_ms =
    speculative ? kFetchTextSpeculativeTimeoutMs : kFetchTextDefaultTimeoutMs;
std::vector<std::string> links = html_href_urls(fetch_text(page_url, timeout_ms), page_url);
```

`resolved_appimage_candidate`:

```cpp
std::string resolved_appimage_candidate(
    const std::string& link,
    const RepoPackage& package,
    const std::string& arch) {
    if (!should_probe_appimage_landing_url(link, package)) {
        return link;
    }
    try {
        const std::string html = fetch_text_limited(
            link,
            kFetchTextDefaultTimeoutMs,
            kFetchTextLandingMaxBytes);
        const std::string landing_url =
            appimage_url_from_download_landing_html(html, link, arch);
        if (!landing_url.empty()) {
            return landing_url;
        }
    } catch (const std::exception&) {
    }
    return link;
}
```

Change `resolve_website_appimage_download` loop to use `next_website_queue_index` instead of raw `index++` (still serial in this task).

- [ ] **Step 4: Re-run smokes**

Run: `make -C /home/fsx/yai && bash /home/fsx/yai/tests/appimage_feed_smoke.sh && bash /home/fsx/yai/tests/fetch_text_timeout_smoke.sh`

Expected: PASS — MuseScore still resolves to real `$MUSESCORE_ASSET` in metadata; no unbounded fetch_text of real AppImage bodies during search.

- [ ] **Step 5: Commit** (only if user asked)

```bash
git add src/resolver_website.cpp tests/appimage_feed_smoke.sh
git commit -m "$(cat <<'EOF'
Prioritize website crawl queue and cap AppImage landing probes.

EOF
)"
```

---

### Task 3: Bounded parallel page fetch (concurrency 4)

**Files:**
- Modify: `src/resolver_website.cpp` (`resolve_website_appimage_download`)
- Modify: `tests/appimage_feed_smoke.sh` (update `after checking N page(s)…` if counts change)

**Interfaces:**
- Consumes: `next_website_queue_index` / priority fields, `fetch_text(url, timeout_ms)`, `std::thread`
- Produces: batch fetch of up to 4 pages; deterministic process order = batch selection order; early exit unchanged

- [ ] **Step 1: Document expected count change by running once after coding, then lock assertion**

Do not hardcode a guessed count in the plan assertion until measured. After implementation, run `install no-github-app` and set:

```bash
grep -q "after checking .* page(s), queued .*, skipped .*" "$TMP_HOME/website_install.err"
grep -q "WebsiteFeed-x86_64.AppImage" "$TMP_HOME/website_install.err"
```

If the suite previously required exact `after checking 3 page(s), queued 3, skipped 1`, replace with the new deterministic string observed from a single local run (or loosen to regex while still requiring `skipped` ≥ 1 and presence of selected asset). Prefer keeping an exact string once measured so regressions stay visible.

- [ ] **Step 2: Implement parallel batch loop**

In `resolve_website_appimage_download`:

```cpp
constexpr std::size_t kWebsiteFetchConcurrency = 4;
constexpr std::size_t kWebsiteMaxPages = 96;

std::size_t pages_checked = 0;
while (pages_checked < kWebsiteMaxPages) {
    std::vector<WebsiteQueueItem> batch;
    std::vector<std::string> batch_seen_mark;
    while (batch.size() < kWebsiteFetchConcurrency && pages_checked + batch.size() < kWebsiteMaxPages) {
        const std::optional<std::size_t> idx = next_website_queue_index(state);
        if (!idx.has_value()) {
            break;
        }
        WebsiteQueueItem page = state.queue[*idx];
        // Temporarily mark as seen so next_website_queue_index skips it within batch building
        state.seen.push_back(page.url);
        batch_seen_mark.push_back(page.url);
        batch.push_back(page);
    }
    if (batch.empty()) {
        break;
    }

    struct FetchOutcome {
        std::optional<std::vector<std::string>> links;
        bool skipped = false;
    };
    std::vector<FetchOutcome> outcomes(batch.size());
    std::vector<std::thread> workers;
    workers.reserve(batch.size());
    for (std::size_t i = 0; i < batch.size(); ++i) {
        workers.emplace_back([&, i]() {
            progress.checked(batch[i].url); // progress must be synchronized — see note
            try {
                const int timeout_ms = batch[i].speculative
                    ? kFetchTextSpeculativeTimeoutMs
                    : kFetchTextDefaultTimeoutMs;
                outcomes[i].links = html_href_urls(
                    fetch_text(batch[i].url, timeout_ms),
                    batch[i].url);
                if (outcomes[i].links.has_value() && is_kde_stable_download_url(batch[i].url)) {
                    std::reverse(outcomes[i].links->begin(), outcomes[i].links->end());
                }
            } catch (const std::exception&) {
                outcomes[i].skipped = true;
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    for (std::size_t i = 0; i < batch.size(); ++i) {
        ++pages_checked;
        if (outcomes[i].skipped || !outcomes[i].links.has_value()) {
            progress.skipped();
            continue;
        }
        collect_appimage_candidates(
            *outcomes[i].links, package, batch[i], state, progress, arch);
        const std::string best = selected_website_candidate(state.appimage_urls, arch, progress);
        if (!best.empty()) {
            return best;
        }
    }
}
throw std::runtime_error(tr("no AppImage download link found on website pages for ") + package.id);
```

**Progress / thread safety:** `WebsiteSearchProgress` writes to `std::cerr` without a mutex. Either:

1. Do not call `progress.checked` inside workers—call `progress.checked(url)` on the main thread when building the batch (before spawn), and only set `skipped` on main after join; or
2. Add a `std::mutex` inside `WebsiteSearchProgress`.

Prefer (1): mark checked on main thread before spawn; workers only return data; main thread calls `skipped` / `collect_*` / `candidate`.

Refactor `fetch_website_links` accordingly so parallel path does not dual-call progress.

Remove the old serial `for (index…)` loop entirely.

- [ ] **Step 3: Measure and fix smoke count assertion**

Run: `bash /home/fsx/yai/tests/appimage_feed_smoke.sh`

Update the exact `after checking …` line to match new deterministic output. Confirm musescore install/update/upgrade still pass.

- [ ] **Step 4: Commit** (only if user asked)

```bash
git add src/resolver_website.cpp tests/appimage_feed_smoke.sh
git commit -m "$(cat <<'EOF'
Fetch website search pages with bounded parallelism.

EOF
)"
```

---

### Task 4: Docs + full smoke regression

**Files:**
- Modify: `README.md` (website search paragraph ~lines 227–236)
- Modify: `AppImage包管理器开发文档.md` (matching website_page bullet)
- Verify: existing smoke suite

- [ ] **Step 1: Add one sentence to README**

Near the website search description, add that discovery may fetch a few same-host pages in parallel, uses short timeouts (faster fail on dead links), and does not download full AppImage bodies while probing HTML landings.

- [ ] **Step 2: Mirror the note in the Chinese developer doc**

Same factual content in `AppImage包管理器开发文档.md` where `website_page` search is described.

- [ ] **Step 3: Run regression smokes**

Run:

```bash
make -C /home/fsx/yai
bash /home/fsx/yai/tests/fetch_text_timeout_smoke.sh
bash /home/fsx/yai/tests/appimage_feed_smoke.sh
bash /home/fsx/yai/tests/download_smoke.sh
```

Expected: all PASS.

- [ ] **Step 4: Commit** (only if user asked)

```bash
git add README.md "AppImage包管理器开发文档.md"
git commit -m "$(cat <<'EOF'
Document parallel timed website AppImage discovery.

EOF
)"
```

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| No unbounded `.AppImage` body during search; capped landing probe | Task 2 (+ Task 1 helper) |
| Default 15s / speculative 5s timeouts | Task 1–2 |
| Concurrency 4 parallel fetch | Task 3 |
| Priority `appimage` > `download` > `linux` > other; seeds stable | Task 2 |
| Early exit / trust bounds / stage landing unchanged | Tasks 2–3 |
| Smoke updates + no curl noise | Tasks 2–4 |
| No cache / no libcurl | Honored globally |
| README/doc note | Task 4 |

## Placeholder / consistency self-review

- No TBD steps; `fetch_text` / `fetch_text_limited` / queue fields named consistently across tasks
- MuseScore update semantics preserved via capped probe (documented Global Constraint interpretation)
- Exact `after checking N…` string deferred to measurement in Task 3 (explicit), not left as vague “update tests”
