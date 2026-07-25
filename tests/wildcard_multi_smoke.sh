#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
trap 'rm -rf "$TMP_HOME"' EXIT
export YAI_LANG=en
make -C "$ROOT" yai >/dev/null

# Lifecycle commands must reject options in the required pattern position.
for command in remove repair rollback; do
  if HOME="$TMP_HOME" "$ROOT/yai" "$command" --bogus >"$TMP_HOME/$command-bogus.out" 2>"$TMP_HOME/$command-bogus.err"; then
    echo "$command --bogus unexpectedly succeeded" >&2
    exit 1
  fi
  grep -q "unknown $command option: --bogus" "$TMP_HOME/$command-bogus.err"
done

# Help must no longer claim that wildcard arguments require exactly one match.
HOME="$TMP_HOME" "$ROOT/yai" help >"$TMP_HOME/help.out"
grep -q "supported commands may apply to multiple matches" "$TMP_HOME/help.out"
! grep -q "patterns when they match exactly one package" "$TMP_HOME/help.out"

# Minimal: two installed stub apps via metadata dirs for remove multi-match
for id in multi-a multi-b; do
  mkdir -p "$TMP_HOME/.local/share/yai/apps/$id"
  cat >"$TMP_HOME/.local/share/yai/apps/$id/metadata.json" <<JSON
{"id":"$id","name":"$id","install_mode":"extract","version":"1","source_kind":"unavailable"}
JSON
  : >"$TMP_HOME/.local/share/yai/apps/$id/current.AppImage"
  mkdir -p "$TMP_HOME/.local/bin" "$TMP_HOME/.local/share/applications"
  : >"$TMP_HOME/.local/bin/$id"
  : >"$TMP_HOME/.local/share/applications/yai-$id.desktop"
done

# rollback uses the planned two-word prompt before touching any package.
printf 'n\n' | HOME="$TMP_HOME" "$ROOT/yai" rollback 'multi-*' >"$TMP_HOME/rollback-cancel.out" 2>"$TMP_HOME/rollback-cancel.err"
grep -Fq "Roll back 2 package(s)? [y/N] " "$TMP_HOME/rollback-cancel.err"

# cancel remove
printf 'n\n' | HOME="$TMP_HOME" "$ROOT/yai" remove 'multi-*' >"$TMP_HOME/rm-cancel.out" 2>"$TMP_HOME/rm-cancel.err" || true
test -d "$TMP_HOME/.local/share/yai/apps/multi-a"
test -d "$TMP_HOME/.local/share/yai/apps/multi-b"

# --yes removes both (sorted: multi-a then multi-b)
HOME="$TMP_HOME" "$ROOT/yai" remove 'multi-*' --yes | tee "$TMP_HOME/rm-yes.out" >/dev/null
grep -q "Removed multi-a" "$TMP_HOME/rm-yes.out"
grep -q "Removed multi-b" "$TMP_HOME/rm-yes.out"
test ! -d "$TMP_HOME/.local/share/yai/apps/multi-a"
test ! -d "$TMP_HOME/.local/share/yai/apps/multi-b"

# Mid-batch failure coverage for install globs is in Task 7 (pack-a then unavailable pack-b).

echo "wildcard multi smoke passed"
