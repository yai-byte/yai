# Batch Progress UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make multi-target `install`/`download` batches stream live prefixed logs and show per-task download progress in a parent-owned sticky footer instead of silent capture-and-replay.

**Architecture:** Keep re-exec batch children. Each child gets stdout/stderr pipes plus a private event pipe (`YAI_BATCH_EVENT_FD`). Children emit `PROGRESS` / `PROGRESS_CLEAR` instead of painting `\r` progress when that fd is set. The parent multiplexes with `poll`, prefixes log lines, and (on TTY stderr) maintains a sticky footer keyed by task index.

**Tech Stack:** C++17, POSIX pipes/`poll`/`fork`, existing `download_progress.cpp` helpers, Makefile, bash smoke tests, gettext `po/en.po` + `po/zh.po` only if new user-facing strings appear.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-26-batch-progress-ui-design.md` exactly
- Single-package `install`/`download` UX must remain unchanged (direct stderr `\r` progress when TTY)
- Wildcard-expanded batches stay sequential + stop-on-first-failure; explicit multi-argv stays parallel + aggregate failures
- Non-TTY: stream prefixed logs; never draw sticky footer / `\r`
- Batch child stdout content is forwarded to the prefixed stderr scroll stream (no post-task stdout replay onto parent stdout)
- Downloaders remain `--silent`/`--quiet`; yai owns progress
- msgid = English source text; sync `po/en.po` and `po/zh.po` if new `tr()` strings are added
- Commit after each task unless the user forbids commits

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | `BatchProgressEvent`, parse/format decls, `batch_event_fd`, `BatchTerminalUi`, `run_batch_task_streaming` |
| `src/batch_progress_event.cpp` | Parse/format `PROGRESS` / `PROGRESS_CLEAR`; read `YAI_BATCH_EVENT_FD` |
| `src/batch_ui.cpp` | Thread-safe sticky-footer UI (`BatchTerminalUi`) |
| `src/download_progress.cpp` | Emit events when event fd present; else keep TTY `\r` path |
| `src/process.cpp` | `run_batch_task_streaming` (stdout/stderr/event pipes + poll) |
| `src/main.cpp` | `run_batch_command` uses streaming + shared UI |
| `Makefile` | Add `src/batch_progress_event.cpp` and `src/batch_ui.cpp` to `SRC` |
| `tests/progress_smoke.sh` | Unit checks for event parse/format |
| `tests/batch_progress_smoke.sh` | Streaming/prefix/non-TTY assertions (create) |
| `tests/download_smoke.sh`, `tests/stage1_smoke.sh`, `tests/wildcard_multi_smoke.sh` | Adapt to prefixed live output |
| `README.md` | Note live batch progress |

---

### Task 1: Progress event protocol helpers

**Files:**
- Create: `src/batch_progress_event.cpp`
- Modify: `src/yai.hpp` (add decls near download progress section)
- Modify: `Makefile` (add `src/batch_progress_event.cpp` to `SRC`)
- Modify: `tests/progress_smoke.sh`

**Interfaces:**
- Consumes: none
- Produces:
  - `struct BatchProgressEvent { enum class Kind { Progress, Clear } kind; std::uintmax_t done = 0; std::optional<std::uintmax_t> total; double rate_bps = 0.0; };`
  - `std::optional<BatchProgressEvent> parse_batch_progress_event(const std::string& line);`
  - `std::string format_batch_progress_event(std::uintmax_t done, std::optional<std::uintmax_t> total, double rate_bps);`
  - `std::string format_batch_progress_clear_event();`
  - `int batch_event_fd();` // `-1` if unset/invalid

- [ ] **Step 1: Extend `tests/progress_smoke.sh` with failing protocol checks**

Inside the embedded `main()`, after existing speed asserts and before the success print, add:

```cpp
    {
        const std::string line = format_batch_progress_event(12345, 67890, 1024.0);
        require(line == "PROGRESS done=12345 total=67890 rate=1024", "format progress");
        const auto ev = parse_batch_progress_event(line);
        require(ev.has_value(), "parse progress");
        require(ev->kind == BatchProgressEvent::Kind::Progress, "kind");
        require(ev->done == 12345, "done");
        require(ev->total.has_value() && *ev->total == 67890, "total");
        require(std::fabs(ev->rate_bps - 1024.0) < 0.001, "rate");

        const std::string unknown = format_batch_progress_event(10, std::nullopt, 0.0);
        require(unknown == "PROGRESS done=10 total=- rate=0", "format unknown total");
        const auto ev2 = parse_batch_progress_event(unknown);
        require(ev2.has_value() && !ev2->total.has_value(), "parse unknown total");

        require(format_batch_progress_clear_event() == "PROGRESS_CLEAR", "format clear");
        const auto clear = parse_batch_progress_event("PROGRESS_CLEAR");
        require(clear.has_value() && clear->kind == BatchProgressEvent::Kind::Clear, "parse clear");

        require(!parse_batch_progress_event("NOPE").has_value(), "reject junk");
        require(!parse_batch_progress_event("PROGRESS done=x total=1 rate=1").has_value(), "reject bad ints");
        require(batch_event_fd() < 0, "unset event fd");
    }
