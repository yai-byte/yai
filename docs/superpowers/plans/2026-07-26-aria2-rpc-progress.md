# aria2 RPC Download Progress Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Report aria2 “Downloaded” from JSON-RPC `completedLength` so multi-connection sparse `.part` files no longer inflate progress.

**Architecture:** Each `aria2c` invocation gets a loopback-only RPC port. Progress ticks POST `aria2.tellActive` via existing `curl` helpers and parse `completedLength` / `totalLength` / `downloadSpeed`. Curl/wget keep file-size progress. The `.aria2` bitfield parser is removed from the progress path.

**Tech Stack:** C++17, POSIX sockets for ephemeral port allocation, `curl` JSON-RPC over `127.0.0.1`, existing `json_find_string`, Makefile, bash smoke tests.

## Global Constraints

- Follow `docs/superpowers/specs/2026-07-26-aria2-rpc-progress-design.md` exactly
- Never use apparent `.part` size as an aria2 progress source
- RPC listens only on loopback; no `--rpc-secret`
- Port bind conflict: rebuild aria2 argv with a new port and retry up to 3 times, then fail clearly
- msgid = English; sync `po/en.po` + `po/zh.po` only if new `tr()` strings appear
- Commit after each task unless the user forbids commits

## File map

| File | Responsibility |
|------|----------------|
| `src/yai.hpp` | `Aria2RpcProgress`, parse/query decls, port helper, `DownloadToolCommand`, progress APIs take optional RPC port |
| `src/download_progress.cpp` | Parse tellActive JSON; query RPC; wire snapshot to RPC; delete `.aria2` bitfield progress probe |
| `src/cli_download.cpp` | Allocate port; aria2 RPC flags; retry port on busy; pass port into progress runner |
| `src/process.cpp` | `run_process_capture_download_progress` accepts optional RPC port and forwards it to render |
| `tests/progress_smoke.sh` | Replace control-file probes with JSON parse + hold-last semantics |
| `tests/download_smoke.sh` | Assert fake aria2c sees `--enable-rpc` and `--rpc-listen-port=` |
| `README.md` | Note aria2 progress uses RPC `completedLength` |

---

### Task 1: Parse `aria2.tellActive` JSON into progress fields

**Files:**
- Modify: `src/yai.hpp` (add `Aria2RpcProgress` + parse decl near download progress)
- Modify: `src/download_progress.cpp` (implement parser; keep existing render for now)
- Modify: `tests/progress_smoke.sh`

**Interfaces:**
- Consumes: `json_find_string`
- Produces:
  - `struct Aria2RpcProgress { std::uintmax_t completed = 0; std::optional<std::uintmax_t> total; std::optional<double> speed_bps; };`
  - `std::optional<Aria2RpcProgress> parse_aria2_tell_active_response(const std::string& json);`

- [ ] **Step 1: Add failing parse checks to `tests/progress_smoke.sh`**

Inside the embedded `main()`, **replace** the existing `.aria2` control-file / torn-file / pre-control blocks (the `write_aria2_control` helper and all asserts that call `download_progress_downloaded_bytes` for aria2 control behavior) with:

```cpp
    {
        const std::string body =
            "{\"id\":\"yai\",\"jsonrpc\":\"2.0\",\"result\":[{"
            "\"gid\":\"abc\","
            "\"completedLength\":\"1048576\","
            "\"totalLength\":\"10485760\","
            "\"downloadSpeed\":\"204800\""
            "}]}";
        const auto parsed = parse_aria2_tell_active_response(body);
        require(parsed.has_value(), "parse tellActive");
        require(parsed->completed == 1048576, "completedLength");
        require(parsed->total.has_value() && *parsed->total == 10485760, "totalLength");
        require(parsed->speed_bps.has_value() && std::fabs(*parsed->speed_bps - 204800.0) < 0.001, "downloadSpeed");

        require(!parse_aria2_tell_active_response(
                    "{\"id\":\"yai\",\"jsonrpc\":\"2.0\",\"result\":[]}").has_value(),
                "empty tellActive");
        require(!parse_aria2_tell_active_response("not-json").has_value(), "reject junk");
    }
```

Keep the existing `download_progress_recent_speed` and batch event asserts after this block. Remove `write_u16` / `write_u32` / `write_u64` / `write_aria2_control` if nothing else uses them.

