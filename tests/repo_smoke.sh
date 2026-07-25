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

# --- repo remove ---
cat > "$TMP_HOME/remove-source.json" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-25T00:00:00Z",
  "packages": [
    {
      "id": "remove-me",
      "name": "Remove Me",
      "summary": "Source for remove smoke",
      "homepage": "https://example.com/remove-me",
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

HOME="$TMP_HOME" "$ROOT/yai" repo add keepme "$SOURCE_INDEX"
HOME="$TMP_HOME" "$ROOT/yai" repo add dropme "$TMP_HOME/remove-source.json"
HOME="$TMP_HOME" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install remove-me
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/remove-me" | grep -q "repo managed app"

HOME="$TMP_HOME" "$ROOT/yai" repo remove dropme | grep -q "Removed repo dropme"
if HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q $'dropme\t'; then
  echo "expected dropme to be gone from repo list" >&2
  exit 1
fi
test ! -e "$TMP_HOME/.local/share/yai/repos/dropme.json"
if HOME="$TMP_HOME" "$ROOT/yai" search "Remove Me" | grep -q "remove-me"; then
  echo "expected remove-me package to leave merged index after source remove" >&2
  exit 1
fi
HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q $'keepme\t'
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/remove-me" | grep -q "repo managed app"
HOME="$TMP_HOME" "$ROOT/yai" list | grep -q "remove-me"

if HOME="$TMP_HOME" "$ROOT/yai" repo remove nosuch 2>"$TMP_HOME/remove-missing.err"; then
  echo "expected missing repo remove to fail" >&2
  exit 1
fi
grep -q "repo not configured: nosuch" "$TMP_HOME/remove-missing.err"

HOME="$TMP_HOME" "$ROOT/yai" repo add alpha "$TMP_HOME/remove-source.json"
HOME="$TMP_HOME" "$ROOT/yai" repo add abelian "$SOURCE_INDEX"
# decline confirm: no change
printf 'n\n' | HOME="$TMP_HOME" "$ROOT/yai" repo remove 'a*' >"$TMP_HOME/remove-cancel.out" 2>"$TMP_HOME/remove-cancel.err" || true
grep -q "alpha" <<<"$(HOME="$TMP_HOME" "$ROOT/yai" repo list)"
grep -q "abelian" <<<"$(HOME="$TMP_HOME" "$ROOT/yai" repo list)"
HOME="$TMP_HOME" "$ROOT/yai" repo remove 'a*' --yes | tee "$TMP_HOME/remove-multi.out" >/dev/null
grep -q "Removed repo alpha" "$TMP_HOME/remove-multi.out"
grep -q "Removed repo abelian" "$TMP_HOME/remove-multi.out"
if HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -Eq $'^(alpha|abelian)\t'; then
  echo "expected alpha and abelian removed" >&2
  exit 1
fi

# dropme already removed; pattern should fail with no match
if HOME="$TMP_HOME" "$ROOT/yai" repo remove 'drop*' 2>"$TMP_HOME/remove-nomatch.err"; then
  echo "expected no-match repo remove to fail" >&2
  exit 1
fi
grep -q "repo pattern matched no configured repos:" "$TMP_HOME/remove-nomatch.err"

HOME="$TMP_HOME" "$ROOT/yai" repo add alpha "$TMP_HOME/remove-source.json"
HOME="$TMP_HOME" "$ROOT/yai" repo remove 'alp*' | grep -q "Removed repo alpha"
if HOME="$TMP_HOME" "$ROOT/yai" repo list | grep -q $'alpha\t'; then
  echo "expected alpha to be removed via glob" >&2
  exit 1
fi

echo "repo smoke test passed"
