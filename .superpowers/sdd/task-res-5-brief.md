### Task 5: Regression verification

**Files:** none unless a smoke hardcodes a stale `g++` list (then update link lines only).

- [ ] **Step 1: Build**

```bash
make clean && make
```

- [ ] **Step 2: Resolution-heavy smokes**

```bash
YAI_LANG=en tests/download_smoke.sh
YAI_LANG=en tests/stage1_smoke.sh
YAI_LANG=en tests/repo_smoke.sh
YAI_LANG=en tests/mirror_policy_smoke.sh
YAI_LANG=en tests/stage5_smoke.sh
YAI_LANG=en tests/appimage_feed_smoke.sh
```

Expected: all pass. If `appimage_feed_smoke.sh` is flaky due to network, note it separately but do not weaken other failures.

- [ ] **Step 3: Line-count spot check**

```bash
wc -l src/resolver.cpp src/resolver_github.cpp src/resolver_url.cpp src/resolver_website.cpp
```

Expected (approximate):
- `resolver_github.cpp` ~160–200
- `resolver_url.cpp` ~350–400
- `resolver_website.cpp` ~330–380
- `resolver.cpp` ~280–350
- Sum ≈ previous `resolver.cpp` (± banners / namespace lines)

- [ ] **Step 4: Commit only if the user asks**

---

## Self-review checklist

1. **Spec coverage:** github / url / website / thin resolver / Makefile / verification — each has a task.
2. **Placeholders:** none intentionally left.
3. **Cut safety:** website cut starts at `truncate_status_text`, not `install_arch_for_options`.
4. **Visibility:** undeclared website/resolve helpers confined to anonymous namespaces; public symbols match `yai.hpp`.