Also remove the sparse `.part` setup that only existed for aria2 control tests (or keep a tiny curl-style file-size check if still useful—optional; do not keep control-file tests).

Ensure the smoke still links `src/download_progress.cpp` and `src/json.cpp` (add `"$ROOT/src/json.cpp"` to the `g++` line if missing).

- [ ] **Step 2: Run test to verify it fails**

Run: `bash tests/progress_smoke.sh`  
Expected: compile failure (`parse_aria2_tell_active_response` undeclared) or link failure.

- [ ] **Step 3: Declare API in `src/yai.hpp`**

Near the download-progress declarations, add:

```cpp
struct Aria2RpcProgress {
    std::uintmax_t completed = 0;
    std::optional<std::uintmax_t> total;
    std::optional<double> speed_bps;
};

std::optional<Aria2RpcProgress> parse_aria2_tell_active_response(const std::string& json);
```

- [ ] **Step 4: Implement parser in `src/download_progress.cpp`**

```cpp
std::optional<Aria2RpcProgress> parse_aria2_tell_active_response(const std::string& json) {
    // aria2 encodes lengths/speeds as JSON strings. Empty result[] means not ready.
    if (json.find("\"result\":[]") != std::string::npos) {
        return std::nullopt;
    }
    const std::optional<std::string> completed = json_find_string(json, "completedLength");
    if (!completed.has_value() || completed->empty()) {
        return std::nullopt;
    }
    Aria2RpcProgress out;
    try {
        out.completed = static_cast<std::uintmax_t>(std::stoull(*completed));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (const std::optional<std::string> total = json_find_string(json, "totalLength")) {
        try {
            const std::uintmax_t value = static_cast<std::uintmax_t>(std::stoull(*total));
            if (value > 0) {
                out.total = value;
            }
        } catch (const std::exception&) {
        }
    }
    if (const std::optional<std::string> speed = json_find_string(json, "downloadSpeed")) {
        try {
            out.speed_bps = static_cast<double>(std::stoull(*speed));
        } catch (const std::exception&) {
        }
    }
    return out;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `bash tests/progress_smoke.sh`  
Expected: `progress smoke test passed`

- [ ] **Step 6: Commit**

```bash
git add src/yai.hpp src/download_progress.cpp tests/progress_smoke.sh
git commit -m "$(cat <<'EOF'
Add aria2 tellActive JSON progress parser.

EOF
)"
```

---

### Task 2: Allocate loopback RPC port and pass flags to aria2c

**Files:**
- Modify: `src/yai.hpp` (`allocate_loopback_tcp_port`, `DownloadToolCommand`, replace/extend `downloader_command`)
- Modify: `src/cli_download.cpp`
- Modify: `tests/download_smoke.sh`

**Interfaces:**
- Consumes: none beyond POSIX sockets
- Produces:
  - `std::uint16_t allocate_loopback_tcp_port();` // throws on failure
  - `struct DownloadToolCommand { std::vector<std::string> args; std::optional<std::uint16_t> aria2_rpc_port; };`
  - `DownloadToolCommand build_downloader_command(const std::string& downloader, const std::string& url, const fs::path& part, const fs::path& headers);`
  - Keep a thin wrapper if other call sites still need argv only, **or** update all call sites to `build_downloader_command` (preferred: update `run_downloader` only; grep for `downloader_command` and migrate)

- [ ] **Step 1: Extend fake aria2c logging in `tests/download_smoke.sh`**

In the fake `aria2c` script, capture RPC flags and include them in the log line:

```bash
enable_rpc=""
rpc_port=""
while (($#)); do
  case "$1" in
    --dir)
      dir="$2"
      shift 2
      ;;
    --out)
      out="$2"
      shift 2
      ;;
    --file-allocation=*)
      file_allocation="${1#*=}"
      shift
      ;;
    --enable-rpc|--enable-rpc=*)
      enable_rpc="1"
      shift
      ;;
    --rpc-listen-port=*)
      rpc_port="${1#*=}"
      shift
      ;;
    --*)
      shift
      ;;
    *)
      url="$1"
      shift
      ;;
  esac
