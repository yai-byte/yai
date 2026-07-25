#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
trap 'rm -rf "$TMP_HOME"' EXIT
export YAI_LANG=en
make -C "$ROOT" yai >/dev/null

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
