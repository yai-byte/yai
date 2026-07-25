#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
API_ROOT="$TMP_HOME/api"
ORIGINAL_ROOT="$TMP_HOME/original"
INDEX="$TMP_HOME/index.json"
ASSET="RepoDemo-x86_64.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

mkdir -p "$API_ROOT/repos/acme/repo-demo/releases" "$ORIGINAL_ROOT"

cat > "$ORIGINAL_ROOT/$ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "repo demo 4.0.0"
  exit 0
fi
echo "repo demo app"
APP
chmod +x "$ORIGINAL_ROOT/$ASSET"

cat > "$API_ROOT/repos/acme/repo-demo/releases/latest" <<JSON
{
  "tag_name": "v4.0.0",
  "assets": [
    {
      "name": "RepoDemo-arm64.AppImage",
      "browser_download_url": "file://$ORIGINAL_ROOT/RepoDemo-arm64.AppImage"
    },
    {
      "name": "$ASSET",
      "browser_download_url": "file://$ORIGINAL_ROOT/$ASSET"
    }
  ]
}
JSON

cat > "$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "repo-demo",
      "name": "Repository Demo",
      "summary": "Stage four indexed AppImage. This extra sentence should stay visible only in info output.",
      "homepage": "https://example.com/repo-demo",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "repo-demo",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    },
    {
      "id": "repo-delta",
      "name": "Repository Delta",
      "summary": "Second package used for wildcard ambiguity checks",
      "homepage": "https://example.com/repo-delta",
      "license": "Unknown",
      "source": {
        "type": "unavailable",
        "reason": "wildcard ambiguity fixture"
      }
    }
  ]
}
JSON

SEARCH_OUT="$(HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" search indexed)"
printf '%s\n' "$SEARCH_OUT" | grep -q "repo-demo"
if [[ "$SEARCH_OUT" == *"only in info output"* ]]; then
  echo "search printed an overlong summary" >&2
  exit 1
fi
HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" search 'repo-d*' | grep -q "repo-delta"
HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" info 'repo-dem*' | grep -q "Source: github_release acme/repo-demo"
HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" info 'repo-dem*' | grep -q "only in info output"
if HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" info 'repo-d*' 2>"$TMP_HOME/wildcard.err"; then
  echo "ambiguous repo wildcard unexpectedly succeeded" >&2
  exit 1
fi
grep -q "package pattern is ambiguous: repo-d\\*" "$TMP_HOME/wildcard.err"

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install 'repo-dem*'

META="$TMP_HOME/.local/share/yai/apps/repo-demo/metadata.json"
grep -q '"source_kind": "repo_github_release"' "$META"
grep -q '"name": "Repository Demo"' "$META"
grep -q '"version": "v4.0.0"' "$META"
grep -q '"github_owner": "acme"' "$META"
grep -q '"github_repo": "repo-demo"' "$META"
grep -Fq "\"github_asset\": \"$ASSET\"" "$META"
grep -Fq "\"source_url\": \"file://$ORIGINAL_ROOT/$ASSET\"" "$META"

HOME="$TMP_HOME" "$TMP_HOME/.local/bin/repo-demo" | grep -q "repo demo app"
HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "repo-demo"
HOME="$TMP_HOME" "$ROOT/yai" remove 'repo-*'

echo "stage4 smoke test passed"