done
# ... existing dir/out check ...
printf 'aria2c\t%s\tfile-allocation=%s\tenable-rpc=%s\trpc-port=%s\n' \
  "$url" "$file_allocation" "${enable_rpc:-0}" "${rpc_port:-}" >> "${FAKE_DOWNLOADER_LOG:?}"
```

Update the existing `grep -Fq` assertions that match the old 3-field log line so they still pass (match a prefix, or update to the new format). Add near the aria2 auto-download assertion:

```bash
grep -E $'aria2c\thttps://example.invalid/AutoDownload-x86_64.AppImage\tfile-allocation=none\tenable-rpc=1\trpc-port=[0-9]+$' \
  "$TMP_HOME/auto-downloader.log"
```

Do the same for the install aria2 assertion if present.

- [ ] **Step 2: Run download smoke to verify it fails**

Run: `bash tests/download_smoke.sh`  
Expected: FAIL — log lacks `enable-rpc=1` / `rpc-port=`.

- [ ] **Step 3: Implement port allocation + `build_downloader_command`**

In `src/yai.hpp`, add declarations (and `#include <cstdint>` already present):

```cpp
std::uint16_t allocate_loopback_tcp_port();

struct DownloadToolCommand {
    std::vector<std::string> args;
    std::optional<std::uint16_t> aria2_rpc_port;
};

DownloadToolCommand build_downloader_command(
    const std::string& downloader,
    const std::string& url,
    const fs::path& part,
    const fs::path& headers);
```

If `downloader_command` remains for compatibility, make it call `build_downloader_command(...).args`. Prefer migrating callers.

In `src/cli_download.cpp` (need `#include <sys/socket.h>`, `#include <netinet/in.h>`, `#include <arpa/inet.h>` if not pulled via `yai.hpp`—add locally if compile requires):

```cpp
std::uint16_t allocate_loopback_tcp_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(tr("failed to allocate loopback TCP port: ") + std::strerror(errno));
    }
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int err = errno;
        close(fd);
        throw std::runtime_error(tr("failed to bind loopback TCP port: ") + std::strerror(err));
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        const int err = errno;
        close(fd);
        throw std::runtime_error(tr("failed to read loopback TCP port: ") + std::strerror(err));
    }
    close(fd);
    return ntohs(addr.sin_port);
}
```

For aria2 branch of `build_downloader_command`:

```cpp
    const std::uint16_t port = allocate_loopback_tcp_port();
    DownloadToolCommand cmd;
    cmd.aria2_rpc_port = port;
    cmd.args = {
        "aria2c",
        "--quiet=true",
        "--console-log-level=error",
        "--summary-interval=0",
        "--download-result=hide",
        "--auto-save-interval=1",
        "--allow-overwrite=true",
        "--auto-file-renaming=false",
        "--continue=true",
        "--file-allocation=none",
        "--max-connection-per-server=16",
        "--split=16",
        "--max-tries=3",
        "--retry-wait=1",
        "--enable-rpc=true",
        "--rpc-listen-all=false",
        "--rpc-allow-origin-all=true",
        "--rpc-listen-port=" + std::to_string(port),
        "--dir",
        part.parent_path().string(),
        "--out",
        part.filename().string(),
        url,
    };
    return cmd;
```

Curl/wget branches: `aria2_rpc_port = nullopt`, same args as today.

Update `run_downloader` to use `build_downloader_command`. For aria2 only, on non-zero exit, if attempts < 3, rebuild with a new port and retry **when** output/stderr mentions bind/listen/address already in use (case-insensitive substring match on `bind`, `Address already in use`, or `Failed to listen`). Otherwise keep single-shot behavior. Pass `cmd.aria2_rpc_port` into `run_process_capture_download_progress`—for this task, temporarily extend the signature to accept `std::optional<std::uint16_t> aria2_rpc_port = std::nullopt` and **ignore** it inside `process.cpp` (wire-up in Task 3). Update the declaration in `yai.hpp` accordingly:

```cpp
ProcessResult run_process_capture_download_progress(
    const std::vector<std::string>& args,
    const fs::path& part,
    const fs::path& headers,
    std::optional<std::uint16_t> aria2_rpc_port = std::nullopt);
```

- [ ] **Step 4: Run download smoke to verify it passes**

