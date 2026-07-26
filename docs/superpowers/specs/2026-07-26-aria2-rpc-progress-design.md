# aria2 RPC download progress (`completedLength`)

**Date:** 2026-07-26  
**Status:** Approved  
**Goal:** For `aria2c` downloads, report “Downloaded” from aria2 JSON-RPC `completedLength` instead of `.part` size or `.aria2` control-file bitfields.

## Problem

- Default aria2 settings use multi-connection (`--split=16`) with `--file-allocation=none`.
- The `.part` file’s apparent size (`st_size`) often jumps to the highest written offset while most of the file is still a hole, so progress “Downloaded” severely over-reports.
- Parsing the binary `*.aria2` control file is brittle (torn auto-save reads, format edge cases) and has already caused both backwards jumps and lock-in on inflated estimates.
- GitHub Release transfers often look fine because multi-range may not apply; AppImage CDN hosts that support Range expose the bug.

## Scope

**In scope**

- Enable a loopback-only JSON-RPC endpoint for each `aria2c` download process
- Poll `completedLength` (and usable `totalLength` / `downloadSpeed` when present) during existing progress ticks
- Keep `curl` / `wget` / `wget2` progress on file size (sequential growth)
- Remove reliance on `.aria2` bitfield parsing for progress UI
- Smoke coverage for RPC URL formatting / JSON field extraction and aria2 argv flags
- Short README note that aria2 progress uses RPC `completedLength`

**Out of scope**

- Changing split/connection count or file-allocation policy for speed
- Persisting RPC across process restarts
- RPC authentication / remote RPC
- Replacing curl/wget progress with another mechanism
- Batch UI layout changes (events still carry `done` / `total` / `rate`)

## Approach

1. When building the `aria2c` command, allocate a free TCP port on `127.0.0.1` and pass:
   - `--enable-rpc=true`
   - `--rpc-listen-all=false`
   - `--rpc-listen-port=<port>`
   - `--rpc-allow-origin-all=true`
   - No `--rpc-secret` (process-local, loopback-only, lifetime = download)
2. Carry the chosen port (or base URL) into the download progress path so each tick can query RPC.
3. On each progress tick for aria2:
   - POST JSON-RPC `aria2.tellActive` (fallback `aria2.tellStatus` with gid if needed)
   - Read `completedLength` as downloaded bytes
   - Prefer RPC `totalLength` when > 0; otherwise keep existing header-derived total
   - Prefer RPC `downloadSpeed` when present for the speed field
4. If RPC is not ready or the call fails: keep the last good RPC reading, or `0` if none yet. **Never** fall back to apparent `.part` size for aria2.
5. Delete (or leave unused) the `.aria2` control-file bitfield parser from the progress probe path.

## Port selection

- Prefer binding a temporary `SOCK_STREAM` socket to `127.0.0.1:0`, read the assigned port, close it, then pass that port to aria2.
- Accept a small race (port reused between close and aria2 bind). If aria2 fails to start because the port is busy, rebuild the command with a freshly allocated port and retry up to 3 times, then fail with an explicit message.
- Parallel batch downloads: each child gets its own port independently (no global singleton).
- RPC HTTP calls should use the already-required `curl` tool against `http://127.0.0.1:<port>/jsonrpc` (short timeout), matching existing network helpers—not a new HTTP library.

## Progress data flow

```
aria2c (--enable-rpc, port P)
        │
        ▼
run_process_capture_download_progress / batch child
        │  every ~200ms
        ▼
query 127.0.0.1:P  →  completedLength / totalLength / downloadSpeed
        │
        ▼
existing render_download_progress / BatchProgressEvent
```

Header dump / prefetch Content-Length remains the fallback for **total** when RPC omits `totalLength`.

## API / code touchpoints (expected)

- `src/cli_download.cpp` — aria2 argv + port allocation helper
- `src/process.cpp` — pass RPC endpoint into progress rendering for aria2 runs
- `src/download_progress.cpp` — RPC query + JSON field extract; drop control-file progress probe
- `src/yai.hpp` — declarations for port helper / RPC progress probe as needed
- `tests/progress_smoke.sh` — unit-test JSON/`completedLength` parsing without network
- `tests/download_smoke.sh` — assert fake aria2c receives RPC-related flags
- `README.md` — one-paragraph progress note update

Exact function names may follow the implementation plan; behavior above is normative.

## Error handling

| Situation | Behavior |
|-----------|----------|
| RPC not listening yet (first ticks) | Show `0` or last good; do not use `.part` size |
| Transient RPC failure mid-download | Hold last good `completedLength` |
| aria2 exits non-zero | Existing downloader error path unchanged |
| Port bind conflict at aria2 start | Retry with a new port up to 3 times, then fail with an explicit message |

## Testing

- **Unit / smoke:** parse a fixture JSON-RPC `tellActive` response → `completedLength` / `totalLength` / `downloadSpeed`
- **Argv smoke:** `download_smoke` fake aria2c log contains `--enable-rpc` and `--rpc-listen-port=`
- **Manual:** multi-connection AppImage (or large Range-capable URL) download — “Downloaded” tracks real progress, does not snap to sparse apparent size

## Non-goals / explicit rejects

- Re-introducing `.part` apparent size as an aria2 progress source
- Keeping dual progress sources (bitfield **and** RPC) “just in case”
- Enabling RPC on non-loopback interfaces
