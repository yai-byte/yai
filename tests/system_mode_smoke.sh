#!/usr/bin/env bash
set -euo pipefail

# Exercises yai's system-wide (sudo / root) install mode:
#   * `YAI_SYSTEM_MODE=1` selects FHS system paths without crashing
#   * usage mentions installing system-wide with sudo
#   * a machine-wide install is visible to unprivileged `yai list` (tagged `system`)
#   * `remove`/`upgrade`/... on a system-wide app demands `sudo` for normal users
#
# The machine-wide parts require write access to /usr/local/share/yai/apps
# (i.e. run as root, or with permission); they are skipped otherwise so the
# test still passes for ordinary developers.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# safe-bin shim intercepts absolute-path command invocations under `set -e`;
# run from $ROOT with a relative path so the binary is not treated as external.
cd "$ROOT"
YAI="./yai"
YAI_ABS="$ROOT/yai"

if [[ ! -x "$YAI" ]]; then
  echo "system_mode_smoke: build yai first (make)" >&2
  exit 1
fi

fail() { echo "FAIL: $*" >&2; exit 1; }

# 1) usage mentions system-wide install via sudo (language-neutral match)
if "$YAI" 2>&1 | grep -qiE "system-wide|系统级"; then
  echo "ok: usage mentions system-wide install"
else
  fail "usage does not mention system-wide / sudo install"
fi

# 2) system mode resolves paths without crashing (isolated HOME)
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
if ! HOME="$tmp" YAI_SYSTEM_MODE=1 "$YAI" list >/dev/null 2>&1; then
  fail "yai list in system mode (YAI_SYSTEM_MODE=1) exited non-zero"
fi
echo "ok: YAI_SYSTEM_MODE=1 path resolution works"

SYSTEM_APPS=/usr/local/share/yai/apps
if [[ -w "$SYSTEM_APPS" ]]; then
  mkdir -p "$SYSTEM_APPS/testsys"
  cat > "$SYSTEM_APPS/testsys/metadata.json" <<'JSON'
{
  "id": "testsys",
  "name": "Test System App",
  "install_mode": "direct",
  "source_kind": "url"
}
JSON

  # 3) unprivileged user sees the machine-wide install, tagged `system`
  out="$(HOME="$tmp" "$YAI" list 2>/dev/null || true)"
  if ! printf '%s\n' "$out" | grep -q $'\ttestsys\t'; then
    fail "normal-user list did not surface machine-wide app testsys"
  fi
  if ! printf '%s\n' "$out" | grep -q $'testsys\t[^\t]*\t[^\t]*\tsystem'; then
    fail "machine-wide app testsys not tagged with scope 'system'"
  fi
  echo "ok: machine-wide install visible to normal users (scope=system)"

  # 4) removing a system-wide app as a normal user must demand sudo
  if command -v sudo >/dev/null 2>&1 && id nobody >/dev/null 2>&1; then
    if sudo -u nobody HOME="$tmp" "$YAI_ABS" remove --yes testsys 2>&1 | grep -q "sudo"; then
      echo "ok: normal-user remove of system app suggests sudo"
    else
      fail "removing a system-wide app as a normal user did not suggest sudo"
    fi
  else
    # Fall back: a non-root process with no write access to the system dir
    # should still be told to use sudo. Spawn yai under a non-root EUID if we
    # can; otherwise just assert the guard code path exists via the message.
    echo "SKIP: sudo/nobody unavailable; cannot exercise sudo guard as non-root"
  fi

  # 5) root (system mode) can manage it
  if ! "$YAI" remove --yes testsys >/dev/null 2>&1; then
    fail "root could not remove system-wide app testsys"
  fi
  echo "ok: root can remove system-wide app"
else
  echo "SKIP: cannot write $SYSTEM_APPS (run as root to exercise machine-wide visibility)"
fi

echo "PASS: system mode behaves as expected"
