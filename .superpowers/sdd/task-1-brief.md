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