```

Also add `"$ROOT/src/batch_progress_event.cpp"` to the `g++` link line in that smoke.

- [ ] **Step 2: Run test to verify it fails**

Run: `bash tests/progress_smoke.sh`  
Expected: compile failure (`format_batch_progress_event` / `BatchProgressEvent` undeclared) or link failure.

- [ ] **Step 3: Declare APIs in `src/yai.hpp`**

Near the download-progress declarations, add:

```cpp
struct BatchProgressEvent {
    enum class Kind { Progress, Clear } kind = Kind::Progress;
    std::uintmax_t done = 0;
    std::optional<std::uintmax_t> total;
    double rate_bps = 0.0;
};

std::optional<BatchProgressEvent> parse_batch_progress_event(const std::string& line);
std::string format_batch_progress_event(
    std::uintmax_t done,
    std::optional<std::uintmax_t> total,
    double rate_bps);
std::string format_batch_progress_clear_event();
int batch_event_fd();
```

- [ ] **Step 4: Implement `src/batch_progress_event.cpp`**

```cpp
#include "yai.hpp"

#include <cstdlib>
#include <sstream>

namespace {

bool parse_u64_field(const std::string& field, const char* key, std::uintmax_t& out) {
    const std::string prefix = std::string(key) + "=";
    if (field.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string value = field.substr(prefix.size());
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<std::uintmax_t>(parsed);
    return true;
}

bool parse_optional_u64_or_dash(
    const std::string& field,
    const char* key,
    std::optional<std::uintmax_t>& out) {
    const std::string prefix = std::string(key) + "=";
    if (field.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string value = field.substr(prefix.size());
    if (value == "-") {
        out = std::nullopt;
        return true;
    }
    std::uintmax_t parsed = 0;
    if (!parse_u64_field(field, key, parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool parse_rate_field(const std::string& field, double& out) {
    if (field.rfind("rate=", 0) != 0) {
        return false;
    }
    const std::string value = field.substr(5);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || parsed < 0.0) {
        return false;
    }
    out = parsed;
    return true;
}

} // namespace

std::string format_batch_progress_event(
    std::uintmax_t done,
    std::optional<std::uintmax_t> total,
    double rate_bps) {
    std::ostringstream out;
    out << "PROGRESS done=" << done << " total=";
    if (total.has_value()) {
        out << *total;
    } else {
        out << '-';
    }
    out << " rate=" << static_cast<std::uintmax_t>(rate_bps);
    return out.str();
}

std::string format_batch_progress_clear_event() {
    return "PROGRESS_CLEAR";
}

std::optional<BatchProgressEvent> parse_batch_progress_event(const std::string& line) {
    const std::string trimmed = trim(line);
    if (trimmed == "PROGRESS_CLEAR") {
        BatchProgressEvent ev;
        ev.kind = BatchProgressEvent::Kind::Clear;
        return ev;
    }
    if (trimmed.rfind("PROGRESS ", 0) != 0) {
        return std::nullopt;
    }

    std::istringstream in(trimmed.substr(9));
    std::string done_field;
    std::string total_field;
    std::string rate_field;
    if (!(in >> done_field >> total_field >> rate_field)) {
        return std::nullopt;
    }
    std::string extra;
    if (in >> extra) {
        return std::nullopt;
    }

    BatchProgressEvent ev;
    ev.kind = BatchProgressEvent::Kind::Progress;
    if (!parse_u64_field(done_field, "done", ev.done) ||
        !parse_optional_u64_or_dash(total_field, "total", ev.total) ||
        !parse_rate_field(rate_field, ev.rate_bps)) {
        return std::nullopt;
    }
    return ev;
}

int batch_event_fd() {
    const std::optional<std::string> raw = env_string("YAI_BATCH_EVENT_FD");
    if (!raw.has_value() || raw->empty()) {
        return -1;
    }
    char* end = nullptr;
    const long fd = std::strtol(raw->c_str(), &end, 10);
    if (end == raw->c_str() || *end != '\0' || fd < 0 || fd > 2147483647L) {
        return -1;
    }
    return static_cast<int>(fd);
}
```

Add `src/batch_progress_event.cpp` to `Makefile` `SRC` after `src/arch.cpp`.

- [ ] **Step 5: Run test to verify it passes**

Run: `bash tests/progress_smoke.sh`  
Expected: `progress smoke test passed`

- [ ] **Step 6: Commit**

```bash
git add src/yai.hpp src/batch_progress_event.cpp Makefile tests/progress_smoke.sh
git commit -m "$(cat <<'EOF'
Add batch progress event parse/format helpers.

EOF
)"
```

---

### Task 2: Emit progress events from download renderer

**Files:**
- Modify: `src/download_progress.cpp` (`render_download_progress`, `clear_download_progress`)
- Modify: `tests/progress_smoke.sh` (small emit harness)

**Interfaces:**
- Consumes: `batch_event_fd`, `format_batch_progress_event`, `format_batch_progress_clear_event`, existing `download_progress_snapshot`
- Produces: when `batch_event_fd() >= 0`, write one event line + `\n` to that fd; do not paint `\r` on stderr

- [ ] **Step 1: Add failing emit harness to `tests/progress_smoke.sh`**

After the protocol block, add a pipe-based check that sets `YAI_BATCH_EVENT_FD`, calls `render_download_progress` / `clear_download_progress`, and reads back lines. Sketch:

```cpp
    {
        int fds[2];
        require(pipe(fds) == 0, "pipe");
        require(setenv("YAI_BATCH_EVENT_FD", std::to_string(fds[1]).c_str(), 1) == 0, "setenv");
        require(batch_event_fd() == fds[1], "event fd visible");

        const fs::path headers = fs::path(argv[1]) / "headers.txt";
        {
            std::ofstream out(headers);
            out << "Content-Length: 1048576\r\n";
        }
        std::size_t last_width = 0;
        DownloadProgressState prog_state;
        const auto start = std::chrono::steady_clock::now();
        render_download_progress(part, headers, start, 0, last_width, prog_state);
        clear_download_progress(last_width);

        require(close(fds[1]) == 0, "close write");
        unsetenv("YAI_BATCH_EVENT_FD");

        char buf[4096];
        const ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
        require(n > 0, "read events");
        buf[n] = '\0';
        close(fds[0]);
        const std::string text(buf);
        require(text.find("PROGRESS done=") != std::string::npos, "progress event missing");
        require(text.find("PROGRESS_CLEAR") != std::string::npos, "clear event missing");
        require(text.find('\r') == std::string::npos, "no carriage return on event path");
    }
```

Include `<unistd.h>` / `<cstdlib>` as needed in the harness.

- [ ] **Step 2: Run test to verify it fails**

Run: `bash tests/progress_smoke.sh`  
Expected: FAIL on missing `PROGRESS` event (current code returns early when stderr is non-TTY / no event emit).

- [ ] **Step 3: Update `render_download_progress` / `clear_download_progress`**

Replace the top of `render_download_progress` so event-fd mode wins before the TTY check:

```cpp
void render_download_progress(...) {
    const int event_fd = batch_event_fd();
    const std::optional<DownloadProgressSnapshot> snapshot =
        download_progress_snapshot(part, headers, start, state);
    if (!snapshot.has_value()) {
        return;
    }

    if (event_fd >= 0) {
        const std::string line = format_batch_progress_event(
            snapshot->downloaded,
            snapshot->total,
            snapshot->bytes_per_second) + "\n";
        // best-effort write; ignore EAGAIN/partial for UI smoothness
        const char* data = line.data();
        std::size_t left = line.size();
        while (left > 0) {
            const ssize_t written = write(event_fd, data, left);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            data += written;
            left -= static_cast<std::size_t>(written);
        }
        return;
    }

    if (isatty(STDERR_FILENO) == 0) {
        return;
    }
    // existing write_progress_line path unchanged
    ...
}

void clear_download_progress(std::size_t& last_width) {
    const int event_fd = batch_event_fd();
    if (event_fd >= 0) {
        const std::string line = format_batch_progress_clear_event() + "\n";
        // same best-effort write loop as above
        last_width = 0;
        return;
    }
    // existing TTY clear path unchanged
    ...
}
```

Ensure `#include`s for `unistd.h` / `errno.h` exist (process/progress files already use POSIX headers elsewhere—add locally if missing).

Note: `download_progress_snapshot` is currently in an anonymous namespace. Either call the existing public byte helpers to build the event fields inside `render_download_progress` after the snapshot is obtained (keep using the internal snapshot helper from within the same file), or lift only what this file already uses—do **not** move snapshot logic out of `download_progress.cpp`.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash tests/progress_smoke.sh`  
Expected: `progress smoke test passed`

- [ ] **Step 5: Commit**

```bash
git add src/download_progress.cpp tests/progress_smoke.sh
git commit -m "$(cat <<'EOF'
Emit batch progress events when YAI_BATCH_EVENT_FD is set.

EOF
)"
```

---

### Task 3: `BatchTerminalUi` sticky footer

**Files:**
- Create: `src/batch_ui.cpp`
- Modify: `src/yai.hpp`
- Modify: `Makefile`
- Create: harness section in `tests/batch_progress_smoke.sh` (UI-only compile test first; full CLI checks land in Task 6)

**Interfaces:**
- Consumes: `parse_batch_progress_event` types; `progress_bar` / `format_byte_count` / `terminal_width` / `display_width` / `truncate_display_width`
- Produces:

```cpp
class BatchTerminalUi {
public:
    explicit BatchTerminalUi(std::size_t total_tasks);

    void log_parent(const std::string& message);
    void log_line(std::size_t index, const std::string& target, const std::string& message);
    void apply_event(std::size_t index, const std::string& target, const BatchProgressEvent& event);
    void clear_task(std::size_t index);
    void shutdown();

private:
    // mutex, tty flag, map/vector of active progress rows, footer_lines count
};
```

Prefix for task lines: `"[" + std::to_string(index + 1) + "/" + std::to_string(total_) + " " + target + "] "`  
(`index` is 0-based internally; display is 1-based.)

- [ ] **Step 1: Write failing UI harness in `tests/batch_progress_smoke.sh`**

Create the file with a compiled harness that:
1. Constructs `BatchTerminalUi(2)` with stderr redirected to a file (non-TTY path).
2. Calls `log_line(0, "foo", "Downloading x")` and `log_line(1, "bar", "Downloading y")`.
3. Asserts the file contains `[1/2 foo] Downloading x` and `[2/2 bar] Downloading y`.
4. Calls `apply_event` with a Progress event under non-TTY and asserts **no** `\r` appeared.

Minimal script skeleton:

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
export YAI_LANG=en

cat > "$TMP_DIR/batch_ui_test.cpp" <<'CPP'
#include "yai.hpp"
#include <fstream>
#include <stdexcept>
static void require(bool c, const char* m) { if (!c) throw std::runtime_error(m); }
int main() {
    BatchTerminalUi ui(2);
    ui.log_line(0, "foo", "Downloading x");
    ui.log_line(1, "bar", "Downloading y");
    BatchProgressEvent ev;
    ev.kind = BatchProgressEvent::Kind::Progress;
    ev.done = 50; ev.total = 100; ev.rate_bps = 10;
    ui.apply_event(0, "foo", ev);
    ui.shutdown();
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$ROOT/src" \
  -o "$TMP_DIR/batch_ui_test" \
  "$TMP_DIR/batch_ui_test.cpp" \
  "$ROOT/src/batch_ui.cpp" \
  "$ROOT/src/batch_progress_event.cpp" \
  "$ROOT/src/download_progress.cpp" \
  "$ROOT/src/core.cpp" \
  "$ROOT/src/arch.cpp" \
  "$ROOT/src/i18n.cpp" \
  "$ROOT/src/process.cpp"

"$TMP_DIR/batch_ui_test" >"$TMP_DIR/out" 2>"$TMP_DIR/err"
grep -q '\[1/2 foo\] Downloading x' "$TMP_DIR/err"
grep -q '\[2/2 bar\] Downloading y' "$TMP_DIR/err"
if grep -q $'\r' "$TMP_DIR/err"; then
  echo "unexpected CR in non-TTY UI output" >&2
  exit 1
fi
echo "batch ui unit section passed"
```

- [ ] **Step 2: Run harness to verify it fails**

Run: `bash tests/batch_progress_smoke.sh`  
Expected: compile failure (`BatchTerminalUi` missing).

- [ ] **Step 3: Implement `BatchTerminalUi` in `src/batch_ui.cpp`**

Behavior requirements (must match spec):

1. `tty_ = isatty(STDERR_FILENO)`.
2. All public methods take a `std::mutex` lock.
3. `log_parent` / `log_line`:
   - If TTY and footer height > 0: move cursor up / clear footer lines (print `\r` + spaces + `\r` per line, or ANSI `CUU`/`EL` if already used elsewhere—prefer plain `\r` clear loops consistent with `clear_download_progress`).
   - Write message + `\n` to `std::cerr`.
   - Redraw footer.
4. Footer row format (reuse byte helpers; keep one line per active task):

```text
[1/2 foo] <bar>  50%  50B/100B  10B/s
```

Use `progress_bar(total, done, width, tick)` with `tick=0` for stable tests; width from `terminal_width()` minus prefix length (clamp bar width like `progress_bar_width` logic—duplicate a small local helper or call into existing free functions if already public; if `progress_bar_width` is anonymous, inline a 12–30 clamp).

5. Cap visible footer rows at `min(active_count, 8)`.
6. `apply_event`: Progress upserts row for `index`; Clear erases it; then redraw if TTY.
7. `clear_task`: erase progress row for index (also call from streaming runner on task end).
8. `shutdown`: clear footer once.

Declare the class in `yai.hpp`; add `src/batch_ui.cpp` to `Makefile`.

- [ ] **Step 4: Run harness to verify it passes**

Run: `bash tests/batch_progress_smoke.sh`  
Expected: `batch ui unit section passed`

- [ ] **Step 5: Commit**

```bash
git add src/yai.hpp src/batch_ui.cpp Makefile tests/batch_progress_smoke.sh
git commit -m "$(cat <<'EOF'
Add BatchTerminalUi for prefixed logs and sticky progress.

EOF
)"
```

---

### Task 4: Stream batch child stdout/stderr/events

**Files:**
- Modify: `src/process.cpp`
- Modify: `src/yai.hpp`
- Modify: `tests/batch_progress_smoke.sh` (add streaming section using a tiny helper child)

**Interfaces:**
- Consumes: `BatchTerminalUi`, `parse_batch_progress_event`
- Produces:

```cpp
struct StreamingBatchResult {
    int exit_code = 1;
};

StreamingBatchResult run_batch_task_streaming(
    const std::vector<std::string>& args,
    const std::optional<fs::path>& cwd,
    const std::vector<std::pair<std::string, std::string>>& base_env,
    std::size_t index,
    std::size_t total,
    const std::string& target,
    BatchTerminalUi& ui);
```

Semantics:
- Create three pipes: stdout, stderr, event.
- `fork` + `setpgid` like `run_process_capture_separate`.
- Child: `dup2` stdout/stderr pipes; close read ends; leave event write fd open; `setenv("YAI_BATCH_CHILD","1")` and `setenv("YAI_BATCH_EVENT_FD", "<write_fd>")` after merging `base_env`; `exec`.
- Parent: close write ends; `poll` the three read fds; accumulate partial lines in per-fd buffers; on `\n`, dispatch:
  - stdout/stderr → `ui.log_line(index, target, line)` (strip trailing `\r`)
  - event → `parse_batch_progress_event`; if ok → `ui.apply_event(...)`
- After child exits, drain remaining fds, `ui.clear_task(index)`, return exit code.
- On parent exceptions: SIGKILL process group, `ui.clear_task(index)`, rethrow.

- [ ] **Step 1: Add failing streaming section to `tests/batch_progress_smoke.sh`**

Append a section that compiles a tiny `fake_child` printing two lines and writing one `PROGRESS` + `PROGRESS_CLEAR` to `YAI_BATCH_EVENT_FD`, then a parent harness calling `run_batch_task_streaming`. Assert prefixed logs appear and exit code is 0.

```cpp
// fake_child.cpp main:
//   std::cout << "hello-out\n";
//   std::cerr << "hello-err\n";
//   const int fd = batch_event_fd();
//   write PROGRESS + PROGRESS_CLEAR lines;
//   return 0;
```

Parent harness uses `BatchTerminalUi` + `run_batch_task_streaming({fake_child}, ...)`.

- [ ] **Step 2: Run to verify fail**

Run: `bash tests/batch_progress_smoke.sh`  
Expected: link/compile failure on missing `run_batch_task_streaming`.

- [ ] **Step 3: Implement `run_batch_task_streaming` in `src/process.cpp`**

Reuse existing helpers in that file (`process_argv`, `close_fd_*`, `waitpid_*`, `setpgid`, `signal_process_best_effort`). Keep line buffers modest (e.g. flush at 64 KiB without newline as one log line to avoid stalls).

Declare in `yai.hpp`.

- [ ] **Step 4: Run to verify pass**

Run: `bash tests/batch_progress_smoke.sh`  
Expected: UI section + streaming section pass.

- [ ] **Step 5: Commit**

```bash
git add src/yai.hpp src/process.cpp tests/batch_progress_smoke.sh
git commit -m "$(cat <<'EOF'
Stream batch child logs and progress events to BatchTerminalUi.

EOF
)"
```

---

### Task 5: Wire `run_batch_command` to streaming UI

**Files:**
- Modify: `src/main.cpp` (`run_batch_command`, `BatchResult`)
- Modify: `README.md` (short note under install/download batch docs)

**Interfaces:**
- Consumes: `BatchTerminalUi`, `run_batch_task_streaming`, `batch_child_args`
- Produces: live batch UX; unchanged exit/aggregation semantics

- [ ] **Step 1: Replace sequential + parallel bodies in `run_batch_command`**

Sketch:

```cpp
void run_batch_command(const BatchCommand& batch) {
    BatchTerminalUi ui(batch.targets.size());
    ui.log_parent(tr("yai: running ") + ... + tr(" job(s)\n")); // keep existing wording

    if (batch.expanded_multi) {
        for (std::size_t i = 0; i < batch.targets.size(); ++i) {
            const std::string& target = batch.targets[i];
            StreamingBatchResult result;
            try {
                result = run_batch_task_streaming(
                    batch_child_args(batch, target),
                    std::nullopt,
                    {{"YAI_BATCH_CHILD", "1"}},
                    i,
                    batch.targets.size(),
                    target,
                    ui);
            } catch (const std::exception& ex) {
                ui.log_line(i, target, ex.what());
                result.exit_code = 1;
            }
            if (result.exit_code != 0) {
                ui.log_parent(tr_format(
                    "yai: task failed: {command} {target} (exit {code})\n",
                    {{"{command}", batch.command},
                     {"{target}", target},
                     {"{code}", std::to_string(result.exit_code)}}));
                ui.shutdown();
                throw std::runtime_error(tr("batch stopped after task failure"));
            }
        }
        ui.shutdown();
        return;
    }

    struct ParallelSlot {
        std::string target;
        int exit_code = 1;
    };
    std::vector<ParallelSlot> results(batch.targets.size());
    for (std::size_t i = 0; i < batch.targets.size(); ++i) {
        results[i].target = batch.targets[i];
    }

    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    for (std::size_t w = 0; w < batch.jobs; ++w) {
        workers.emplace_back([&]() {
            while (true) {
                const std::size_t index = next.fetch_add(1);
                if (index >= batch.targets.size()) {
                    return;
                }
                try {
                    const StreamingBatchResult result = run_batch_task_streaming(
                        batch_child_args(batch, results[index].target),
                        std::nullopt,
                        {{"YAI_BATCH_CHILD", "1"}},
                        index,
                        batch.targets.size(),
                        results[index].target,
                        ui);
                    results[index].exit_code = result.exit_code;
                } catch (const std::exception& ex) {
                    ui.log_line(index, results[index].target, ex.what());
                    results[index].exit_code = 1;
                }
                if (results[index].exit_code != 0) {
                    ui.log_parent(tr_format(
                        "yai: task failed: {command} {target} (exit {code})\n",
                        {{"{command}", batch.command},
                         {"{target}", results[index].target},
                         {"{code}", std::to_string(results[index].exit_code)}}));
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    std::size_t failed = 0;
    for (const auto& result : results) {
        if (result.exit_code != 0) {
            ++failed;
        }
    }
    ui.shutdown();
    if (failed > 0) {
        throw std::runtime_error(tr_format(
            "{failed} of {total} batch task(s) failed",
            {{"{failed}", std::to_string(failed)},
             {"{total}", std::to_string(results.size())}}));
    }
}
```

Remove obsolete `BatchResult` / `ProcessOutput` replay code from this function. Keep `batch_child_args` as-is (`YAI_BATCH_EVENT_FD` is injected by `run_batch_task_streaming`, not argv).

Important: `run_batch_task_streaming` must merge `base_env` with the event-fd env var and still set `YAI_BATCH_CHILD` (either in `base_env` from main or inside streaming—do not drop it).

- [ ] **Step 2: Build**

Run: `make -C /home/fsx/yai yai`  
Expected: success.

- [ ] **Step 3: Update README batch paragraph**

Near the existing `--jobs` / wildcard sequential note (~lines 68–72), add one sentence:

```markdown
During batch `install`/`download`, yai streams each task's logs with a
`[i/n target]` prefix and shows per-task download progress in a sticky
footer when stderr is a TTY.
```

- [ ] **Step 4: Manual smoke**

Run:

```bash
make -C /home/fsx/yai yai
# local dual AppImage if available from stage1 fixtures pattern, or:
bash tests/stage1_smoke.sh
```

Expected: stage1 still installs parallel local AppImages (may fail until Task 6 adapts greps—if only file-existence asserts, it should pass).

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp README.md
git commit -m "$(cat <<'EOF'
Wire batch install/download to streaming progress UI.

EOF
)"
```

---

### Task 6: Adapt smokes + finish batch progress coverage

**Files:**
- Modify: `tests/download_smoke.sh`
- Modify: `tests/stage1_smoke.sh` (only if assertions break)
- Modify: `tests/wildcard_multi_smoke.sh` (only if assertions break)
- Modify: `tests/batch_progress_smoke.sh` (add real `yai` CLI checks)

**Interfaces:**
- Consumes: Task 5 CLI behavior
- Produces: green smokes proving live prefixes + non-TTY no `\r` + failure semantics

- [ ] **Step 1: Adapt failure grep in `tests/download_smoke.sh`**

Keep:

```bash
grep -q "task failed: download acme/missing" "$TMP_HOME/parallel-failure.err"
grep -q "batch task(s) failed" "$TMP_HOME/parallel-failure.err"
```

If success-path greps look for unprefixed `Downloaded to:`, change to accept optional prefix, e.g.:

```bash
grep -Eq '\[.*/parallel-one\] Downloaded to:|Downloaded to:.*parallel-one' ...
```

Prefer asserting artifacts on disk (already present) over brittle log shapes. Only loosen greps that fail under prefixes.

- [ ] **Step 2: Extend `tests/batch_progress_smoke.sh` with CLI checks**

After unit sections, `make -C "$ROOT" yai`, then:

1. **Parallel download with stderr file (non-TTY):** reuse lightweight GitHub API file fixtures like `download_smoke.sh` (two tiny assets), run:

```bash
HOME="$TMP_HOME" YAI_GITHUB_API_BASE="file://$API_ROOT" \
  "$ROOT/yai" download acme/parallel-one acme/parallel-two --jobs 2 \
  >"$TMP_DIR/cli.out" 2>"$TMP_DIR/cli.err"
grep -E '\[1/2 .+\] ' "$TMP_DIR/cli.err"
grep -E '\[2/2 .+\] ' "$TMP_DIR/cli.err"
if grep -q $'\r' "$TMP_DIR/cli.err"; then
  echo "CR in non-TTY batch stderr" >&2
  exit 1
fi
test -f .../parallel-one.AppImage
test -f .../parallel-two.AppImage
```

2. **Wildcard sequential failure:** reuse / call patterns from `wildcard_multi_smoke.sh` enough to assert stop-on-first still holds (or invoke the existing script unchanged if it still passes).

- [ ] **Step 3: Run smokes**

```bash
bash tests/progress_smoke.sh
bash tests/batch_progress_smoke.sh
bash tests/download_smoke.sh
bash tests/stage1_smoke.sh
bash tests/wildcard_multi_smoke.sh
```

Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add tests/batch_progress_smoke.sh tests/download_smoke.sh tests/stage1_smoke.sh tests/wildcard_multi_smoke.sh
git commit -m "$(cat <<'EOF'
Cover streaming batch progress in smoke tests.

EOF
)"
```

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| Interleaved logs + sticky footer | 3, 5 |
| Progress in footer; child text in scroll log | 2, 3, 4 |
| All install/download batch paths | 5 |
| Full child stdout/stderr streamed | 4, 5 |
| Non-TTY: logs yes, footer no | 3, 6 |
| Scheduling semantics unchanged | 5, 6 |
| Event protocol without target field; per-fd index | 1, 4 |
| Single-package unchanged | 2 (event fd unset) |
| Cap footer at 8 | 3 |
| Ctrl-C clears footer | 3 (`shutdown`/`clear_task`); 4 (kill path) |
| README note | 5 |
| Smoke adaptations | 6 |

## Plan self-review notes

- No TBD placeholders in task steps.
- Types are consistent: `BatchProgressEvent`, `BatchTerminalUi`, `StreamingBatchResult`, `run_batch_task_streaming`.
- `rate` is formatted as integer bytes/sec in the protocol (`static_cast<std::uintmax_t>(rate_bps)`) to keep parsing simple; parent UI may still show formatted byte counts.
- `run_process_capture_separate` remains for any non-batch callers; batch no longer uses it.
