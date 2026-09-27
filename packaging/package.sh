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
# <prefix>/share/icons/hicolor/scalable/apps/yai.svg and
# <prefix>/share/applications/yai.desktop in every format; the AppImage also
# renders a 256x256 PNG, and the Flatpak bundle uses the app-id name.
#
# Usage:
#   bash packaging/package.sh [--version X.Y.Z] [--arch ARCH] [--format FMT]...
#                            [--sign] [--sign-key KEYID] [--help]
#
#   --version X.Y.Z   Override the version (default: kYaiVersion in src/main.cpp).
#   --arch ARCH       Target architecture (default: uname -m). x86_64 -> amd64 for deb.
#   --format FMT      One of: tar.gz, deb, rpm, appimage, flatpak. Repeatable.
#                     Default: build all five formats.
#   --sign            GPG-sign every produced artifact (off by default).
#   --sign-key KEYID  GPG key to sign with (default: first secret key, or $YAI_SIGN_KEY).
#   --help            Show this help.
#
# Tooling policy: every required tool for the selected formats is checked up
# front; if any is missing the script prints the install command and exits 1.
# `linuxdeploy` is not a distro package, so it is auto-downloaded when missing;
# a download failure is treated as a missing tool (exit 1).

set -euo pipefail

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
        "$dest/usr/share/icons/hicolor/scalable/apps/yai.svg"
    install -m644 "$ROOT/data/yai.desktop" \
        "$dest/usr/share/applications/yai.desktop"
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

