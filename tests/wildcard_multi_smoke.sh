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
grep -q "multiple matches run in sorted order" "$TMP_HOME/help.out"
grep -q "Side-effect commands list matches and confirm when N>1" "$TMP_HOME/help.out"
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

# Shell-expanded local AppImage / multi-file argv must hint to quote wildcards.
SHELL_GLOB_DIR="$TMP_HOME/shell-glob-cwd"
mkdir -p "$SHELL_GLOB_DIR"
: >"$SHELL_GLOB_DIR/packa.AppImage"
: >"$SHELL_GLOB_DIR/package-lock.json"
if (
  cd "$SHELL_GLOB_DIR"
  HOME="$TMP_HOME" "$ROOT/yai" remove packa.AppImage
) >"$TMP_HOME/rm-local.out" 2>"$TMP_HOME/rm-local.err"; then
  echo "remove packa.AppImage unexpectedly succeeded" >&2
  exit 1
fi
grep -Fq "quote wildcards like 'name*'" "$TMP_HOME/rm-local.err"
grep -Fq "installed package ids" "$TMP_HOME/rm-local.err"
! grep -q "package is not installed: packa.appimage" "$TMP_HOME/rm-local.err"
if (
  cd "$SHELL_GLOB_DIR"
  HOME="$TMP_HOME" "$ROOT/yai" remove packa.AppImage package-lock.json
) >"$TMP_HOME/rm-multi.out" 2>"$TMP_HOME/rm-multi.err"; then
  echo "remove with multiple shell-expanded args unexpectedly succeeded" >&2
  exit 1
fi
grep -Fq "quote wildcards like 'name*'" "$TMP_HOME/rm-multi.err"
! grep -q "unknown remove option: package-lock.json" "$TMP_HOME/rm-multi.err"

# --yes removes both (sorted: multi-a then multi-b)
HOME="$TMP_HOME" "$ROOT/yai" remove 'multi-*' --yes | tee "$TMP_HOME/rm-yes.out" >/dev/null
grep -q "Removed multi-a" "$TMP_HOME/rm-yes.out"
grep -q "Removed multi-b" "$TMP_HOME/rm-yes.out"
test ! -d "$TMP_HOME/.local/share/yai/apps/multi-a"
test ! -d "$TMP_HOME/.local/share/yai/apps/multi-b"

# Multi-match install/download globs expand repo ids before dispatch.
ASSET_DIR="$TMP_HOME/assets"
INDEX="$TMP_HOME/multi-index.json"
DOWNLOAD_DIR="$TMP_HOME/downloads"
mkdir -p "$ASSET_DIR" "$DOWNLOAD_DIR"
for asset in PackA DownA DownB; do
  cat >"$ASSET_DIR/$asset.AppImage" <<APP
#!/usr/bin/env bash
if [[ "\${1:-}" == "--appimage-version" ]]; then
  echo "$asset 1.0.0"
  exit 0
fi
echo "$asset app"
APP
  chmod +x "$ASSET_DIR/$asset.AppImage"
done
cat >"$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-25T00:00:00Z",
  "packages": [
    {
      "id": "pack-a",
      "name": "Pack A",
      "summary": "Installable first package",
      "homepage": "https://example.com/pack-a",
      "license": "Unknown",
      "source": {"type": "direct_url", "url": "file://$ASSET_DIR/PackA.AppImage"}
    },
    {
      "id": "pack-b",
      "name": "Pack B",
      "summary": "Unavailable second package",
      "homepage": "https://example.com/pack-b",
      "license": "Unknown",
      "source": {"type": "unavailable", "reason": "mid-batch failure fixture"}
    },
    {
      "id": "down-a",
      "name": "Down A",
      "summary": "First downloadable package",
      "homepage": "https://example.com/down-a",
      "license": "Unknown",
      "source": {"type": "direct_url", "url": "file://$ASSET_DIR/DownA.AppImage"}
    },
    {
      "id": "down-b",
      "name": "Down B",
      "summary": "Second downloadable package",
      "homepage": "https://example.com/down-b",
      "license": "Unknown",
      "source": {"type": "direct_url", "url": "file://$ASSET_DIR/DownB.AppImage"}
    }
  ]
}
JSON

printf 'n\n' | HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" \
  "$ROOT/yai" install 'pack-*' >"$TMP_HOME/install-cancel.out" 2>"$TMP_HOME/install-cancel.err"
grep -Fq "Install 2 package(s)? [y/N] " "$TMP_HOME/install-cancel.err"
grep -q "Install cancelled" "$TMP_HOME/install-cancel.out"
test ! -e "$TMP_HOME/.local/share/yai/apps/pack-a"

if HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" \
  "$ROOT/yai" install 'pack-*' --yes >"$TMP_HOME/install-multi.out" 2>"$TMP_HOME/install-multi.err"; then
  echo "expected multi install to fail on unavailable pack-b" >&2
  exit 1
fi
test -x "$TMP_HOME/.local/share/yai/apps/pack-a/current.AppImage"
test ! -e "$TMP_HOME/.local/share/yai/apps/pack-b"

(
  cd "$DOWNLOAD_DIR"
  HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" download 'down-*' -y
)
test -f "$DOWNLOAD_DIR/down-a.AppImage"
test -f "$DOWNLOAD_DIR/down-b.AppImage"

# Unique-match repo globs must stay on the single-target path (no multi confirm).
printf 'n\n' | HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" \
  "$ROOT/yai" install 'pack-a*' >"$TMP_HOME/install-unique.out" 2>"$TMP_HOME/install-unique.err" || true
! grep -Fq "Install 1 package(s)? [y/N] " "$TMP_HOME/install-unique.err"
! grep -Fq "Install 2 package(s)? [y/N] " "$TMP_HOME/install-unique.err"
! grep -q "yai: running " "$TMP_HOME/install-unique.err"

# URL query strings (?...) are not repo-id globs.
QUERY_URL="file://$ASSET_DIR/PackA.AppImage?token=1"
set +e
HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" \
  "$ROOT/yai" download "$QUERY_URL" -y \
  >"$TMP_HOME/url-query.out" 2>"$TMP_HOME/url-query.err"
query_rc=$?
set -e
! grep -q "package pattern matched no repo packages" "$TMP_HOME/url-query.err"
if [[ "$query_rc" -ne 0 ]]; then
  # Local file:// may reject ?query on open; any non-glob failure is acceptable.
  ! grep -q "package pattern matched no repo packages" "$TMP_HOME/url-query.out"
fi

echo "wildcard multi smoke passed"
