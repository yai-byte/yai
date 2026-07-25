### Task 2: Extract `src/process.cpp`

**Files:**
- Create: `src/process.cpp`
- Modify: `src/core.cpp` (remove process block + `run_process_capture_download_progress`)

**Interfaces:**
- Consumes: `tr`, progress APIs (`render_download_progress`, `clear_download_progress`, …) via `yai.hpp`
- Produces: `run_process`, `run_process_capture`, `run_process_capture_separate`, `run_process_capture_timeout`, `run_process_capture_download_progress`
- Internal (anonymous namespace): `process_argv`, fd/wait/kill helpers, `exec_child_process`, `start_captured_process`, `set_nonblocking`, `kill_and_reap`, `ReadOutputResult`, read/append output helpers, `terminate_for_timeout`

- [ ] **Step 1: Re-locate markers in the post–Task-1 `core.cpp`**

```bash
rg -n '^(std::vector<char\*> process_argv|ProcessResult run_process_capture_timeout|std::string format_byte_count|ProcessResult run_process_capture_download_progress|void ensure_directory)' src/core.cpp
```

Record:
- `P0` = line of `process_argv`
- `P1` = last line of `run_process_capture_timeout` (closing `}` of that function)
- `D0` = line of `format_byte_count` (start of progress section; must remain in core until Task 3)
- `R0` = line of `run_process_capture_download_progress`
- `R1` = last line of that function
- `E0` = line of `ensure_directory`

- [ ] **Step 2: Create `src/process.cpp`**

Extract `[P0, P1]` and `[R0, R1]` into one file. Wrap every extracted symbol that is **not** declared in `yai.hpp` inside `namespace { ... }`. Public functions stay at file scope:

Public (outside anon):
- `run_process`
- `run_process_capture`
- `run_process_capture_separate`
- `run_process_capture_timeout`
- `run_process_capture_download_progress`

Everything else from the process slice goes in the anonymous namespace (including `enum class ReadOutputResult` and helpers used only by the public runners).

File skeleton:

```cpp
#include "yai.hpp"

// Child-process execution and captured downloads (including progress-aware capture).

namespace {
// ... former file-scope process helpers ...
} // namespace

int run_process(...) { ... }
ProcessResult run_process_capture(...) { ... }
ProcessOutput run_process_capture_separate(...) { ... }
ProcessResult run_process_capture_timeout(...) { ... }
ProcessResult run_process_capture_download_progress(...) { ... }
```

Implementation tip: after copying the contiguous `P0–P1` block, open `namespace {` before `process_argv` and close it immediately before `int run_process(`. Keep `run_process_capture_download_progress` after the timeout function (still file scope).

- [ ] **Step 3: Remove `[P0, P1]` and `[R0, R1]` from `src/core.cpp`**

Leave the progress block (`format_byte_count` … `clear_download_progress`) in place for Task 3. After removal, `output_has_fuse_error` should be followed by `format_byte_count` (temporarily), then later `ensure_directory`.

- [ ] **Step 4: Add `src/process.cpp` to `Makefile` and build**

```bash
# edit Makefile SRC to include src/process.cpp
make clean && make
```

Expected: success. Failures about missing helpers mean a helper was left outside anon in the wrong file or still referenced from `core.cpp`.

---

