#!/usr/bin/env bash
# Hermetic network guard for the smoke suite.
#
# Sourced by run_all.sh (or by an individual smoke test) to install a curl shim
# at the front of PATH. The shim allows only requests that never leave the
# machine -- file:// URLs, local filesystem paths, and loopback HTTP -- and
# refuses everything else, recording the URL it rejected.
#
# Without this, several tests quietly reach the public internet: guessed
# third-party pages, hard-coded upstream catalog bases, and the default remote
# index. Those requests make the suite depend on outside servers being up and
# reachable, and they swing runtimes from seconds to minutes.
#
# Usage:
#   source tests/network_guard.sh
#   network_guard_init
#   PATH="$(network_guard_path):$PATH" ./yai ...
#   network_guard_report          # non-zero if anything was refused

network_guard_init() {
  if [[ -n "${YAI_NETWORK_GUARD_DIR:-}" ]]; then
    return 0
  fi

  # Resolve the real curl now and bake it into the shim. Skipping any curl that
  # is itself a guard avoids the shim re-executing itself forever when a test
  # chains its own curl wrapper on top of ours.
  local real_curl=""
  local dir
  local IFS=:
  for dir in $PATH; do
    [[ -n "$dir" && -x "$dir/curl" ]] || continue
    if grep -q "YAI network guard" "$dir/curl" 2>/dev/null; then
      continue
    fi
    real_curl="$dir/curl"
    break
  done
  unset IFS

  if [[ -z "$real_curl" ]]; then
    echo "network guard: could not find curl on PATH" >&2
    return 1
  fi

  local guard_dir
  guard_dir="$(mktemp -d)"
  export YAI_NETWORK_GUARD_DIR="$guard_dir"

  YAI_NETWORK_VIOLATIONS="$guard_dir/violations.log"
  export YAI_NETWORK_VIOLATIONS
  : > "$YAI_NETWORK_VIOLATIONS"

  {
    printf '%s\n' '#!/usr/bin/env bash'
    printf '%s\n' '# YAI network guard: refuse any request that would leave this machine.'
    printf 'real=%q\n' "$real_curl"
    cat <<'SH'
log="${YAI_NETWORK_VIOLATIONS:-}"

is_url_arg() {
  [[ "$1" != -* && "$1" =~ ^[a-zA-Z][a-zA-Z0-9+.-]*:// ]]
}

allowed() {
  case "$1" in
    file://*|/*|./*|../*) return 0 ;;
    http://127.0.0.1*|https://127.0.0.1*|http://localhost*|https://localhost*) return 0 ;;
    http://\[::1\]*|https://\[::1\]*) return 0 ;;
  esac
  return 1
}

for arg in "$@"; do
  if is_url_arg "$arg" && ! allowed "$arg"; then
    [[ -n "$log" ]] && printf '%s\n' "$arg" >> "$log"
    printf 'network guard: refused outbound request: %s\n' "$arg" >&2
    exit 7
  fi
done

exec "$real" "$@"
SH
  } > "$guard_dir/curl"
  chmod +x "$guard_dir/curl"
}

network_guard_path() {
  [[ -n "${YAI_NETWORK_GUARD_DIR:-}" ]] || network_guard_init || return 1
  printf '%s\n' "$YAI_NETWORK_GUARD_DIR"
}

# Fails the caller when any outbound request was refused.
network_guard_report() {
  local violations="${YAI_NETWORK_VIOLATIONS:-}"
  [[ -n "$violations" && -f "$violations" ]] || return 0
  if [[ -s "$violations" ]]; then
    {
      echo "network guard: this test reached the public internet:"
      sort -u "$violations" | sed 's/^/  /'
      echo "  Point the remote base at a local file with the matching"
      echo "  YAI_*_BASE env var so the suite stays hermetic."
    } >&2
    return 1
  fi
  return 0
}
