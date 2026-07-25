### Task 2: Extract `src/commands_upgrade.cpp`

**Files:**
- Create: `src/commands_upgrade.cpp`
- Modify: `src/commands.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `resolve_installed_package_id`, `print_mode_line`, `print_fuse_fallback_line`, `previous_version_dir`, `save_previous_version`, `restore_previous_version`, etc. via `yai.hpp`
- Produces public: `cleanup_update_candidate`, `upgrade_app`, `update_app`
- Internal (anonymous namespace): `UpgradeCommandOptions`, `UpdateContext`, `UpdatePreviewResult`, and every upgrade helper not declared in `yai.hpp` (see symbol list in the design doc / pre-split inventory)

- [ ] **Step 1: Re-locate markers**

```bash
rg -n '^(struct UpgradeCommandOptions|void update_app|void remove_if_exists)' src/commands.cpp
```

- `U0` = `struct UpgradeCommandOptions`
- `U1` = last line of `update_app`
- `Q0` = `remove_if_exists` (must remain until Task 3)

- [ ] **Step 2: Create `src/commands_upgrade.cpp`**

Skeleton:

```cpp
#include "yai.hpp"

// Upgrade / update preview and apply workflows.

namespace {
struct UpgradeCommandOptions { ... };
// ... all INTERNAL upgrade helpers and types ...
} // namespace

void cleanup_update_candidate(...) { ... }
void upgrade_app(...) { ... }
void update_app(...) { ... }
```

Important: `cleanup_update_candidate`, `upgrade_app`, and `update_app` must be **outside** the anonymous namespace. Helpers they call that are not in `yai.hpp` must be **inside** it (same TU).

Practical approach: copy `[U0, U1]` into the new file, open `namespace {` before `struct UpgradeCommandOptions`, close it immediately before `void cleanup_update_candidate`, and also ensure `upgrade_app` / `update_app` remain outside. If other PUBLIC symbols appear mid-block, keep those outside too (inventory shows only those three public APIs in this slice).

- [ ] **Step 3: Remove `[U0, U1]` from `commands.cpp`**

- [ ] **Step 4: Makefile + build**

```bash
make clean && make
```

Expected: exit 0. Undefined-reference failures mean a helper was left outside/inside the wrong namespace or still referenced from another TU without a header declaration.

---