Run: `bash tests/download_smoke.sh`  
Expected: pass (RPC flags present in fake log).

- [ ] **Step 5: Commit**

```bash
git add src/yai.hpp src/cli_download.cpp src/process.cpp tests/download_smoke.sh
git commit -m "$(cat <<'EOF'
Enable loopback JSON-RPC on aria2c downloads.

EOF
)"
```

---

### Task 3: Query RPC on progress ticks; stop using `.aria2` / apparent size for aria2

**Files:**
- Modify: `src/yai.hpp` (`query_aria2_rpc_progress`, simplify `DownloadProgressState`, change `render_download_progress` / downloaded-bytes APIs as needed)
- Modify: `src/download_progress.cpp`
- Modify: `src/process.cpp`
- Modify: `tests/progress_smoke.sh` (hold-last behavior for query helper if tested with fixture injection)

**Interfaces:**
- Consumes: `parse_aria2_tell_active_response`, `run_process_capture_timeout`, `allocate` port from Task 2
- Produces:
  - `std::optional<Aria2RpcProgress> query_aria2_rpc_progress(std::uint16_t port);`
  - Progress snapshot uses RPC when `aria2_rpc_port` is set

- [ ] **Step 1: Add a failing unit check for hold-last semantics via state**

In `tests/progress_smoke.sh`, after the parse checks, add a focused check that documents snapshot behavior using a **test-only path** is hard without mocking curl. Instead, test the pure merge helper if you introduce one:

```cpp
std::optional<Aria2RpcProgress> merge_aria2_rpc_progress(
    const std::optional<Aria2RpcProgress>& previous,
    const std::optional<Aria2RpcProgress>& current);
```

Semantics:

- `current` present → return `current`
- `current` missing → return `previous`
- both missing → `nullopt`

```cpp
    {
        Aria2RpcProgress prev;
        prev.completed = 1000;
        prev.total = 5000;
        prev.speed_bps = 10.0;
        const auto held = merge_aria2_rpc_progress(prev, std::nullopt);
        require(held.has_value() && held->completed == 1000, "hold last rpc progress");
        Aria2RpcProgress next;
        next.completed = 2000;
        const auto advanced = merge_aria2_rpc_progress(prev, next);
        require(advanced.has_value() && advanced->completed == 2000, "accept newer rpc progress");
        require(!merge_aria2_rpc_progress(std::nullopt, std::nullopt).has_value(), "no rpc yet");
    }
```

- [ ] **Step 2: Run `bash tests/progress_smoke.sh` — expect fail on undeclared `merge_aria2_rpc_progress`**

- [ ] **Step 3: Implement query + merge + wire snapshot**

```cpp
std::optional<Aria2RpcProgress> merge_aria2_rpc_progress(
    const std::optional<Aria2RpcProgress>& previous,
    const std::optional<Aria2RpcProgress>& current) {
    if (current.has_value()) {
        return current;
    }
    return previous;
}

std::optional<Aria2RpcProgress> query_aria2_rpc_progress(std::uint16_t port) {
    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/jsonrpc";
    const std::string body =
        "{\"jsonrpc\":\"2.0\",\"id\":\"yai\",\"method\":\"aria2.tellActive\",\"params\":[]}";
    // Write body to a temp file or use curl --data; prefer argv:
    const ProcessResult result = run_process_capture_timeout({
        "curl",
        "--silent",
        "--show-error",
        "--max-time",
        "1",
        "--header",
        "Content-Type: application/json",
        "--data",
        body,
        url,
    }, 1500);
    if (result.exit_code != 0 || result.timed_out) {
        return std::nullopt;
    }
    return parse_aria2_tell_active_response(result.output);
}
```

Update `DownloadProgressState`:

```cpp
struct DownloadProgressState {
    std::vector<DownloadProgressSample> samples;
    std::optional<std::chrono::steady_clock::time_point> last_progress_time;
    std::optional<Aria2RpcProgress> last_aria2_rpc;
    double bytes_per_second = 0.0;
};
```

Remove `last_downloaded` / `last_downloaded_from_aria2` if nothing else needs them.

Change `render_download_progress` / internal `download_progress_snapshot` to take `std::optional<std::uint16_t> aria2_rpc_port`.

Snapshot logic:

```cpp
std::uintmax_t downloaded = 0;
std::optional<std::uintmax_t> total = download_total_from_headers(headers);
double rpc_speed = -1.0;

if (aria2_rpc_port.has_value()) {
    const auto merged = merge_aria2_rpc_progress(
        state.last_aria2_rpc,
        query_aria2_rpc_progress(*aria2_rpc_port));
    state.last_aria2_rpc = merged;
    if (merged.has_value()) {
        downloaded = merged->completed;
        if (merged->total.has_value()) {
            total = merged->total;
        }
        if (merged->speed_bps.has_value()) {
            rpc_speed = *merged->speed_bps;
        }
    }
} else {
    // curl/wget: sequential growth — file_size is correct.
    std::error_code ec;
    downloaded = fs::file_size(part, ec);
    if (ec) {
        return std::nullopt;
    }
}
```

Speed: if `rpc_speed >= 0`, set `snapshot.bytes_per_second = rpc_speed`; else use `download_progress_recent_speed(state, now, downloaded)`.

**Delete** from `download_progress.cpp`: `aria2_control_downloaded_bytes` and helpers only used by it, `download_progress_part_bytes_fallback`, and preferably `download_progress_downloaded_bytes` if unused—or reduce it to file_size-only for non-RPC callers. Grep and remove dead declarations from `yai.hpp`.

In `src/process.cpp`, pass `aria2_rpc_port` through to `render_download_progress`.

- [ ] **Step 4: Run tests**

```bash
bash tests/progress_smoke.sh
bash tests/download_smoke.sh
make -j"$(nproc)"
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/yai.hpp src/download_progress.cpp src/process.cpp tests/progress_smoke.sh
git commit -m "$(cat <<'EOF'
Drive aria2 download progress from JSON-RPC completedLength.

EOF
)"
```

---

### Task 4: README note + leftover cleanup

**Files:**
- Modify: `README.md` (progress paragraph that currently mentions aria2 control file)
- Grep leftovers: `.aria2` bitfield, `download_progress_downloaded_bytes`, `last_downloaded_from_aria2`

- [ ] **Step 1: Update README progress wording**

Replace the sentence about reading aria2’s control file with:

```markdown
recent transfer speed instead of lifetime average speed; for `aria2c`, yai queries
aria2's local JSON-RPC `completedLength` so multi-connection sparse writes do not
inflate the downloaded byte count.
```

- [ ] **Step 2: Grep cleanup**

```bash
rg -n 'aria2_control_downloaded|last_downloaded_from_aria2|download_progress_part_bytes_fallback|parse_aria2_tell_active|query_aria2_rpc' src tests README.md
```

Ensure no progress path still documents or uses control-file bitfields. Keep `.aria2` **cleanup** on disk (`remove_best_effort(part + ".aria2")`) — that stays; only progress probing changes.

- [ ] **Step 3: Final verification**

```bash
bash tests/progress_smoke.sh
bash tests/download_smoke.sh
make -j"$(nproc)"
```

Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add README.md src tests
git commit -m "$(cat <<'EOF'
Document aria2 RPC download progress.

EOF
)"
```

---

## Spec coverage (self-review)

| Spec requirement | Task |
|------------------|------|
| Loopback RPC flags on aria2c | Task 2 |
| Poll `completedLength` / usable total / speed | Task 3 |
| curl/wget keep file size | Task 3 |
| Remove `.aria2` bitfield progress probe | Task 3 |
| Never fall back to apparent `.part` for aria2 | Task 3 |
| Port allocation + 3 retries on busy | Task 2 |
| Parallel batch = per-child port | Task 2 (each child process builds its own command) |
| Smoke: JSON parse | Task 1 |
| Smoke: argv flags | Task 2 |
| README note | Task 4 |
| RPC via curl to `/jsonrpc` | Task 3 |

## Placeholder scan

No TBD / “add validation later” steps. Exact APIs and commands included.

## Type consistency

- `Aria2RpcProgress` / `parse_aria2_tell_active_response` — Task 1
- `merge_aria2_rpc_progress` / `query_aria2_rpc_progress` — Task 3
- `DownloadToolCommand.aria2_rpc_port` → `run_process_capture_download_progress` → `render_download_progress` — Tasks 2–3
