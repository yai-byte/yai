#!/usr/bin/env bash
#
# package.sh — one-shot multi-format packager for yai.
#
# Builds the single `yai` binary via the project Makefile and produces release
# artifacts for tar.gz, deb, rpm, AppImage and Flatpak into packaging/dist/.
#
# Translation catalogs are always installed to <prefix>/share/yai/po/ so that
# src/i18n.cpp::translation_dirs() finds them automatically in every layout
# (<exe_dir>/../share/yai/po).
#
# Desktop integration assets (data/yai.svg, data/yai.desktop) are installed to
# <prefix>/share/icons/hicolor/scalable/apps/com.github.yai_byte.yai.svg and
# <prefix>/share/applications/com.github.yai_byte.yai.desktop in every format; the AppImage also
# renders a 256x256 PNG, and the Flatpak bundle uses the app-id name.
#
# Usage:
#   bash packaging/package.sh [--version X.Y.Z] [--arch ARCH] [--format FMT]...
#                            [--sign] [--sign-key KEYID] [--sign-only]
#                            [--embed-sign] [--help]
#
#   --version X.Y.Z   Override the version (default: kYaiVersion in src/main.cpp).
#   --arch ARCH       Target architecture (default: uname -m). x86_64 -> amd64 for deb.
#   --format FMT      One of: tar.gz, deb, rpm, appimage, flatpak. Repeatable.
#                     Default: build all five formats.
#   --sign            GPG-sign every produced artifact (off by default).
#   --sign-key KEYID  GPG key to sign with (default: first secret key, or $YAI_SIGN_KEY).
#   --sign-only       Do not build; GPG-sign artifacts already in packaging/dist/.
#   --embed-sign      Embed the GPG signature into the AppImage via appimagetool
#                     (self-verifiable by AppImageKit) instead of a detached .sig.
#   --help            Show this help.
#
# Tooling policy: every required tool for the selected formats is checked up
# front; if any is missing the script prints the install command and exits 1.
# `linuxdeploy` is not a distro package, so it is auto-downloaded when missing;
# a download failure is treated as a missing tool (exit 1).

set -euo pipefail

# Create world-readable artifacts by default. When the build runs inside a
# virt-manager VM, shared filesystems surface the output to the host mapped to
# the qemu service user; a restrictive umask would leave them 0600/0700 and
# unreadable on the host. 022 -> 0644 files / 0755 dirs.
umask 022

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST="$SCRIPT_DIR/dist"
BUILD="$SCRIPT_DIR/build"
TOOLS="$SCRIPT_DIR/tools"

