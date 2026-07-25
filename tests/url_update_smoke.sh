#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
ORIGINAL_ROOT="$TMP_HOME/original"
INDEX="$TMP_HOME/index.json"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

write_appimage() {
  local path="$1"
  local label="$2"
  cat > "$path" <<APP
#!/usr/bin/env bash
if [[ "\${1:-}" == "--appimage-version" ]]; then
  echo "$label"
  exit 0
fi
echo "$label app"
APP
  chmod +x "$path"
}

mkdir -p "$ORIGINAL_ROOT"

# --- plain URL install: unchanged then size-changed ---
ASSET="$ORIGINAL_ROOT/UrlUpdatable-x86_64.AppImage"
write_appimage "$ASSET" "url-v1"

HOME="$TMP_HOME" "$ROOT/yai" install "file://$ASSET" --id url-updatable

URL_META="$TMP_HOME/.local/share/yai/apps/url-updatable/metadata.json"
grep -q '"source_kind": "url"' "$URL_META"

# Prefer Task 3 capture; seed content_length if empty so Unchanged is testable.
if ! grep -qE '"http_content_length": "[1-9][0-9]*"' "$URL_META"; then
  SIZE="$(wc -c < "$ASSET" | tr -d ' ')"
  python3 - "$URL_META" "$SIZE" <<'PY'
import json, sys
path, size = sys.argv[1], sys.argv[2]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
data["http_content_length"] = size
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2)
    f.write("\n")
PY
fi

HOME="$TMP_HOME" "$ROOT/yai" update url-updatable > "$TMP_HOME/url_current.out"
grep -q $'url-updatable\t' "$TMP_HOME/url_current.out"
grep -q $'\tcurrent\t' "$TMP_HOME/url_current.out"
grep -q 'already up to date' "$TMP_HOME/url_current.out"

printf '\n# appended for freshness\n' >> "$ASSET"

HOME="$TMP_HOME" "$ROOT/yai" update url-updatable > "$TMP_HOME/url_changed.out"
grep -q $'\tupgradable\t' "$TMP_HOME/url_changed.out"
if grep -q 'remote content changed' "$TMP_HOME/url_changed.out"; then
  :
elif grep -q 'download verification required' "$TMP_HOME/url_changed.out"; then
  :
else
  echo "expected remote content changed or download verification required" >&2
  cat "$TMP_HOME/url_changed.out" >&2
  exit 1
fi

# --- repo_direct_url same URL: same-size rewrite without usable ETag → Unknown ---
SAME="$ORIGINAL_ROOT/SameUrlDirect-x86_64.AppImage"
write_appimage "$SAME" "direct-v1"
cat > "$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "same-url-direct",
      "name": "Same URL Direct",
      "summary": "Same-URL freshness demo",
      "homepage": "https://example.com/same-url-direct",
      "license": "Unknown",
      "source": {
        "type": "direct_url",
        "url": "file://$SAME"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" install same-url-direct

REPO_META="$TMP_HOME/.local/share/yai/apps/same-url-direct/metadata.json"
grep -q '"source_kind": "repo_direct_url"' "$REPO_META"

# Same-length rewrite (v1 / v2 labels are equal width) so length alone cannot
# prove a change; clear validators to force Unknown without ETag.
write_appimage "$SAME" "direct-v2"
python3 - "$REPO_META" <<'PY'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
data["http_etag"] = ""
data["http_last_modified"] = ""
data["http_content_length"] = ""
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2)
    f.write("\n")
PY

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" update same-url-direct > "$TMP_HOME/repo_same.out"

# Must not claim current solely because basename/version identity matched.
if grep -q $'\tcurrent\t' "$TMP_HOME/repo_same.out"; then
  echo "repo same-URL same-size change must not report current" >&2
  cat "$TMP_HOME/repo_same.out" >&2
  exit 1
fi
grep -q $'\tupgradable\t' "$TMP_HOME/repo_same.out"
grep -q 'download verification required' "$TMP_HOME/repo_same.out"

# --- Task 5: upgrade execution (gated until Task 5) ---
# HOME="$TMP_HOME" "$ROOT/yai" upgrade url-updatable
# HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" "$ROOT/yai" upgrade same-url-direct

echo "url_update smoke (preview) passed"
