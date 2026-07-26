# Task 5 Report

**Status:** DONE
**Commits:** `4872d49` (impl); `7abaaa3`/`7f08172` (report)
**Tests:** make PASS; website_mtime_smoke PASS; appimage_feed_smoke PASS (counts unchanged)
**Concerns:** Fixture under `download/krita` for follow; root older AppImage forces pool vs early-return
**Report:** `/home/fsx/yai/.worktrees/website-mtime-candidate/.superpowers/sdd/task-5-report.md`

## Fix: stale_penalty before arch score (design §5)

**Finding:** `website_candidate_better` ordered by arch score before `stale_penalty`, so a stale attic AppImage with arch tag (score 100) could beat a non-stale generic AppImage (score 20).

**Change:** Keep arch scoring; if either score `< 0`, still prefer non-negative / higher score for invalid rejection. When both scores `≥ 0`: `stale_penalty` first, then known/newer mtime, then arch score as tie-break.

**Files:** `src/resolver_url.cpp`, `tests/website_mtime_smoke.sh` (added stale x86_64 vs non-stale generic unit + best-picker cases).

**Test evidence (2026-07-26):**
- `bash tests/website_mtime_smoke.sh` → PASS (unit + integration)
- `YAI_LANG=en bash tests/appimage_feed_smoke.sh` → PASS (`appimage feed smoke test passed`)
