#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

cat > "$TMP_DIR/arch_test.cpp" <<'CPP'
#include "yai.hpp"

#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main() {
    require(normalize_arch("x86_64") == "x86_64", "x86_64 normalization failed");
    require(normalize_arch("AMD64") == "x86_64", "amd64 normalization failed");
    require(normalize_arch("x64") == "x86_64", "x64 normalization failed");
    require(normalize_arch("aarch64") == "aarch64", "aarch64 normalization failed");
    require(normalize_arch("arm64") == "aarch64", "arm64 normalization failed");
    require(normalize_arch("i686") == "x86", "i686 normalization failed");
    require(normalize_arch("armhf") == "armv7", "armhf normalization failed");
    require(normalize_arch("armv7l") == "armv7", "armv7l normalization failed");
    require(normalize_arch("ppc64el") == "ppc64le", "ppc64el normalization failed");
    require(normalize_arch("powerpc64le") == "ppc64le", "powerpc64le normalization failed");
    require(normalize_arch("riscv64") == "riscv64", "riscv64 normalization failed");
    require(normalize_arch("s390x") == "s390x", "s390x normalization failed");
    require(normalize_arch("loongarch64") == "loongarch64", "loongarch64 normalization failed");
    require(normalize_arch("  ") == "unknown", "empty normalization failed");
    require(is_supported_arch("armhf"), "armhf support check failed");
    require(is_supported_arch("loongarch64"), "loongarch64 support check failed");
    require(!is_supported_arch("mips64"), "mips64 support check failed");

    require(appimage_asset_score("Demo-x86_64.AppImage", "amd64") == 100, "amd64 asset score failed");
    require(appimage_asset_score("Demo-arm64.AppImage", "aarch64") == 100, "aarch64 asset score failed");
    require(appimage_asset_score("Demo-arm64.AppImage", "x86_64") == -1, "x86_64 rejected arm64 failed");
    require(appimage_asset_score("Demo-i686.AppImage", "x86_64") == -1, "x86_64 rejected x86 failed");
    require(appimage_asset_score("Demo-riscv64.AppImage", "riscv64") == 100, "riscv64 asset score failed");
    require(appimage_asset_score("Demo-ppc64el.AppImage", "ppc64le") == 100, "ppc64le asset score failed");
    require(appimage_asset_score("Demo-s390x.AppImage", "s390x") == 100, "s390x asset score failed");
    require(appimage_asset_score("Demo-loongarch64.AppImage", "loongarch64") == 100, "loongarch64 asset score failed");
    require(appimage_asset_score("Demo-armhf.AppImage", "armv7") == 100, "armv7 asset score failed");
    require(appimage_asset_score("Demo-riscv64.AppImage", "x86_64") == -1, "x86_64 rejected riscv64 failed");
    require(appimage_asset_score("Demo.AppImage", "riscv64") == 20, "riscv64 generic score failed");
    require(appimage_asset_score("Demo-x86_64.AppImage", "unknown") == -1, "unknown rejected specific arch failed");

    std::cout << "arch smoke test passed\n";
    return 0;
}
CPP

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -I"$ROOT/src" \
  -o "$TMP_DIR/arch_test" \
  "$TMP_DIR/arch_test.cpp" "$ROOT/src/arch.cpp" "$ROOT/src/core.cpp" \
  "$ROOT/src/i18n.cpp" "$ROOT/src/process.cpp" \
  "$ROOT/src/download_progress.cpp"

"$TMP_DIR/arch_test"
