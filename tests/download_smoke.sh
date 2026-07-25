#!/usr/bin/env bash
set -euo pipefail
export YAI_LANG=en

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
API_ROOT="$TMP_HOME/api"
ORIGINAL_ROOT="$TMP_HOME/original"
DOWNLOAD_DIR="$TMP_HOME/downloads"
PARALLEL_DOWNLOAD_DIR="$TMP_HOME/parallel-downloads"
PARALLEL_FAILURE_DIR="$TMP_HOME/parallel-failure"
WILDCARD_DOWNLOAD_DIR="$TMP_HOME/wildcard-downloads"
AUTO_DOWNLOAD_DIR="$TMP_HOME/auto-download"
AUTO_INSTALL_ASSET="AutoInstall-x86_64.AppImage"
AUTO_DOWNLOAD_ASSET="AutoDownload-x86_64.AppImage"
FAKE_BIN="$TMP_HOME/fake-bin"
LATEST="$API_ROOT/repos/acme/download-demo/releases/latest"
PARALLEL_ONE_LATEST="$API_ROOT/repos/acme/parallel-one/releases/latest"
PARALLEL_TWO_LATEST="$API_ROOT/repos/acme/parallel-two/releases/latest"
INDEX="$TMP_HOME/index.json"
ASSET="DownloadDemo-x86_64.AppImage"
PARALLEL_ONE_ASSET="ParallelOne-x86_64.AppImage"
PARALLEL_TWO_ASSET="ParallelTwo-x86_64.AppImage"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

mkdir -p "$(dirname "$LATEST")" "$(dirname "$PARALLEL_ONE_LATEST")" "$(dirname "$PARALLEL_TWO_LATEST")" \
  "$ORIGINAL_ROOT" "$DOWNLOAD_DIR" "$PARALLEL_DOWNLOAD_DIR" "$PARALLEL_FAILURE_DIR" "$WILDCARD_DOWNLOAD_DIR" \
  "$AUTO_DOWNLOAD_DIR" "$FAKE_BIN"

cat > "$FAKE_BIN/aria2c" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
dir=""
out=""
url=""
file_allocation=""
while (($#)); do
  case "$1" in
    --dir)
      dir="$2"
      shift 2
      ;;
    --out)
      out="$2"
      shift 2
      ;;
    --file-allocation=*)
      file_allocation="${1#*=}"
      shift
      ;;
    --*)
      shift
      ;;
    *)
      url="$1"
      shift
      ;;
  esac
done
if [[ -z "$dir" || -z "$out" ]]; then
  echo "missing aria2 output arguments" >&2
  exit 2
fi
printf 'aria2c\t%s\tfile-allocation=%s\n' "$url" "$file_allocation" >> "${FAKE_DOWNLOADER_LOG:?}"
mkdir -p "$dir"
{
  echo '#!/usr/bin/env bash'
  echo 'if [[ "${1:-}" == "--appimage-version" ]]; then'
  echo '  echo "fake downloader 1.0.0"'
  echo '  exit 0'
  echo 'fi'
  echo 'echo "fake downloader app"'
} > "$dir/$out"
SH
chmod +x "$FAKE_BIN/aria2c"

cat > "$FAKE_BIN/curl" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
headers=""
url=""
while (($#)); do
  case "$1" in
    --dump-header)
      headers="$2"
      shift 2
      ;;
    --output|max-time)
      shift 2
      ;;
    --*)
      shift
      ;;
    *)
      url="$1"
      shift
      ;;
  esac
done
if [[ -z "$headers" || -z "$url" ]]; then
  echo "missing curl header arguments" >&2
  exit 2
fi
printf 'curl-head\t%s\n' "$url" >> "${FAKE_DOWNLOADER_LOG:?}"
{
  echo 'HTTP/2 200'
  echo 'Content-Length: 128'
  echo
} > "$headers"
SH
chmod +x "$FAKE_BIN/curl"

cat > "$ORIGINAL_ROOT/$ASSET" <<'APP'
#!/usr/bin/env bash
if [[ "${1:-}" == "--appimage-version" ]]; then
  echo "download demo 1.0.0"
  exit 0
fi
echo "download demo app"
APP
chmod +x "$ORIGINAL_ROOT/$ASSET"

cat > "$ORIGINAL_ROOT/$PARALLEL_ONE_ASSET" <<'APP'
#!/usr/bin/env bash
echo "parallel one app"
APP
chmod +x "$ORIGINAL_ROOT/$PARALLEL_ONE_ASSET"

cat > "$ORIGINAL_ROOT/$PARALLEL_TWO_ASSET" <<'APP'
#!/usr/bin/env bash
echo "parallel two app"
APP
chmod +x "$ORIGINAL_ROOT/$PARALLEL_TWO_ASSET"

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

