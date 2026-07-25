#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
API_ROOT="$TMP_HOME/api"
ORIGINAL_ROOT="$TMP_HOME/original"
SOURCE_INDEX="$TMP_HOME/source-index.json"
LATEST="$API_ROOT/repos/acme/repo-managed/releases/latest"
ASSET="RepoManaged-x86_64.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

mkdir -p "$(dirname "$LATEST")" "$ORIGINAL_ROOT"

cat > "$ORIGINAL_ROOT/$ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "repo managed 1.0.0"
  exit 0
fi
echo "repo managed app"
APP
chmod +x "$ORIGINAL_ROOT/$ASSET"

cat > "$LATEST" <<JSON
{
  "tag_name": "v1.0.0",
  "assets": [
    {
      "name": "$ASSET",
      "browser_download_url": "file://$ORIGINAL_ROOT/$ASSET"
    }
  ]
}
JSON

cat > "$SOURCE_INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "repo-managed",
      "name": "Repo Managed",
      "summary": "Managed by repo add",
      "homepage": "https://example.com/repo-managed",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "repo-managed",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" "$ROOT/yai" repo add main "$SOURCE_INDEX"
HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q $'main\t'
HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q "cached"
HOME="$TMP_HOME" "$ROOT/yai" search managed | grep -q "repo-managed"
HOME="$TMP_HOME" "$ROOT/yai" info repo-managed | grep -q "Repo Managed"

HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install repo-managed

HOME="$TMP_HOME" "$TMP_HOME/.local/bin/repo-managed" | grep -q "repo managed app"
HOME="$TMP_HOME" "$ROOT/yai" remove repo-managed

cat > "$SOURCE_INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-21T00:00:00Z",
  "packages": [
    {
      "id": "repo-managed",
      "name": "Repo Managed",
      "summary": "Managed by repo add",
      "homepage": "https://example.com/repo-managed",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "repo-managed",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    },
    {
      "id": "repo-second",
      "name": "Repo Second",
      "summary": "Added by repo update",
      "homepage": "https://example.com/repo-second",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "repo-managed",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" "$ROOT/yai" repo update main | grep -q "Updated 1 repo"
HOME="$TMP_HOME" "$ROOT/yai" search second | grep -q "repo-second"
if HOME="$TMP_HOME" "$ROOT/yai" repo update missing 2>"$TMP_HOME/missing.err"; then
  echo "expected missing repo update to fail" >&2
  exit 1
fi
grep -q "repo not configured: missing" "$TMP_HOME/missing.err"

echo "repo smoke test passed"
