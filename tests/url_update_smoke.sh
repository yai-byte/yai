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

HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" update url-updatable > "$TMP_HOME/url_current.out"
grep -q $'url-updatable\t' "$TMP_HOME/url_current.out"
grep -q $'\tcurrent\t' "$TMP_HOME/url_current.out"
grep -q 'already up to date' "$TMP_HOME/url_current.out"

printf '\n# appended for freshness\n' >> "$ASSET"

HOME="$TMP_HOME" YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" update url-updatable > "$TMP_HOME/url_changed.out"
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

# --- repo_direct_url: index URL change with same basename → upgradable ---
# Install from path A; repoint index to path B with the same filename so
# version identity matches but source_url differs (no size change needed).
URL_CHANGE_DIR_A="$ORIGINAL_ROOT/url-change-a"
URL_CHANGE_DIR_B="$ORIGINAL_ROOT/url-change-b"
mkdir -p "$URL_CHANGE_DIR_A" "$URL_CHANGE_DIR_B"
URL_CHANGE_ASSET_A="$URL_CHANGE_DIR_A/SameName-x86_64.AppImage"
URL_CHANGE_ASSET_B="$URL_CHANGE_DIR_B/SameName-x86_64.AppImage"
write_appimage "$URL_CHANGE_ASSET_A" "urlchg-v1"
cp -a "$URL_CHANGE_ASSET_A" "$URL_CHANGE_ASSET_B"

cat > "$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "url-change-direct",
      "name": "URL Change Direct",
      "summary": "Index URL change with same basename",
      "homepage": "https://example.com/url-change-direct",
      "license": "Unknown",
      "source": {
        "type": "direct_url",
        "url": "file://$URL_CHANGE_ASSET_A"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" install url-change-direct

URL_CHANGE_META="$TMP_HOME/.local/share/yai/apps/url-change-direct/metadata.json"
grep -q '"source_kind": "repo_direct_url"' "$URL_CHANGE_META"
grep -Fq "\"source_url\": \"file://$URL_CHANGE_ASSET_A\"" "$URL_CHANGE_META"

cat > "$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-20T00:00:00Z",
  "packages": [
    {
      "id": "url-change-direct",
      "name": "URL Change Direct",
      "summary": "Index URL change with same basename",
      "homepage": "https://example.com/url-change-direct",
      "license": "Unknown",
      "source": {
        "type": "direct_url",
        "url": "file://$URL_CHANGE_ASSET_B"
      }
    }
  ]
}
JSON

HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" update url-change-direct > "$TMP_HOME/repo_url_change.out"

grep -q $'\tupgradable\t' "$TMP_HOME/repo_url_change.out"
grep -Fq "file://$URL_CHANGE_ASSET_B" "$TMP_HOME/repo_url_change.out"
if grep -q $'\tcurrent\t' "$TMP_HOME/repo_url_change.out"; then
  echo "index URL change with same basename must not report current" >&2
  cat "$TMP_HOME/repo_url_change.out" >&2
  exit 1
fi

# --- Task 5: upgrade execution ---
# Case 1: URL install already grown above; upgrade applies new bytes.
# Keep the grown file (do not rewrite to an equal-length label — that would
# restore the stored Content-Length and hide the change from the probe).
URL_OLD_SHA="$(python3 - "$URL_META" <<'PY'
import json, sys
print(json.load(open(sys.argv[1], encoding="utf-8"))["sha256"])
PY
)"
test -n "$URL_OLD_SHA"
test ! -e "$TMP_HOME/.local/share/yai/apps/url-updatable/versions/previous"

HOME="$TMP_HOME" "$ROOT/yai" upgrade --yes url-updatable > "$TMP_HOME/url_upgrade.out"
grep -q 'Upgraded url-updatable' "$TMP_HOME/url_upgrade.out"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/url-updatable" | grep -q "url-v1 app"
grep -q 'appended for freshness' \
  "$TMP_HOME/.local/share/yai/apps/url-updatable/current.AppImage"

URL_NEW_SHA="$(python3 - "$URL_META" <<'PY'
import json, sys
print(json.load(open(sys.argv[1], encoding="utf-8"))["sha256"])
PY
)"
test "$URL_OLD_SHA" != "$URL_NEW_SHA"
ASSET_SHA="$(sha256sum "$ASSET" | awk '{print $1}')"
test "$URL_NEW_SHA" = "$ASSET_SHA"

# http_* refreshed when download captured validators (file:// → content_length).
python3 - "$URL_META" "$ASSET" <<'PY'
import json, os, sys
meta_path, asset = sys.argv[1], sys.argv[2]
with open(meta_path, encoding="utf-8") as f:
    data = json.load(f)
cl = data.get("http_content_length", "")
if cl:
    size = str(os.path.getsize(asset))
    if cl != size:
        raise SystemExit(f"http_content_length {cl!r} != asset size {size!r}")
PY

test -e "$TMP_HOME/.local/share/yai/apps/url-updatable/versions/previous"

# Case 2: no remote change → already up to date; Unchanged path does not require previous.
HOME="$TMP_HOME" "$ROOT/yai" upgrade url-updatable > "$TMP_HOME/url_upgrade_noop.out"
grep -q 'already up to date' "$TMP_HOME/url_upgrade_noop.out"
if grep -q 'Upgraded url-updatable' "$TMP_HOME/url_upgrade_noop.out"; then
  echo "noop upgrade must not apply a new version" >&2
  cat "$TMP_HOME/url_upgrade_noop.out" >&2
  exit 1
fi
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/url-updatable" | grep -q "url-v1 app"

# Case 3: repo_direct_url same URL; content already rewritten to direct-v2 above.
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
"$ROOT/yai" upgrade --yes same-url-direct > "$TMP_HOME/repo_upgrade.out"
grep -q 'Upgraded same-url-direct' "$TMP_HOME/repo_upgrade.out"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/same-url-direct" | grep -q "direct-v2 app"

echo "url_update smoke (preview + upgrade) passed"
