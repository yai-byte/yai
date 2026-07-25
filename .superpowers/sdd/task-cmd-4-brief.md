### Task 4: Extract `src/commands_repo.cpp` and thin `commands.cpp`

**Files:**
- Create: `src/commands_repo.cpp`
- Modify: `src/commands.cpp` (leave only shared helpers; wrap `command_match_list`; update banner)
- Modify: `Makefile` (finalize full `SRC` list)

**Interfaces:**
- Repo file produces all `repo_*` / `mirror_*` public APIs; anon helpers: `parse_repo_update_target`, `write_empty_repo_index`, `refresh_repo_index`, `refresh_selected_repo_indexes`, `store_repo_index_updates`, `print_repo_update_result`
- Thin `commands.cpp` produces shared public helpers only

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(void repo_list_app|void mirror_app|std::string command_match_list|std::string resolve_installed_package_id)' src/commands.cpp
```

- [ ] **Step 2: Create `src/commands_repo.cpp`**

Move from `repo_list_app` through end of file. Put INTERNAL helpers in `namespace { }`; keep PUBLIC `repo_*` / `mirror_*` at file scope.

- [ ] **Step 3: Thin `src/commands.cpp`**

Final content should be approximately:

```cpp
#include "yai.hpp"

// Shared helpers used by multiple command-family translation units.

namespace {
std::string command_match_list(const std::vector<std::string>& values) {
    ...
}
} // namespace

std::string resolve_installed_package_id(const std::string& pattern) {
    ...
}

void print_mode_line(const std::string& mode) {
    ...
}

void print_fuse_fallback_line() {
    ...
}
```

- [ ] **Step 4: Finalize Makefile `SRC`**

Exact list:

```make
SRC := \
	src/arch.cpp \
	src/appimage.cpp \
	src/cli_download.cpp \
	src/commands.cpp \
	src/commands_doctor.cpp \
	src/commands_lifecycle.cpp \
	src/commands_query.cpp \
	src/commands_repo.cpp \
	src/commands_upgrade.cpp \
	src/core.cpp \
	src/download_progress.cpp \
	src/i18n.cpp \
	src/json.cpp \
	src/main.cpp \
	src/process.cpp \
	src/repo.cpp \
	src/resolver.cpp
```

- [ ] **Step 5: Build**

```bash
make clean && make
```

Expected: exit 0; `./yai` produced.

---