cat > "$PARALLEL_ONE_LATEST" <<JSON
{
  "tag_name": "v1.0.0",
  "assets": [
    {
      "name": "$PARALLEL_ONE_ASSET",
      "browser_download_url": "file://$ORIGINAL_ROOT/$PARALLEL_ONE_ASSET"
    }
  ]
}
JSON

cat > "$PARALLEL_TWO_LATEST" <<JSON
{
  "tag_name": "v1.0.0",
  "assets": [
    {
      "name": "$PARALLEL_TWO_ASSET",
      "browser_download_url": "file://$ORIGINAL_ROOT/$PARALLEL_TWO_ASSET"
    }
  ]
}
JSON

cat > "$INDEX" <<JSON
{
  "schema_version": 1,
  "updated_at": "2026-07-23T00:00:00Z",
  "packages": [
    {
      "id": "download-demo",
      "name": "Download Demo",
      "summary": "Download-only wildcard package",
      "homepage": "https://example.com/download-demo",
      "license": "Unknown",
      "source": {
        "type": "github_release",
        "owner": "acme",
        "repo": "download-demo",
        "asset_pattern": ".*x86_64.*\\.AppImage$"
      }
    }
  ]
}
JSON

(
  cd "$DOWNLOAD_DIR"
  HOME="$TMP_HOME" YAI_GITHUB_API_BASE="file://$API_ROOT" "$ROOT/yai" download acme/download-demo
)

DOWNLOADED="$DOWNLOAD_DIR/download-demo.AppImage"
test -f "$DOWNLOADED"
grep -q "download demo app" "$DOWNLOADED"
test ! -x "$DOWNLOADED"
test ! -e "$TMP_HOME/.local/share/yai/apps/download-demo"
test ! -e "$TMP_HOME/.local/bin/download-demo"
test ! -e "$TMP_HOME/.local/share/applications/yai-download-demo.desktop"

(
  cd "$PARALLEL_DOWNLOAD_DIR"
  HOME="$TMP_HOME" YAI_GITHUB_API_BASE="file://$API_ROOT" \
    "$ROOT/yai" download acme/parallel-one acme/parallel-two --jobs 2
)

test -f "$PARALLEL_DOWNLOAD_DIR/parallel-one.AppImage"
test -f "$PARALLEL_DOWNLOAD_DIR/parallel-two.AppImage"
grep -q "parallel one app" "$PARALLEL_DOWNLOAD_DIR/parallel-one.AppImage"
grep -q "parallel two app" "$PARALLEL_DOWNLOAD_DIR/parallel-two.AppImage"
test ! -x "$PARALLEL_DOWNLOAD_DIR/parallel-one.AppImage"
test ! -x "$PARALLEL_DOWNLOAD_DIR/parallel-two.AppImage"
test ! -e "$TMP_HOME/.local/share/yai/apps/parallel-one"
test ! -e "$TMP_HOME/.local/share/yai/apps/parallel-two"

if (
  cd "$PARALLEL_FAILURE_DIR"
  HOME="$TMP_HOME" YAI_GITHUB_API_BASE="file://$API_ROOT" \
    "$ROOT/yai" download acme/parallel-one acme/missing --jobs 2 2>"$TMP_HOME/parallel-failure.err"
); then
  echo "parallel download succeeded despite a failed task" >&2
  exit 1
fi
grep -q "task failed: download acme/missing" "$TMP_HOME/parallel-failure.err"
grep -q "batch task(s) failed" "$TMP_HOME/parallel-failure.err"

if (
  cd "$DOWNLOAD_DIR"
  HOME="$TMP_HOME" YAI_GITHUB_API_BASE="file://$API_ROOT" "$ROOT/yai" download acme/download-demo 2>"$TMP_HOME/exists.err"
); then
  echo "download overwrote an existing file" >&2
  exit 1
fi
grep -q "download target already exists" "$TMP_HOME/exists.err"

(
  cd "$WILDCARD_DOWNLOAD_DIR"
  HOME="$TMP_HOME" \
  YAI_REPO_INDEX="$INDEX" \
  YAI_GITHUB_API_BASE="file://$API_ROOT" \
  "$ROOT/yai" download 'download-*'
)

test -f "$WILDCARD_DOWNLOAD_DIR/download-demo.AppImage"
test ! -x "$WILDCARD_DOWNLOAD_DIR/download-demo.AppImage"
test ! -e "$TMP_HOME/.local/share/yai/apps/download-demo"

