# Task 2 Report

- Status: Complete
- Commits: None
- Changes: Extracted the full upgrade/update workflow into `src/commands_upgrade.cpp`; retained `remove_if_exists` and later handlers in `src/commands.cpp`; added the new translation unit to `Makefile`.
- Linkage: Only `cleanup_update_candidate`, `upgrade_app`, and `update_app` have external linkage in the new translation unit; upgrade-specific types and helpers are in an anonymous namespace.
- Build: `make clean && make` completed successfully with exit code 0.
- Concerns: None.
