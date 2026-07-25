### Task 1: Extract `src/commands_lifecycle.cpp`

**Files:**
- Create: `src/commands_lifecycle.cpp`
- Modify: `src/commands.cpp` (remove moved block)
- Modify: `Makefile` (add `src/commands_lifecycle.cpp`)

**Interfaces:**
- Consumes via `yai.hpp`: shared helpers still in `commands.cpp` (`resolve_installed_package_id`, `print_mode_line`, `print_fuse_fallback_line`), plus resolver/appimage/core APIs
- Produces: lifecycle public APIs listed above; anon `download_output_name`

- [ ] **Step 1: Re-locate markers in current `commands.cpp`**

```bash
rg -n '^(std::string download_output_name|void download_app|void rollback_app|struct UpgradeCommandOptions)' src/commands.cpp
```

Record:
- `L0` = `download_output_name`
- `L1` = last line of `rollback_app`
- `U0` = `struct UpgradeCommandOptions` (must stay until Task 2)

- [ ] **Step 2: Create `src/commands_lifecycle.cpp`**

```cpp
#include "yai.hpp"

// Download, install, repair, and rollback command workflows.

namespace {
// download_output_name ...
} // namespace

void download_app(...) { ... }
// ... through rollback_app
```

Move contiguous `[L0, L1]`. Wrap only `download_output_name` in `namespace { }`. Keep all other moved symbols that are in `yai.hpp` at file scope.

- [ ] **Step 3: Delete `[L0, L1]` from `src/commands.cpp`**

Afterward, shared helpers should be followed by `struct UpgradeCommandOptions` (aside from blank lines).

- [ ] **Step 4: Add to Makefile and build**

Insert `src/commands_lifecycle.cpp \` into `SRC` (alphabetically near other `commands_*` files).

```bash
make clean && make
```

Expected: exit 0.

---

