# Task 2 Report: Extract `src/resolver_url.cpp`

## Status

Completed.

## Changes

- Moved the URL/HTML helper slice from `strip_url_fragment_query` through
  `appimage_url_from_download_landing_page` into `src/resolver_url.cpp`.
- Kept `html_appimage_urls`, `text_looks_like_html`, and
  `appimage_url_from_download_landing_html` in anonymous namespaces.
- Added the public `appimage_url_from_download_landing_text` wrapper because
  `resolver.cpp` still needs to inspect fetched landing-page HTML while the
  underlying HTML parser remains internal.
- Left `install_arch_for_options` and all later resolver code in
  `src/resolver.cpp`.
- Added `src/resolver_url.cpp` to the Makefile source list.

## Verification

`make clean && make` completed with exit code 0 using the configured
`-Wall -Wextra -Wpedantic` flags. No compiler warnings were emitted.

## Commits

None. `/home/fsx/yai` is not currently detected as a Git repository.

## Concerns

No build blockers. The new text wrapper slightly expands the public helper
surface solely to preserve the existing landing-page behavior across the
translation-unit boundary.

## Fix: promote landing_html

- Moved `appimage_url_from_download_landing_html` out of the anonymous namespace
  to file scope in `src/resolver_url.cpp`; kept `text_looks_like_html` and
  `html_appimage_urls` internal.
- Removed the redundant `appimage_url_from_download_landing_text` wrapper.
- Replaced the `_text` declaration in `src/yai.hpp` with
  `appimage_url_from_download_landing_html` (same signature).
- Restored the `src/resolver.cpp` call site to
  `appimage_url_from_download_landing_html(...)`.
- `make clean && make` succeeded (exit 0, no warnings).
