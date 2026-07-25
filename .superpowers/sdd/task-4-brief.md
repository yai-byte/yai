### Task 4: Regression verification

**Files:** none (run only)

- [ ] **Step 1: Build once more**

```bash
make clean && make
```

Expected: exit 0; produces `./yai`.

- [ ] **Step 2: Run progress + download smokes (moved code paths)**

```bash
YAI_LANG=en tests/progress_smoke.sh
YAI_LANG=en tests/download_smoke.sh
```

Expected: each prints a `* smoke test passed` line and exits 0.

- [ ] **Step 3: Run baseline stage/repo smokes**

```bash
YAI_LANG=en tests/stage1_smoke.sh
YAI_LANG=en tests/repo_smoke.sh
YAI_LANG=en tests/arch_smoke.sh
YAI_LANG=en tests/json_smoke.sh
```

Expected: all pass.

- [ ] **Step 4: Spot-check line counts**

```bash
wc -l src/core.cpp src/i18n.cpp src/process.cpp src/download_progress.cpp
```

Expected (approximate):
- `core.cpp` ≲ 500 lines
- `i18n.cpp` ~220–250
- `process.cpp` ~450–550 (helpers + download progress capture)
- `download_progress.cpp` ~500–600
- Sum ≈ previous `core.cpp` size (± headers/comments)

- [ ] **Step 5: Commit only if the user asks**

Do not run `git commit` unless explicitly requested.

---

## Self-review checklist

1. **Spec coverage:** i18n / process / download_progress / thin core / Makefile / no header split / verification — each has a task.
2. **Placeholders:** none intentionally left.
3. **Type consistency:** public symbols unchanged; internals confined to anonymous namespaces per owning TU.
