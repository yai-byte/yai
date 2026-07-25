### Task 1: Extract `src/i18n.cpp`

**Files:**
- Create: `src/i18n.cpp`
- Modify: `src/core.cpp` (remove moved block)
- Modify: `Makefile` (add `src/i18n.cpp`)

**Interfaces:**
- Consumes (via `yai.hpp`, defined later in `core.cpp`): `env_string`, `ascii_lower`, `replace_all`
- Produces: `current_language`, `use_chinese`, `tr`, `tr_format` plus internal PO helpers

- [ ] **Step 1: Create `src/i18n.cpp` from current `core.cpp` slices**

Run from `/home/fsx/yai`:

```bash
python3 - <<'PY'
from pathlib import Path
core = Path('src/core.cpp').read_text().splitlines(True)
# Markers (1-based inclusive ranges from pre-split core.cpp):
# anon PO helpers: lines 13-166
# language + tr: lines 183-234
anon = ''.join(core[12:166])          # 13-166
lang = ''.join(core[182:234])         # 183-234
header = '''#include "yai.hpp"

#include <unordered_map>

// Locale selection and gettext-style po catalog loading for tr()/tr_format().

'''
Path('src/i18n.cpp').write_text(header + anon + '\n' + lang)
print('wrote src/i18n.cpp', len(header + anon + '\n' + lang), 'bytes')
PY
```

- [ ] **Step 2: Delete the same slices from `src/core.cpp`**

```bash
python3 - <<'PY'
from pathlib import Path
core = Path('src/core.cpp').read_text().splitlines(True)
# Keep file header lines 1-12, drop 13-166 and 183-234.
# After deleting 13-166, former 183-234 shifts: delete in one pass by index.
keep = core[0:12] + core[166:182] + core[234:]
# core[166:182] is blank line + env_string + ascii_lower (lines 167-182)
Path('src/core.cpp').write_text(''.join(keep))
print('core.cpp now', len(keep), 'lines')
PY
```

Expected: `core.cpp` still starts with `#include "yai.hpp"`, then `APPIMAGE_FEED_URL`, then `env_string` / `ascii_lower`, then `home_dir`, … and no `namespace {` PO helpers / no `tr(`.

- [ ] **Step 3: Add `src/i18n.cpp` to `Makefile` `SRC`**

Insert alphabetically among sources:

```make
	src/i18n.cpp \
```

after `src/core.cpp \` (or keep alpha order with other files as in the design doc).

- [ ] **Step 4: Build**

```bash
make clean && make
```

Expected: link succeeds. If undefined `replace_all` / `env_string` / `ascii_lower`, confirm they remain defined in `core.cpp` and declared in `yai.hpp`.

---

