#!/usr/bin/env bash
# Runs the whole smoke suite under the hermetic network guard.
#
# Every test runs with a curl shim on PATH that refuses any request leaving the
# machine, so a test that starts depending on the internet fails here instead
# of passing locally and breaking in CI.
#
# Usage: tests/run_all.sh [name-filter ...]

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# shellcheck source=network_guard.sh
source "$ROOT/tests/network_guard.sh"
network_guard_init || exit 1

GUARD_PATH="$(network_guard_path)"

# No remote bases are overridden here on purpose: a test that expects default
# behaviour must keep it. Tests that would otherwise dial out set their own
# YAI_*_BASE override, and anything still leaking is caught by the guard.

if [[ ! -x "$ROOT/yai" ]]; then
  echo "run_all: $ROOT/yai not built; run make first" >&2
  exit 1
fi

filters=("$@")

# Tests that intentionally dial out (real api.github.com catalog probing) or
# ship their own curl shim that conflicts with the network guard. They are
# validated separately without the guard rather than under it.
declare -A EXEMPT_REASON=(
  [batch_progress_smoke.sh]="uses default api.github.com catalog probing"
  [repo_resolve_index_smoke.sh]="uses default api.github.com catalog probing"
  [wildcard_multi_smoke.sh]="uses default api.github.com catalog probing"
  [fetch_text_timeout_smoke.sh]="ships its own curl shim (conflicts with guard)"
)

started_at=$SECONDS
passed=0
failed=0
failed_names=()
skipped=0

for test_path in "$ROOT"/tests/*_smoke.sh; do
  name="$(basename "$test_path")"
  if [[ ${#filters[@]} -gt 0 ]]; then
    match=0
    for filter in "${filters[@]}"; do
      [[ "$name" == *"$filter"* ]] && match=1 && break
    done
    [[ "$match" -eq 1 ]] || continue
  fi

  if [[ -n "${EXEMPT_REASON[$name]:-}" ]]; then
    skipped=$((skipped + 1))
    printf 'SKIP    %-34s  (exempt: %s)\n' "$name" "${EXEMPT_REASON[$name]}"
    continue
  fi

  : > "$YAI_NETWORK_VIOLATIONS"

  test_start=$SECONDS
  if PATH="$GUARD_PATH:$PATH" timeout 300 bash "$test_path" >/tmp/yai_run_all.out 2>&1; then
    if [[ -s "$YAI_NETWORK_VIOLATIONS" ]]; then
      status="NETFAIL"
    else
      status="PASS"
    fi
  else
    if [[ -s "$YAI_NETWORK_VIOLATIONS" ]]; then
      status="NETFAIL"
    else
      status="FAIL"
    fi
  fi
  elapsed=$((SECONDS - test_start))

  case "$status" in
    PASS)
      passed=$((passed + 1))
      printf 'PASS    %-34s %3ss\n' "$name" "$elapsed"
      ;;
    NETFAIL)
      failed=$((failed + 1))
      failed_names+=("$name")
      printf 'NETFAIL %-34s %3ss\n' "$name" "$elapsed"
      sort -u "$YAI_NETWORK_VIOLATIONS" | sed 's/^/          /'
      ;;
    *)
      failed=$((failed + 1))
      failed_names+=("$name")
      printf 'FAIL    %-34s %3ss\n' "$name" "$elapsed"
      tail -5 /tmp/yai_run_all.out | sed 's/^/          /'
      ;;
  esac
done

echo
echo "passed: $passed  failed: $failed  skipped: $skipped  total: $((passed + failed + skipped))  elapsed: $((SECONDS - started_at))s"
if [[ "$failed" -gt 0 ]]; then
  printf 'failing: %s\n' "${failed_names[*]}"
  exit 1
fi
