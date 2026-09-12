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
# Usage:
#   bash packaging/package.sh [--version X.Y.Z] [--arch ARCH] [--format FMT]... [--help]
#
#   --version X.Y.Z   Override the version (default: kYaiVersion in src/main.cpp).
#   --arch ARCH       Target architecture (default: uname -m). x86_64 -> amd64 for deb.
#   --format FMT      One of: tar.gz, deb, rpm, appimage, flatpak. Repeatable.
#                     Default: build all five formats.
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
# Uses the standard FHS usr/ prefix so the layout works for tar.gz (extract to /),
# deb, rpm, and AppImage (AppDir/usr/bin) alike. translation_dirs() resolves
# <exe_dir>/../share/yai/po correctly in every case.
stage_tree() {
    local dest="$1"
    mkdir -p "$dest/usr/bin" "$dest/usr/share/yai/po"
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

%files
%{_bindir}/yai
%{_datadir}/yai/po/en.po
%{_datadir}/yai/po/zh.po

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
}

# write a desktop entry used by AppImage (and optionally Flatpak)
write_desktop() {
    local out="$1"
    cat > "$out" <<'EOF'
[Desktop Entry]
Name=yai
Comment=AppImage Package Manager
Exec=yai
Terminal=true
Type=Application
Icon=yai
Categories=Utility;
EOF
}

# write a minimal valid PNG icon (used so linuxdeploy has an Icon= to deploy)
write_icon() {
    local out="$1"
    mkdir -p "$(dirname "$out")"
    base64 -d > "$out" <<'PNG'
iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAOklEQVR4nO3BAQ0AAADCoPdPbQ43oAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAvg0hAAABh2T8RwAAAABJRU5ErkJggg==
PNG
}

package_appimage() {
    local ver="$1" arch="$2"
    log "packaging AppImage"
    ensure_linuxdeploy || exit 1
    local appdir="$BUILD/appimage/AppDir"
    rm -rf "$appdir"
    stage_tree "$appdir"
    mkdir -p "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/256x256/apps"
    write_desktop "$appdir/usr/share/applications/yai.desktop"
    write_icon "$appdir/usr/share/icons/hicolor/256x256/apps/yai.png"

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
id: com.github.yai-byte.yai
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
    sources:
      - type: dir
        path: $rel
EOF

    flatpak-builder --repo="$mdir/repo" "$mdir/builddir" "$manifest" >/dev/null
    flatpak build-bundle "$mdir/repo" \
        "$DIST/yai-$ver.flatpak" com.github.yai-byte.yai
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

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
extract_version
mkdir -p "$DIST" "$BUILD"
log "version=$VERSION arch=$ARCH_RAW formats=${FORMATS[*]}"
build_binary

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
        tar.gz)   ok "dist/yai-$VERSION-$ARCH_RAW.tar.gz" ;;
        deb)      ok "dist/yai_${VERSION}_$(deb_arch "$ARCH_RAW").deb" ;;
        rpm)      ok "dist/yai-$VERSION-1.$ARCH_RAW.rpm" ;;
        appimage) ok "dist/yai-$VERSION-$ARCH_RAW.AppImage" ;;
        flatpak)  ok "dist/yai-$VERSION.flatpak" ;;
    esac
done