(
  cd "$AUTO_DOWNLOAD_DIR"
  HOME="$TMP_HOME" \
  PATH="$FAKE_BIN:$PATH" \
  FAKE_DOWNLOADER_LOG="$TMP_HOME/auto-downloader.log" \
  "$ROOT/yai" download "https://example.invalid/$AUTO_DOWNLOAD_ASSET"
)

test -f "$AUTO_DOWNLOAD_DIR/autodownload.AppImage"
grep -q "fake downloader app" "$AUTO_DOWNLOAD_DIR/autodownload.AppImage"
test ! -x "$AUTO_DOWNLOAD_DIR/autodownload.AppImage"
grep -Fq $'curl-head\thttps://example.invalid/AutoDownload-x86_64.AppImage' "$TMP_HOME/auto-downloader.log"
grep -Fq $'aria2c\thttps://example.invalid/AutoDownload-x86_64.AppImage\tfile-allocation=none' "$TMP_HOME/auto-downloader.log"
test ! -e "$TMP_HOME/.local/share/yai/apps/autodownload"

HOME="$TMP_HOME" \
PATH="$FAKE_BIN:$PATH" \
FAKE_DOWNLOADER_LOG="$TMP_HOME/install-downloader.log" \
"$ROOT/yai" install "https://example.invalid/$AUTO_INSTALL_ASSET" \
  --id auto-downloader-install \
  --name "Auto Downloader Install" \
  --downloader aria2c

grep -Fq $'curl-head\thttps://example.invalid/AutoInstall-x86_64.AppImage' "$TMP_HOME/install-downloader.log"
grep -Fq $'aria2c\thttps://example.invalid/AutoInstall-x86_64.AppImage\tfile-allocation=none' "$TMP_HOME/install-downloader.log"
grep -Fq '"download_url": "https://example.invalid/AutoInstall-x86_64.AppImage"' \
  "$TMP_HOME/.local/share/yai/apps/auto-downloader-install/metadata.json"
HOME="$TMP_HOME" "$TMP_HOME/.local/bin/auto-downloader-install" | grep -q "fake downloader app"

if HOME="$TMP_HOME" "$ROOT/yai" download "https://example.invalid/$AUTO_DOWNLOAD_ASSET" --downloader bad 2>"$TMP_HOME/bad-downloader.err"; then
  echo "download accepted an unsupported downloader" >&2
  exit 1
fi
grep -q -- "--downloader must be auto, curl, wget, wget2, or aria2c" "$TMP_HOME/bad-downloader.err"

# download (repo id name) then local install must use package id
VERSIONED_ASSET="download-demo-1.2.3-x86_64.AppImage"
cp "$ORIGINAL_ROOT/$ASSET" "$TMP_HOME/$VERSIONED_ASSET"
HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
YAI_GITHUB_API_BASE="file://$API_ROOT" \
"$ROOT/yai" install "$TMP_HOME/$VERSIONED_ASSET"
grep -q '"id": "download-demo"' \
  "$TMP_HOME/.local/share/yai/apps/download-demo/metadata.json"
grep -q '"name": "Download Demo"' \
  "$TMP_HOME/.local/share/yai/apps/download-demo/metadata.json"
grep -q '"source_kind": "local_path"' \
  "$TMP_HOME/.local/share/yai/apps/download-demo/metadata.json"
HOME="$TMP_HOME" "$ROOT/yai" remove download-demo

# URL-style strip without repo: Foo-1.2-x86_64.AppImage -> id foo
cp "$ORIGINAL_ROOT/$ASSET" "$TMP_HOME/Foo-1.2-x86_64.AppImage"
HOME="$TMP_HOME" "$ROOT/yai" install "$TMP_HOME/Foo-1.2-x86_64.AppImage"
test -e "$TMP_HOME/.local/share/yai/apps/foo/metadata.json"
grep -q '"id": "foo"' "$TMP_HOME/.local/share/yai/apps/foo/metadata.json"
HOME="$TMP_HOME" "$ROOT/yai" remove foo

# id-named download then install
mkdir -p "$TMP_HOME/id-named-download"
(
  cd "$TMP_HOME/id-named-download"
  HOME="$TMP_HOME" \
  YAI_REPO_INDEX="$INDEX" \
  YAI_GITHUB_API_BASE="file://$API_ROOT" \
  "$ROOT/yai" download download-demo
)
HOME="$TMP_HOME" \
YAI_REPO_INDEX="$INDEX" \
"$ROOT/yai" install "$TMP_HOME/id-named-download/download-demo.AppImage"
grep -q '"id": "download-demo"' \
  "$TMP_HOME/.local/share/yai/apps/download-demo/metadata.json"
HOME="$TMP_HOME" "$ROOT/yai" remove download-demo

echo "download smoke test passed"