# ---------------------------------------------------------------------------
# Defaults / state
# ---------------------------------------------------------------------------
VERSION=""
ARCH_RAW="$(uname -m)"
FORMATS=()
SIGN=0
SIGN_KEY="${YAI_SIGN_KEY:-}"
SIGN_ONLY=0
APPIMAGE_EMBED_SIGN=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log()  { printf '\033[1;34m[package]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[ ok ]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; }

usage() {
    sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

# require_tool <cmd> <install-hint>
require_tool() {
    local cmd="$1" hint="$2"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        err "required tool '$cmd' not found."
        err "  install: $hint"
        return 1
    fi
}

# ensure_linuxdeploy -> sets LINUXDEPLOY (path to executable) or errors.
ensure_linuxdeploy() {
    if command -v linuxdeploy >/dev/null 2>&1; then
        LINUXDEPLOY="$(command -v linuxdeploy)"
        return 0
    fi
    # yai-installed copy (wrapper on PATH already covered above). Use the real
    # AppImage directly so an install done via `yai install linuxdeploy` is reused
    # even when ~/.local/bin is not on the script's PATH.
    local yai_app
    for yai_app in \
        "$HOME/.local/share/yai/apps/linuxdeploy/current.AppImage" \
        "/usr/local/share/yai/apps/linuxdeploy/current.AppImage"; do
        if [ -x "$yai_app" ]; then
            LINUXDEPLOY="$yai_app"
            return 0
        fi
    done
    local f="$TOOLS/linuxdeploy-x86_64.AppImage"
    if [ -x "$f" ]; then
        LINUXDEPLOY="$f"
        return 0
    fi
    log "linuxdeploy not found in PATH; downloading to $f"
    mkdir -p "$TOOLS"
    require_tool curl "apt-get install curl / dnf install curl" || return 1
    if ! curl -fL -o "$f" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"; then
        err "failed to download linuxdeploy."
        return 1
    fi
    chmod +x "$f"
    LINUXDEPLOY="$f"
}

# ensure_appimage_tooling -> sets APPIMAGETOOL and RUNTIME_FILE (paths) or errors.
# Both are fetched via curl, which works behind the sandbox's egress filter. This
# deliberately bypasses appimagetool's own runtime downloader, which fails in
# restricted networks ("Failed to download runtime: server returned status code 0").
ensure_appimage_tooling() {
    [ -n "${APPIMAGETOOL:-}" ] && [ -n "${RUNTIME_FILE:-}" ] && return 0
    require_tool curl "apt-get install curl / dnf install curl" || return 1
    mkdir -p "$TOOLS"
    if [ ! -x "$TOOLS/appimagetool-x86_64.AppImage" ]; then
        log "appimagetool not found; downloading to $TOOLS/appimagetool-x86_64.AppImage"
        if ! curl -fSL --retry 3 -o "$TOOLS/appimagetool-x86_64.AppImage" \
            "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"; then
            err "failed to download appimagetool."
            return 1
        fi
        chmod +x "$TOOLS/appimagetool-x86_64.AppImage"
    fi
    if [ ! -f "$TOOLS/runtime-x86_64" ]; then
        log "AppImage runtime not found; downloading to $TOOLS/runtime-x86_64"
        if ! curl -fSL --retry 3 -o "$TOOLS/runtime-x86_64" \
            "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"; then
            err "failed to download AppImage runtime."
            return 1
        fi
    fi
    APPIMAGETOOL="$TOOLS/appimagetool-x86_64.AppImage"
    RUNTIME_FILE="$TOOLS/runtime-x86_64"
}

# debian-ish architecture from raw (x86_64 -> amd64, aarch64 -> arm64, ...)
deb_arch() {
    case "$1" in
        x86_64)  echo amd64 ;;
        aarch64) echo arm64 ;;
        armv7l|armhf) echo armhf ;;
        i386|i686) echo i386 ;;
        ppc64le) echo ppc64el ;;
        s390x)   echo s390x ;;
        riscv64) echo riscv64 ;;
        loongarch64) echo loong64 ;;
        *)       echo "$1" ;;
    esac
}

