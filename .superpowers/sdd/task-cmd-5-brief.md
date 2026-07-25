### Task 5: Regression verification

**Files:** none unless a smoke hardcodes a stale `g++` source list (then update link lines only).

- [ ] **Step 1: Build**

```bash
make clean && make
```

- [ ] **Step 2: Lifecycle / download smokes**

```bash
YAI_LANG=en tests/download_smoke.sh
YAI_LANG=en tests/stage1_smoke.sh
```

Expected: both pass.

- [ ] **Step 3: Upgrade / later-stage smoke**

```bash
YAI_LANG=en tests/stage5_smoke.sh
```

Expected: pass (covers update/upgrade-related flows if present in suite).

- [ ] **Step 4: Repo / mirror / query-adjacent smokes**

```bash
YAI_LANG=en tests/repo_smoke.sh
YAI_LANG=en tests/mirror_policy_smoke.sh
YAI_LANG=en tests/stage2_smoke.sh
```

Expected: all pass.

- [ ] **Step 5: Line-count spot check**

```bash
wc -l src/commands.cpp src/commands_lifecycle.cpp src/commands_upgrade.cpp \
  src/commands_query.cpp src/commands_repo.cpp src/commands_doctor.cpp
```

Expected (approximate):
- `commands.cpp` ≲ 100
- `commands_lifecycle.cpp` ~280–320
- `commands_upgrade.cpp` ~550–620
- `commands_query.cpp` ~110–140
- `commands_repo.cpp` ~220–260
- Sum of command family files ≈ previous `commands.cpp` + `commands_doctor.cpp` (± banners)

- [ ] **Step 6: Commit only if the user asks**

---

## Self-review checklist

1. **Spec coverage:** lifecycle / upgrade / query / repo / thin shared / Makefile / doctor untouched / verification — each has a task.
2. **Placeholders:** none intentionally left.
3. **Type consistency:** file-local upgrade/repo/query helpers confined to anonymous namespaces; public symbols unchanged and match `yai.hpp`.
