### Task 3: Extract `src/download_progress.cpp`

**Files:**
- Create: `src/download_progress.cpp`
- Modify: `src/core.cpp` (remove progress block)
- Modify: `Makefile` (add `src/download_progress.cpp`)

**Interfaces:**
- Consumes: `tr`, `tr_format`, filesystem helpers as already used
- Produces: all progress-related APIs declared in `yai.hpp` from `format_byte_count` through `clear_download_progress`
- Internal (anonymous namespace): endian/bitfield/aria2 helpers, `DownloadProgressSnapshot`, `download_progress_snapshot`, `download_progress_knows_total`, `format_download_progress_stats`, `progress_bar_width`, `render_progress_line`, `write_progress_line`, and any other helper in that slice not listed in `yai.hpp`

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(std::string format_byte_count|void clear_download_progress|void ensure_directory)' src/core.cpp
```

- [ ] **Step 2: Create `src/download_progress.cpp` with the progress slice**

```cpp
#include "yai.hpp"

// Terminal download progress rendering and aria2/curl progress probes.

```

Move `format_byte_count` through `clear_download_progress` inclusive. Put non-API helpers/types in `namespace { }`. Keep these at file scope (they are in `yai.hpp`):

- `format_byte_count`
- `format_duration_seconds`
- `display_width`
- `truncate_display_width`
- `terminal_width`
- `download_total_from_headers`
- `download_progress_downloaded_bytes`
- `download_progress_recent_speed`
- `progress_bar`
- `render_download_progress`
- `clear_download_progress`

Note: `aria2_control_downloaded_bytes` is **not** in `yai.hpp` → anonymous namespace. Same for `DownloadProgressSnapshot` and friends.

- [ ] **Step 3: Remove that slice from `src/core.cpp`**

Afterward, `output_has_fuse_error` should be immediately followed by `ensure_directory` (aside from blank lines).

- [ ] **Step 4: Update `core.cpp` banner comment**

Replace the old multi-responsibility comment with something accurate, e.g.:

```cpp
// Shared path/string helpers, filesystem utilities, mirror/network config, and
// install path derivation. Process execution, download progress UI, and i18n
// live in process.cpp, download_progress.cpp, and i18n.cpp.
```

- [ ] **Step 5: Finalize `Makefile` `SRC`**

Exact list:

```make
SRC := \
	src/arch.cpp \
	src/appimage.cpp \
	src/cli_download.cpp \
	src/commands.cpp \
	src/commands_doctor.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/resolver.cpp
```

- [ ] **Step 6: Build**

```bash
make clean && make
```

Expected: clean build of `yai`.

---

