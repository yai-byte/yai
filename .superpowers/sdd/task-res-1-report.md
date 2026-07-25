# Task 1 Report: Extract `src/resolver_github.cpp`

## Status
**Complete**

## Markers
| Marker | Location |
|--------|----------|
| G0 `github_api_base` | `src/resolver_github.cpp:5` |
| G1 `download_with_strategy` (last line) | `src/resolver_github.cpp:165` |
| U0 `strip_url_fragment_query` (stays) | `src/resolver.cpp:24` |

## Changes
- **Created** `src/resolver_github.cpp` — moved `[G0, G1]`: GitHub API base, blocklist helpers, `enforce_github_release_policy`, `resolve_github_latest`, `mirror_url_for`, `download_with_strategy`. All `yai.hpp`-declared symbols kept at file scope.
- **Modified** `src/resolver.cpp` — removed moved slice; `package_matches_keyword` now followed by `strip_url_fragment_query`. `contains_case_insensitive` and package match helpers remain.
- **Modified** `Makefile` — added `src/resolver_github.cpp` after `src/resolver.cpp`.

## Build
```
make clean && make
```
Exit code: **0** (no warnings).

## Commits
None (per instructions).

## Concerns
None. Cross-TU linkage verified via successful link of all translation units.