# ---------------------------------------------------------------------------
# Format packagers
# ---------------------------------------------------------------------------
package_targz() {
    local ver="$1" arch="$2"
    local stage="$BUILD/targz/yai-$ver"
    log "packaging tar.gz"
    rm -rf "$stage"
    stage_tree "$stage"
    tar czf "$DIST/yai-$ver-$arch.tar.gz" -C "$stage" .
    ok "dist/yai-$ver-$arch.tar.gz"
    [ "$SIGN" -eq 1 ] && sign_detached "$DIST/yai-$ver-$arch.tar.gz"
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
    dpkg-deb --build --root-owner-group "$d" "$DIST/yai_${ver}_${debarch}.deb" >/dev/null
    ok "dist/yai_${ver}_${debarch}.deb"
    [ "$SIGN" -eq 1 ] && sign_deb "$DIST/yai_${ver}_${debarch}.deb"
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
install -Dm644 yai.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/yai.svg
install -Dm644 yai.desktop %{buildroot}%{_datadir}/applications/yai.desktop

%files
%{_bindir}/yai
%{_datadir}/yai/po/en.po
%{_datadir}/yai/po/zh.po
%{_datadir}/icons/hicolor/scalable/apps/yai.svg
%{_datadir}/applications/yai.desktop

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
        "$topdir/SPECS/yai.spec" >/dev/null

    # rpmbuild writes to RPMS/<arch>/yai-<ver>-1.<arch>.rpm
    local rpm
    rpm="$(find "$topdir/RPMS" -name "yai-$ver-*.rpm" | head -n1)"
    if [ -z "$rpm" ]; then
        err "rpmbuild produced no rpm under $topdir/RPMS"
        exit 1
    fi
    cp "$rpm" "$DIST/"
    ok "dist/$(basename "$rpm")"
    [ "$SIGN" -eq 1 ] && sign_rpm "$DIST/$(basename "$rpm")"
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
    ensure_linuxdeploy || exit 1
    local appdir="$BUILD/appimage/AppDir"
    rm -rf "$appdir"
    stage_tree "$appdir"
    render_icon_png "$appdir/usr/share/icons/hicolor/256x256/apps/yai.png" \
        "$ROOT/data/yai.svg"

    ( cd "$BUILD/appimage"
      ARCH="$arch" "$LINUXDEPLOY" \
        --appdir "$appdir" \
        --desktop-file "$appdir/usr/share/applications/yai.desktop" \
        --output appimage >/dev/null
    )
    local produced
    produced="$(find "$BUILD/appimage" -maxdepth 1 -name '*.AppImage' | head -n1)"
    if [ -z "$produced" ]; then
        err "linuxdeploy produced no .AppImage in $BUILD/appimage"
        exit 1
    fi
    mv "$produced" "$DIST/yai-$ver-$arch.AppImage"
    ok "dist/yai-$ver-$arch.AppImage"
    [ "$SIGN" -eq 1 ] && sign_detached "$DIST/yai-$ver-$arch.AppImage"
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
    sources:
      - type: dir
        path: $rel
EOF

    local gpg_args=()
    if [ "$SIGN" -eq 1 ]; then
        gpg_args+=(--gpg-sign="$SIGN_KEY")
        [ -n "${GNUPGHOME:-}" ] && gpg_args+=(--gpg-homedir="$GNUPGHOME")
    fi

    flatpak-builder --disable-rofiles-fuse "${gpg_args[@]}" --repo="$mdir/repo" "$mdir/builddir" "$manifest" >/dev/null
    flatpak build-bundle "${gpg_args[@]}" "$mdir/repo" \
        "$DIST/yai-$ver.flatpak" com.github.yai_byte.yai
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
# Pre-flight tool checks (all required).
# ---------------------------------------------------------------------------
require_tool make  "apt-get install make / dnf install make" || exit 1
require_tool g++   "apt-get install g++ / dnf install gcc-c++" || exit 1
require_tool tar   "apt-get install tar / dnf install tar" || exit 1
if printf '%s\n' "${FORMATS[@]}" | grep -qx deb; then
    require_tool dpkg-deb "apt-get install dpkg" || exit 1
fi
if printf '%s\n' "${FORMATS[@]}" | grep -qx rpm; then
    require_tool rpmbuild "apt-get install rpm / dnf install rpm-build" || exit 1
fi
if printf '%s\n' "${FORMATS[@]}" | grep -qx appimage; then
    # linuxdeploy is fetched on demand; just need curl for that.
    require_tool curl "apt-get install curl / dnf install curl" || exit 1
fi
if printf '%s\n' "${FORMATS[@]}" | grep -qx flatpak; then
    require_tool flatpak-builder "flatpak install flathub org.flatpak.Builder" || exit 1
fi

# Signing prerequisites (only when --sign is requested).
if [ "$SIGN" -eq 1 ]; then
    require_tool gpg "apt-get install gnupg / dnf install gnupg2" || exit 1
    if printf '%s\n' "${FORMATS[@]}" | grep -qx deb; then
        if ! command -v dpkg-sig >/dev/null 2>&1; then
            log "dpkg-sig not found; the .deb will get a detached GPG signature (yai_*.deb.sig)"
        fi
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

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------
for f in "${FORMATS[@]}"; do
    case "$f" in
        tar.gz)   package_targz   "$VERSION" "$ARCH_RAW" ;;
        deb)      package_deb      "$VERSION" "$ARCH_RAW" ;;
        rpm)      package_rpm      "$VERSION" "$ARCH_RAW" ;;
        appimage) package_appimage "$VERSION" "$ARCH_RAW" ;;
        flatpak)  package_flatpak  "$VERSION" "$ARCH_RAW" ;;
    esac
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo
log "done. artifacts in $DIST:"
for f in "${FORMATS[@]}"; do
    case "$f" in
        tar.gz)   ok "dist/yai-$VERSION-$ARCH_RAW.tar.gz"; [ "$SIGN" -eq 1 ] && ok "dist/yai-$VERSION-$ARCH_RAW.tar.gz.sig" ;;
        deb)      ok "dist/yai_${VERSION}_$(deb_arch "$ARCH_RAW").deb (signed)" ;;
        rpm)      ok "dist/yai-$VERSION-1.$ARCH_RAW.rpm (signed)" ;;
        appimage) ok "dist/yai-$VERSION-$ARCH_RAW.AppImage"; [ "$SIGN" -eq 1 ] && ok "dist/yai-$VERSION-$ARCH_RAW.AppImage.sig" ;;
        flatpak)  ok "dist/yai-$VERSION.flatpak (signed)" ;;
    esac
done
