# Task 4: Regression verification report

**Date:** 2026-07-24  
**Working directory:** `/home/fsx/yai`  
**Status:** **BLOCKED** — 2 of 6 smoke tests fail at link time (split-related; smoke scripts not updated)

---

## Step 1: Build

```bash
make clean && make
```

**Exit code:** 0

**Output:**
```
rm -f yai
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -o yai src/arch.cpp src/appimage.cpp src/cli_download.cpp src/commands.cpp src/commands_doctor.cpp src/core.cpp src/download_progress.cpp src/i18n.cpp src/json.cpp src/main.cpp src/process.cpp src/repo.cpp src/resolver.cpp
```

**Binary check:**
```
-rwxr-xr-x. 1 root root 885168  7月24日 22:06 /home/fsx/yai/yai
```

**Result:** PASS — `./yai` produced and executable.

---

## Step 2: Progress + download smokes

### `YAI_LANG=en tests/progress_smoke.sh`

**Exit code:** 1

**Output (link failure):**
```
/usr/bin/ld.bfd: /tmp/ccLqxG69.o: in function `main':
progress_test.cpp:(.text.startup+0x7d4): undefined reference to `download_progress_downloaded_bytes(std::filesystem::__cxx11::path const&)'
/usr/bin/ld.bfd: progress_test.cpp:(.text.startup+0x834): undefined reference to `download_progress_recent_speed(DownloadProgressState&, ...)'
... (5 more `download_progress_recent_speed` refs) ...
/usr/bin/ld.bfd: /tmp/cc1C0Jcy.o: in function `china_network_disclaimer[abi:cxx11]()':
core.cpp:(.text+0x1caa): undefined reference to `tr(std::__cxx11::basic_string<char, ...> const&)'
/usr/bin/ld.bfd: ... more undefined references to `tr(...)' from core.cpp ...
collect2: error: ld 返回 1
```

**Diagnosis:** `tests/progress_smoke.sh` line 106–108 compiles only `progress_test.cpp` + `src/core.cpp`. After the split, `download_progress_*` symbols live in `src/download_progress.cpp` and `tr()` lives in `src/i18n.cpp`. Main `Makefile` build succeeds; this unit-test-style smoke was not updated.

**Result:** FAIL

### `YAI_LANG=en tests/download_smoke.sh`

**Exit code:** 0

**Output (summarized):** Full download/install flow against fake API and fake aria2c/curl; ends with:
```
download smoke test passed
```

**Result:** PASS

---

## Step 3: Baseline stage / repo / arch / json smokes

### `YAI_LANG=en tests/stage1_smoke.sh`

**Exit code:** 0  
**Output (summarized):** install/repair/remove/local/parallel/bare-app flows; ends with `stage1 smoke test passed`.  
**Result:** PASS

### `YAI_LANG=en tests/repo_smoke.sh`

**Exit code:** 0  
**Output (summarized):** repo add + GitHub-release install + remove; ends with `repo smoke test passed`.  
**Result:** PASS

### `YAI_LANG=en tests/arch_smoke.sh`

**Exit code:** 1

**Output (link failure):**
```
/usr/bin/ld.bfd: /tmp/cczKsDGm.o: in function `current_arch[abi:cxx11]()':
arch.cpp:(.text+0x1bbd): undefined reference to `run_process_capture(...)'
/usr/bin/ld.bfd: /tmp/cctxWVKe.o: in function `china_network_disclaimer[abi:cxx11]()':
core.cpp:(.text+0x1caa): undefined reference to `tr(...)'
/usr/bin/ld.bfd: ... more undefined references to `tr(...)' from core.cpp ...
collect2: error: ld 返回 1
```

**Diagnosis:** `tests/arch_smoke.sh` line 61–63 compiles `arch_test.cpp` + `src/arch.cpp` + `src/core.cpp`. After the split, `run_process_capture` is in `src/process.cpp` and `tr()` is in `src/i18n.cpp`.

**Result:** FAIL

### `YAI_LANG=en tests/json_smoke.sh`

**Exit code:** 0  
**Output:** `json smoke test passed`  
**Result:** PASS

---

## Step 4: Line counts

```bash
wc -l src/core.cpp src/i18n.cpp src/process.cpp src/download_progress.cpp
```

**Output:**
```
  461 src/core.cpp
  213 src/i18n.cpp
  447 src/process.cpp
  513 src/download_progress.cpp
 1634 总计
```

| File | Actual | Expected (brief) | Verdict |
|------|--------|------------------|---------|
| `core.cpp` | 461 | ≲ 500 | OK |
| `i18n.cpp` | 213 | ~220–250 | Slightly below range (extra banner/blank lines in new TUs explain delta) |
| `process.cpp` | 447 | ~450–550 | OK (lower end) |
| `download_progress.cpp` | 513 | ~500–600 | OK |
| **Sum** | **1634** | ≈ pre-split `core.cpp` | Pre-split baseline: **1613** lines (`.superpowers/sdd/core.cpp.pre-task1`); +21 lines consistent with per-file banners/includes |

**Result:** PASS (approximate expectations met)

---

## Summary

| Check | Result |
|-------|--------|
| `make clean && make` | PASS |
| `progress_smoke.sh` | **FAIL** (link) |
| `download_smoke.sh` | PASS |
| `stage1_smoke.sh` | PASS |
| `repo_smoke.sh` | PASS |
| `arch_smoke.sh` | **FAIL** (link) |
| `json_smoke.sh` | PASS |
| Line counts | PASS |

**Overall:** **BLOCKED** — main binary and integration smokes (download, stage1, repo, json) pass; two compile-and-link unit smokes fail because they still link only subsets of the old monolithic `core.cpp`.

**Recommended follow-up (out of Task 4 scope):** Update smoke link lines:
- `progress_smoke.sh`: add `src/download_progress.cpp` and `src/i18n.cpp`
- `arch_smoke.sh`: add `src/process.cpp` and `src/i18n.cpp`

**Commits:** None (per brief).

---

## Fix: smoke link lines

**What changed**

- `tests/progress_smoke.sh` (g++ link line ~106–112): added `src/download_progress.cpp`, `src/i18n.cpp`, and `src/process.cpp` alongside existing `src/core.cpp` so `download_progress_*`, `tr()`, and any process symbols resolve after the `core.cpp` split.
- `tests/arch_smoke.sh` (g++ link line ~61–66): added `src/i18n.cpp` and `src/process.cpp` per spec; also added `src/download_progress.cpp` because `process.cpp` references `render_download_progress` / `clear_download_progress` (link failed without it).

**Commands**

```bash
YAI_LANG=en tests/progress_smoke.sh
YAI_LANG=en tests/arch_smoke.sh
```

**Results**

| Script | Exit | Output |
|--------|------|--------|
| `progress_smoke.sh` | 0 | `progress smoke test passed` |
| `arch_smoke.sh` | 0 | `arch smoke test passed` |

Both smokes now link and run cleanly. No test logic changed. No commit.
