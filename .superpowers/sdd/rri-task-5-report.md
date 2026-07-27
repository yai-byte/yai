# Task 5 Report: i18n + usage polish

**Status:** PASS  
**Commits:** `c340514` (i18n + help); `dffc421`/`17ee6c4` (report)  
**Tests:**
- `YAI_LANG=zh ./yai help` shows `repo resolve` and `--recrawl`
- `YAI_LANG=zh ./yai repo` / `repo resolve --show abc` / `repo resolve --nope` → Chinese errors
- `bash tests/repo_resolve_index_smoke.sh` → PASS (all four sections)

**Concerns:**
- Pre-existing msgid gaps remain outside Tasks 3–4 (e.g. `batch stream*`, `: response exceeded maximum size`); not synced in this task.
- Installed `~/.local/share/yai/po` may lag until `make install`; in-tree `./yai` loads `./po` first.

**Report:** `.superpowers/sdd/rri-task-5-report.md`

## Changes

- `src/cli_download.cpp`: global help lists `yai repo resolve` options
- `po/en.po` / `po/zh.po`: resolve/error/summary/`--recrawl` usage msgids; Chinese msgstr for resolve paths
