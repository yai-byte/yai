### Task 3: Extract `src/commands_query.cpp`

**Files:**
- Create: `src/commands_query.cpp`
- Modify: `src/commands.cpp` (move query block **and** `search_summary`)
- Modify: `Makefile`

**Interfaces:**
- Produces public: `remove_if_exists`, `remove_app`, `list_apps`, `search_packages`, `info_package`
- Internal: `search_summary`, `print_package_source_reason`, `print_package_source_info`

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(std::string search_summary|void remove_if_exists|void info_package|void repo_list_app)' src/commands.cpp
```

- [ ] **Step 2: Create `src/commands_query.cpp`**

```cpp
#include "yai.hpp"

// Remove, list, search, and info command workflows.

namespace {
std::string search_summary(...) { ... }
void print_package_source_reason(...) { ... }
void print_package_source_info(...) { ... }
} // namespace

void remove_if_exists(...) { ... }
void remove_app(...) { ... }
void list_apps() { ... }
void search_packages(...) { ... }
void info_package(...) { ... }
```

Move `search_summary` from the top of `commands.cpp` into this file’s anonymous namespace (it is only used by `search_packages`).

- [ ] **Step 3: Remove `search_summary` and the query slice from `commands.cpp`**

`commands.cpp` should now contain: banner, anon `command_match_list` (if already wrapped) or still file-scope pending Task 4, `resolve_installed_package_id`, `print_mode_line`, `print_fuse_fallback_line`, then repo/mirror block.

- [ ] **Step 4: Makefile + build**

```bash
make clean && make
```

Expected: exit 0.

---