# ---------------------------------------------------------------------------
# Version extraction
# ---------------------------------------------------------------------------
extract_version() {
    if [ -n "$VERSION" ]; then
        return 0
    fi
    if [ ! -f "$ROOT/src/main.cpp" ]; then
        err "src/main.cpp not found; cannot auto-detect version. Use --version."
        exit 1
    fi
    local v
    v="$(grep -oP 'kYaiVersion = "\K[^"]+' "$ROOT/src/main.cpp" || true)"
    if [ -z "$v" ]; then
        err "could not parse kYaiVersion from src/main.cpp. Use --version."
        exit 1
    fi
    VERSION="$v"
}

# ---------------------------------------------------------------------------
# Build + stage
# ---------------------------------------------------------------------------
build_binary() {
    log "building yai via make (CXX=${CXX:-g++})"
    make -C "$ROOT" ${CXX:+CXX="$CXX"} ${CXXFLAGS:+CXXFLAGS="$CXXFLAGS"} >/dev/null
    if [ ! -x "$ROOT/yai" ]; then
        err "make did not produce an executable $ROOT/yai"
        exit 1
    fi
    ok "built $ROOT/yai"
}

# stage_tree <dest> -> dest/usr/bin/yai + dest/usr/share/yai/po/*.po
# plus the desktop integration assets (icon + .desktop). Uses the standard FHS
# usr/ prefix so the layout works for tar.gz (extract to /), deb, rpm, and
# AppImage (AppDir/usr/bin) alike. translation_dirs() resolves
# <exe_dir>/../share/yai/po correctly in every case; the icon/desktop land in
# the conventional hicolor/applications paths so desktop environments pick them up.
# Presence of the assets is checked once up front (see the build section).
stage_tree() {
    local dest="$1"
    mkdir -p "$dest/usr/bin" "$dest/usr/share/yai/po" \
             "$dest/usr/share/applications" \
             "$dest/usr/share/metainfo" \
             "$dest/usr/share/icons/hicolor/scalable/apps"
    install -m755 "$ROOT/yai" "$dest/usr/bin/yai"
    local p found=0
    for p in "$ROOT"/po/*.po; do
        [ -e "$p" ] || continue
        install -m644 "$p" "$dest/usr/share/yai/po/"
        found=1
    done
    if [ "$found" -eq 0 ]; then
        err "no .po files found in $ROOT/po"
        exit 1
    fi
    install -m644 "$ROOT/data/yai.svg" \
        "$dest/usr/share/icons/hicolor/scalable/apps/com.github.yai_byte.yai.svg"
    install -m644 "$ROOT/data/yai.desktop" \
        "$dest/usr/share/applications/com.github.yai_byte.yai.desktop"
    install -m644 "$ROOT/data/yai.metainfo.xml" \
        "$dest/usr/share/metainfo/com.github.yai_byte.yai.metainfo.xml"
}

# ---------------------------------------------------------------------------
# GPG signing helpers (only invoked when --sign is set)
# ---------------------------------------------------------------------------
# sign_detached <artifact> -> writes <artifact>.sig (binary GPG detached signature)
sign_detached() {
    local art="$1"
    local args=()
    [ -n "$SIGN_KEY" ] && args+=(--local-user "$SIGN_KEY")
    gpg --batch --yes --detach-sign "${args[@]}" --output "$art.sig" "$art"
    ok "signed $(basename "$art") -> $(basename "$art").sig"
}

# sign_deb <deb> -> sign the .deb. Uses dpkg-sig for an embedded signature when
# available (Debian/Ubuntu); otherwise falls back to a detached GPG signature
# (<deb>.sig) so signing still works on hosts without dpkg-sig (e.g. Fedora).
sign_deb() {
    local deb="$1"
    if command -v dpkg-sig >/dev/null 2>&1; then
        local args=()
        [ -n "$SIGN_KEY" ] && args+=(-k "$SIGN_KEY")
        dpkg-sig --sign=builder "${args[@]}" "$deb"
        ok "signed $(basename "$deb") (embedded via dpkg-sig)"
    else
        local args=()
        [ -n "$SIGN_KEY" ] && args+=(--local-user "$SIGN_KEY")
        gpg --batch --yes --detach-sign "${args[@]}" --output "$deb.sig" "$deb"
        ok "signed $(basename "$deb") -> $(basename "$deb").sig (detached; dpkg-sig unavailable)"
    fi
}

# sign_rpm <rpm> -> embed a GPG signature into the .rpm via rpmsign
sign_rpm() {
    local rpm="$1"
    local args=()
    [ -n "$SIGN_KEY" ] && args+=(--key-id "$SIGN_KEY")
    rpmsign --addsign "${args[@]}" "$rpm" >/dev/null
    ok "signed $(basename "$rpm") (embedded via rpmsign)"
}

# sign_appimage <appimage> -> GPG-sign the AppImage. By default writes a detached
# signature (<appimage>.sig). With --embed-sign, embeds the signature directly into
# the AppImage via appimagetool so AppImageKit can self-verify it at runtime.
sign_appimage() {
    local ai="$1"
    if [ "$APPIMAGE_EMBED_SIGN" -eq 1 ]; then
        local tool="${APPIMAGETOOL:-}"
        if [ -z "$tool" ] && command -v appimagetool >/dev/null 2>&1; then
            tool="$(command -v appimagetool)"
        fi
        if [ -z "$tool" ]; then
            log "appimagetool not found; falling back to detached GPG signature"
            sign_detached "$ai"
            return 0
        fi
        local args=()
        [ -n "$SIGN_KEY" ] && args+=(--sign-key "$SIGN_KEY")
        # appimagetool is itself an AppImage; run FUSE-less if needed.
        APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --sign "${args[@]}" "$ai" >/dev/null
        ok "signed $(basename "$ai") (embedded via appimagetool)"
    else
        sign_detached "$ai"
    fi
}

# sign_existing <ver> <arch> -> sign artifacts already present in $DIST (used by
# --sign-only). Flatpak bundles are signed at bundle time by flatpak-builder and
# cannot be re-signed from the file alone, so they are skipped with a note.
sign_existing() {
    local ver="$1" arch="$2"
    local debarch f signed=0
    debarch="$(deb_arch "$arch")"

    f="$DIST/yai-$ver-$arch.tar.gz"
    if [ -f "$f" ]; then sign_detached "$f"; signed=1; fi

    f="$DIST/yai_${ver}_${debarch}.deb"
    if [ -f "$f" ]; then sign_deb "$f"; signed=1; fi

    f="$(find "$DIST" -maxdepth 1 -name "yai-$ver-*.rpm" 2>/dev/null | head -n1 || true)"
    if [ -n "$f" ] && [ -f "$f" ]; then sign_rpm "$f"; signed=1; fi

    f="$DIST/yai-$ver-$arch.AppImage"
    if [ -f "$f" ]; then sign_appimage "$f"; signed=1; fi

    f="$DIST/yai-$ver.flatpak"
    if [ -f "$f" ]; then
        log "flatpak already present; --sign-only cannot re-sign a .flatpak bundle (signed at bundle time) — skipping"
    fi

    if [ "$signed" -eq 0 ]; then
        err "no signable artifacts found in $DIST for version $ver / arch $arch"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Format packagers
# ---------------------------------------------------------------------------
package_targz() {
    local ver="$1" arch="$2"
    local stage="$BUILD/targz/yai-$ver"
    log "packaging tar.gz"
    rm -rf "$stage"
    stage_tree "$stage"
    tar czf "$DIST/yai-$ver-$arch.tar.gz" -C "$stage" . || { err "tar failed"; return 1; }
    ok "dist/yai-$ver-$arch.tar.gz"
    if [ "$SIGN" -eq 1 ]; then sign_detached "$DIST/yai-$ver-$arch.tar.gz"; fi
}

package_deb() {
    local ver="$1" arch="$2"
    local debarch
    debarch="$(deb_arch "$arch")"
    local d="$BUILD/deb/yai_${ver}_${debarch}"
    log "packaging deb ($debarch)"
    rm -rf "$d"
    stage_tree "$d"
    mkdir -p "$d/DEBIAN"
    cat > "$d/DEBIAN/control" <<EOF
Package: yai
Version: $ver
Section: utils
Priority: optional
Architecture: $debarch
Depends: curl
Maintainer: yai-byte <yai@example.com>
Homepage: https://github.com/yai-byte/yai
Description: Fast, dependency-light AppImage package manager
 Installs, updates, upgrades, rolls back and repairs Linux AppImage
 applications, resolving download URLs from GitHub releases, custom
 repository indexes, AppImageHub feeds and project websites.
EOF
    dpkg-deb --build --root-owner-group "$d" "$DIST/yai_${ver}_${debarch}.deb" >/dev/null || { err "dpkg-deb build failed"; return 1; }
    ok "dist/yai_${ver}_${debarch}.deb"
    if [ "$SIGN" -eq 1 ]; then sign_deb "$DIST/yai_${ver}_${debarch}.deb"; fi
}

package_rpm() {
    local ver="$1" arch="$2"
    local topdir="$BUILD/rpm"
    local srcpkg="$BUILD/srcpkg/yai-$ver"
    log "packaging rpm"
    rm -rf "$topdir" "$srcpkg"
    mkdir -p "$topdir"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    mkdir -p "$srcpkg/po"
    install -m755 "$ROOT/yai" "$srcpkg/yai"
    local p
    for p in "$ROOT"/po/*.po; do
        [ -e "$p" ] || continue
        install -m644 "$p" "$srcpkg/po/"
    done
    install -m644 "$ROOT/data/yai.svg" "$srcpkg/yai.svg"
    install -m644 "$ROOT/data/yai.desktop" "$srcpkg/yai.desktop"
    install -m644 "$ROOT/data/yai.metainfo.xml" "$srcpkg/yai.metainfo.xml"
    tar czf "$topdir/SOURCES/yai-$ver.tar.gz" -C "$BUILD/srcpkg" "yai-$ver"

    cat > "$topdir/SPECS/yai.spec" <<EOF
%global debug_package %{nil}
Name:           yai
Version:        $ver
Release:        1%{?dist}
Summary:        Fast, dependency-light AppImage package manager
License:        MIT
URL:            https://github.com/yai-byte/yai
Source0:        yai-%{version}.tar.gz
Requires:       curl
BuildArch:      $arch

%description
yai installs, updates, upgrades, rolls back and repairs Linux AppImage
applications, resolving download URLs from GitHub releases, custom
repository indexes, AppImageHub feeds and project websites.

%prep
%setup -q

%build
# Prebuilt single binary; nothing to compile in the package stage.

%install
mkdir -p %{buildroot}%{_bindir} %{buildroot}%{_datadir}/yai/po
install -m755 yai %{buildroot}%{_bindir}/yai
install -m644 po/en.po %{buildroot}%{_datadir}/yai/po/en.po
install -m644 po/zh.po %{buildroot}%{_datadir}/yai/po/zh.po
install -Dm644 yai.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/com.github.yai_byte.yai.svg
install -Dm644 yai.desktop %{buildroot}%{_datadir}/applications/com.github.yai_byte.yai.desktop
install -Dm644 yai.metainfo.xml %{buildroot}%{_datadir}/metainfo/com.github.yai_byte.yai.metainfo.xml

%files
%{_bindir}/yai
%{_datadir}/yai/po/en.po
%{_datadir}/yai/po/zh.po
%{_datadir}/icons/hicolor/scalable/apps/com.github.yai_byte.yai.svg
%{_datadir}/applications/com.github.yai_byte.yai.desktop
%{_datadir}/metainfo/com.github.yai_byte.yai.metainfo.xml

%changelog
* $(LC_ALL=C date '+%a %b %d %Y') yai-byte <yai@example.com> - $ver-1
- Automated build via packaging/package.sh
EOF

    rpmbuild -bb \
        --define "_topdir $topdir" \
        --define "_sourcedir $topdir/SOURCES" \
        --define "_specdir $topdir/SPECS" \
        --define "_srcrpmdir $topdir/SRPMS" \
        --define "_rpmdir $topdir/RPMS" \
        --define "_builddir $topdir/BUILD" \
        "$topdir/SPECS/yai.spec" >/dev/null \
        || { err "rpmbuild failed"; return 1; }

    # rpmbuild writes to RPMS/<arch>/yai-<ver>-1.<arch>.rpm
    local rpm
    rpm="$(find "$topdir/RPMS" -name "yai-$ver-*.rpm" | head -n1)"
    if [ -z "$rpm" ]; then
        err "rpmbuild produced no rpm under $topdir/RPMS"
        return 1
    fi
    cp "$rpm" "$DIST/"
    ok "dist/$(basename "$rpm")"
    if [ "$SIGN" -eq 1 ]; then sign_rpm "$DIST/$(basename "$rpm")"; fi
}

# render_icon_png <out_png> <src_svg> -> render a 256x256 PNG via an available
# renderer. Optional: if no rasterizer is installed we just warn and skip (the
# SVG is still shipped, so desktop environments with native SVG support are fine).
render_icon_png() {
    local out="$1" src="$2"
    mkdir -p "$(dirname "$out")"
    if command -v rsvg-convert >/dev/null 2>&1; then
        rsvg-convert -w 256 -h 256 "$src" -o "$out"
    elif command -v inkscape >/dev/null 2>&1; then
        inkscape "$src" --export-filename="$out" -w 256 -h 256
    elif command -v convert >/dev/null 2>&1; then
        convert -background none -resize 256x256 "$src" "$out"
    else
        log "no PNG renderer (rsvg-convert/inkscape/convert) found; skipping 256x256 PNG"
        return 0
    fi
    ok "rendered $(basename "$out")"
}

package_appimage() {
    local ver="$1" arch="$2"
    log "packaging AppImage"
    ensure_appimage_tooling || return 1
    local appdir="$BUILD/appimage/AppDir"
    rm -rf "$appdir"
    stage_tree "$appdir"
    render_icon_png "$appdir/usr/share/icons/hicolor/256x256/apps/com.github.yai_byte.yai.png" \
        "$ROOT/data/yai.svg"
    # AppRun: appimagetool launches AppDir/AppRun at runtime.
    ln -sf usr/bin/yai "$appdir/AppRun"
    # Point the desktop Icon at the shipped icon id so appimagetool can resolve it.
    sed -i 's/^Icon=yai/Icon=com.github.yai_byte.yai/' \
        "$appdir/usr/share/applications/com.github.yai_byte.yai.desktop"
    # .DirIcon: a PNG thumbnail for broad file-manager compatibility.
    if [ -f "$appdir/usr/share/icons/hicolor/256x256/apps/com.github.yai_byte.yai.png" ]; then
        ln -sf "usr/share/icons/hicolor/256x256/apps/com.github.yai_byte.yai.png" "$appdir/.DirIcon"
    fi
    # appimagetool resolves the entry point and icon from symlinks at the AppDir
    # root (mirrors what linuxdeploy deploys there).
    ln -sf "usr/share/applications/com.github.yai_byte.yai.desktop" "$appdir/com.github.yai_byte.yai.desktop"
    ln -sf "usr/share/icons/hicolor/scalable/apps/com.github.yai_byte.yai.svg" "$appdir/com.github.yai_byte.yai.svg"

    local out="$BUILD/appimage/yai-$ver-$arch.AppImage"
    local ldlog="$BUILD/appimage/appimagetool.log"
    set +e
    ( cd "$BUILD/appimage"
      # appimagetool is itself an AppImage; in FUSE-less VMs/containers let it
      # extract-and-run. We pass the runtime explicitly to avoid its internal
      # downloader, which fails behind restricted egress ("status code 0").
      ARCH="$arch" APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" \
        --runtime-file "$RUNTIME_FILE" \
        "$appdir" "$out"
    ) >"$ldlog" 2>&1
    local rc=$?
    set -e
    if [ "$rc" -ne 0 ] || [ ! -f "$out" ]; then
        err "appimagetool failed (rc=$rc); see $ldlog"
        log "--- contents of $BUILD/appimage ---"
        ls -la "$BUILD/appimage" || true
        log "--- appimagetool.log (tail) ---"
        tail -n 40 "$ldlog" || true
        return 1
    fi
    mv "$out" "$DIST/yai-$ver-$arch.AppImage"
    ok "dist/yai-$ver-$arch.AppImage"
    if [ "$SIGN" -eq 1 ]; then sign_appimage "$DIST/yai-$ver-$arch.AppImage"; fi
}

package_flatpak() {
    local ver="$1" arch="$2"
    log "packaging Flatpak"
    local mdir="$BUILD/flatpak"
    rm -rf "$mdir"
    mkdir -p "$mdir"
    local manifest="$mdir/com.github.yai-byte.yai.yaml"
    local rel
    rel="$(realpath --relative-to="$mdir" "$ROOT")"

    cat > "$manifest" <<EOF
id: com.github.yai_byte.yai
runtime: org.freedesktop.Platform
runtime-version: '23.08'
sdk: org.freedesktop.Sdk
command: yai
modules:
  - name: yai
    buildsystem: simple
    build-options:
      env:
        CXXFLAGS: '-std=c++17 -O2 -pthread'
    build-commands:
      - make
      - install -Dm755 yai /app/bin/yai
      - install -Dm644 po/en.po /app/share/yai/po/en.po
      - install -Dm644 po/zh.po /app/share/yai/po/zh.po
      - install -Dm644 data/yai.svg /app/share/icons/hicolor/scalable/apps/com.github.yai_byte.yai.svg
      - mkdir -p /app/share/applications && sed 's/^Icon=yai/Icon=com.github.yai_byte.yai/' data/yai.desktop > /app/share/applications/com.github.yai_byte.yai.desktop
      - mkdir -p /app/share/metainfo && sed -e 's|<id>yai</id>|<id>com.github.yai_byte.yai</id>|' -e 's|<launchable type="desktop-id">yai.desktop</launchable>|<launchable type="desktop-id">com.github.yai_byte.yai.desktop</launchable>|' data/yai.metainfo.xml > /app/share/metainfo/com.github.yai_byte.yai.metainfo.xml
    sources:
      - type: dir
        path: $rel
EOF

    local gpg_args=()
    if [ "$SIGN" -eq 1 ]; then
        gpg_args+=(--gpg-sign="$SIGN_KEY")
        [ -n "${GNUPGHOME:-}" ] && gpg_args+=(--gpg-homedir="$GNUPGHOME")
    fi

    flatpak-builder --disable-rofiles-fuse "${gpg_args[@]}" --repo="$mdir/repo" "$mdir/builddir" "$manifest" >/dev/null \
        || { err "flatpak-builder failed"; return 1; }
    flatpak build-bundle "${gpg_args[@]}" "$mdir/repo" \
        "$DIST/yai-$ver.flatpak" com.github.yai_byte.yai \
        || { err "flatpak build-bundle failed"; return 1; }
    ok "dist/yai-$ver.flatpak"
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        --version) VERSION="${2:-}"; [ -n "$VERSION" ] || { err "--version needs a value"; exit 1; }; shift 2 ;;
        --arch)    ARCH_RAW="${2:-}"; [ -n "$ARCH_RAW" ] || { err "--arch needs a value"; exit 1; }; shift 2 ;;
        --format)  FORMATS+=("${2:-}"); [ -n "${FORMATS[-1]}" ] || { err "--format needs a value"; exit 1; }; shift 2 ;;
        --sign)    SIGN=1; shift ;;
        --sign-key) SIGN_KEY="${2:-}"; [ -n "$SIGN_KEY" ] || { err "--sign-key needs a value"; exit 1; }; shift 2 ;;
        --sign-only) SIGN_ONLY=1; shift ;;
        --embed-sign) APPIMAGE_EMBED_SIGN=1; shift ;;
        --help|-h) usage ;;
        *) err "unknown argument: $1"; usage ;;
    esac
done

# Default: all formats.
if [ ${#FORMATS[@]} -eq 0 ]; then
    FORMATS=(tar.gz deb rpm appimage flatpak)
fi

# Validate formats.
declare -A KNOWN=( [tar.gz]=1 [deb]=1 [rpm]=1 [appimage]=1 [flatpak]=1 )
for f in "${FORMATS[@]}"; do
    if [ -z "${KNOWN[$f]:-}" ]; then
        err "unknown format: $f (use tar.gz|deb|rpm|appimage|flatpak)"
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# Sign-only mode: sign artifacts already in $DIST, do not rebuild. This is the
# typical workflow when the AppImage was built on an older system (e.g. a VM or
# container) to keep glibc requirements low, then brought back to the host where
# the GPG secret key lives.
# ---------------------------------------------------------------------------
if [ "$SIGN_ONLY" -eq 1 ]; then
    SIGN=1
    extract_version
    require_tool gpg "apt-get install gnupg / dnf install gnupg2" || exit 1
    if [ "$APPIMAGE_EMBED_SIGN" -eq 1 ] && printf '%s\n' "${FORMATS[@]}" | grep -qx appimage; then
        ensure_appimage_tooling || exit 1
    fi
    if [ -z "$SIGN_KEY" ]; then
        SIGN_KEY="$(gpg --list-secret-keys --with-colons 2>/dev/null | awk -F: '$1=="sec"{print $5; exit}')"
        if [ -z "$SIGN_KEY" ]; then
            err "no GPG secret key available and --sign-key was not given; cannot sign."
            exit 1
        fi
        log "signing with default GPG secret key $SIGN_KEY"
    fi
    log "sign-only: signing existing artifacts in $DIST (version=$VERSION arch=$ARCH_RAW)"
    sign_existing "$VERSION" "$ARCH_RAW"
    echo
    log "done. signed artifacts in $DIST."
    exit 0
fi

# ---------------------------------------------------------------------------
# Pre-flight tool checks (all required).
# ---------------------------------------------------------------------------
require_tool make  "apt-get install make / dnf install make" || exit 1
require_tool g++   "apt-get install g++ / dnf install gcc-c++" || exit 1
require_tool tar   "apt-get install tar / dnf install tar" || exit 1

# Per-format tool checks: a missing tool SKIPS that format (with a warning)
# instead of aborting the whole run, so a default invocation still produces every
# format it can. This is what previously left only tar.gz behind when e.g.
# dpkg-deb / rpmbuild / flatpak-builder were absent on the build host.
drop_format() {
    local f="$1" reason="$2" out=()
    log "skipping format '$f': $reason"
    for x in "${FORMATS[@]}"; do
        if [ "$x" != "$f" ]; then out+=("$x"); fi
    done
    FORMATS=("${out[@]}")
}
if printf '%s\n' "${FORMATS[@]}" | grep -qx deb; then
    command -v dpkg-deb >/dev/null 2>&1 || drop_format deb "dpkg-deb not found (apt-get install dpkg)"
fi
if printf '%s\n' "${FORMATS[@]}" | grep -qx rpm; then
    command -v rpmbuild >/dev/null 2>&1 || drop_format rpm "rpmbuild not found (dnf install rpm-build)"
fi
if printf '%s\n' "${FORMATS[@]}" | grep -qx appimage; then
    # linuxdeploy is fetched on demand; just need curl for that.
    command -v curl >/dev/null 2>&1 || drop_format appimage "curl not found (apt-get install curl)"
fi
if printf '%s\n' "${FORMATS[@]}" | grep -qx flatpak; then
    command -v flatpak-builder >/dev/null 2>&1 || drop_format flatpak "flatpak-builder not found (flatpak install flathub org.flatpak.Builder)"
fi
if [ ${#FORMATS[@]} -eq 0 ]; then
    err "no formats left after tool checks"
    exit 1
fi

# Signing prerequisites (only when --sign is requested).
if [ "$SIGN" -eq 1 ]; then
    require_tool gpg "apt-get install gnupg / dnf install gnupg2" || exit 1
    if printf '%s\n' "${FORMATS[@]}" | grep -qx deb; then
        if ! command -v dpkg-sig >/dev/null 2>&1; then
            log "dpkg-sig not found; the .deb will get a detached GPG signature (yai_*.deb.sig)"
        fi
    fi
    if [ "$APPIMAGE_EMBED_SIGN" -eq 1 ] && printf '%s\n' "${FORMATS[@]}" | grep -qx appimage; then
        ensure_appimage_tooling || exit 1
    fi
    # rpmsign ships with the rpm package (already required for the rpm format).
    if [ -z "$SIGN_KEY" ]; then
        SIGN_KEY="$(gpg --list-secret-keys --with-colons 2>/dev/null | awk -F: '$1=="sec"{print $5; exit}')"
        if [ -z "$SIGN_KEY" ]; then
            err "no GPG secret key available and --sign-key was not given; cannot sign."
            exit 1
        fi
        log "signing with default GPG secret key $SIGN_KEY"
    fi
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
extract_version
mkdir -p "$DIST" "$BUILD"
log "version=$VERSION arch=$ARCH_RAW formats=${FORMATS[*]}"
build_binary

# Desktop integration assets are required by every supported format.
if [ ! -f "$ROOT/data/yai.svg" ]; then
    err "required asset $ROOT/data/yai.svg missing (export your Figma icon there)"
    exit 1
fi
if [ ! -f "$ROOT/data/yai.desktop" ]; then
    err "required asset $ROOT/data/yai.desktop missing"
    exit 1
fi
if [ ! -f "$ROOT/data/yai.metainfo.xml" ]; then
    err "required asset $ROOT/data/yai.metainfo.xml missing"
    exit 1
fi

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------
failed=()
for f in "${FORMATS[@]}"; do
    if ! case "$f" in
        tar.gz)   package_targz   "$VERSION" "$ARCH_RAW" ;;
        deb)      package_deb      "$VERSION" "$ARCH_RAW" ;;
        rpm)      package_rpm      "$VERSION" "$ARCH_RAW" ;;
        appimage) package_appimage "$VERSION" "$ARCH_RAW" ;;
        flatpak)  package_flatpak  "$VERSION" "$ARCH_RAW" ;;
    esac; then
        failed+=("$f")
        err "format '$f' failed; continuing with the rest"
    fi
done

# ---------------------------------------------------------------------------
# Relax permissions so artifacts are readable regardless of which user
# virt-manager/qemu maps them to on the host. Best-effort; never abort.
# ---------------------------------------------------------------------------
chmod -R a+rX,u+rw,g+rw "$DIST" 2>/dev/null || true

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo
log "done. artifacts in $DIST:"
for f in "${FORMATS[@]}"; do
    if [ ${#failed[@]} -gt 0 ] && printf '%s\n' "${failed[@]}" | grep -qx "$f"; then
        err "dist: format '$f' FAILED — no artifact produced"
        continue
    fi
    case "$f" in
        tar.gz)   ok "dist/yai-$VERSION-$ARCH_RAW.tar.gz"; [ "$SIGN" -eq 1 ] && ok "dist/yai-$VERSION-$ARCH_RAW.tar.gz.sig" ;;
        deb)      if [ "$SIGN" -eq 1 ]; then ok "dist/yai_${VERSION}_$(deb_arch "$ARCH_RAW").deb (signed)"; else ok "dist/yai_${VERSION}_$(deb_arch "$ARCH_RAW").deb"; fi ;;
        rpm)      if [ "$SIGN" -eq 1 ]; then ok "dist/yai-$VERSION-1.$ARCH_RAW.rpm (signed)"; else ok "dist/yai-$VERSION-1.$ARCH_RAW.rpm"; fi ;;
        appimage) ok "dist/yai-$VERSION-$ARCH_RAW.AppImage"; [ "$SIGN" -eq 1 ] && { if [ "$APPIMAGE_EMBED_SIGN" -eq 1 ]; then ok "dist/yai-$VERSION-$ARCH_RAW.AppImage (signed, embedded)"; else ok "dist/yai-$VERSION-$ARCH_RAW.AppImage.sig"; fi; } ;;
        flatpak)  if [ "$SIGN" -eq 1 ]; then ok "dist/yai-$VERSION.flatpak (signed)"; else ok "dist/yai-$VERSION.flatpak"; fi ;;
    esac
done
if [ ${#failed[@]} -gt 0 ]; then
    err "the following formats failed: ${failed[*]}"
    exit 1
fi
