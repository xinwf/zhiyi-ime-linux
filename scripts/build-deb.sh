#!/usr/bin/env bash
# Build, test and package zhiyi-ime as a .deb (no root privileges needed).
#
# The same source tree ships both input method front ends: CMake auto-detects
# which frameworks (fcitx4 / fcitx5) the host provides and builds the matching
# addons, so the produced package works on whatever the build machine offers.
#
# Packaging is deliberately NOT done with CPack: its default xz compression
# spends a very long time on the ~46 MiB binary dictionaries. Instead we
# install into a staging tree and call dpkg-deb with gzip level 1, which is
# fast and fully under our control.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/deb}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
STAGE="$BUILD_DIR/stage"
JOBS="${JOBS:-$(nproc)}"

chmod +x "$ROOT/packaging/postinst" "$ROOT/packaging/postrm" \
         "$ROOT/scripts/zhiyi-config" "$ROOT/scripts/zhiyi-tray" \
         "$ROOT/scripts/fetch-sdk.sh" "$ROOT/scripts/generate_fcitx5_themes.py"

if [ ! -f "$ROOT/.deps/sysroot/usr/include/fcitx/ime.h" ]; then
    echo "Fcitx SDK missing, fetching development packages ..."
    http_proxy="${http_proxy:-http://127.0.0.1:20172}" \
    https_proxy="${https_proxy:-http://127.0.0.1:20172}" \
        bash "$ROOT/scripts/fetch-sdk.sh"
fi

echo "==> configure"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

echo "==> build"
cmake --build "$BUILD_DIR" -j"$JOBS"

echo "==> test"
(cd "$BUILD_DIR" && ctest --output-on-failure)

echo "==> stage install"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$DIST_DIR"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR" >/dev/null

# Depends 由实际打包出来的前端决定：双前端 → 二选一；单前端 → 对应框架
FCITX4_ON=0
FCITX5_ON=0
[ -n "$(find "$STAGE/usr" -name 'fcitx-zhiyi.so' -print -quit)" ] && FCITX4_ON=1
[ -n "$(find "$STAGE/usr" -name 'libzhiyi.so' -print -quit)" ] && FCITX5_ON=1
echo "==> front ends: fcitx4=$FCITX4_ON fcitx5=$FCITX5_ON"
if [ "$FCITX4_ON" = "1" ] && [ "$FCITX5_ON" = "1" ]; then
    FRAMEWORK_DEPS="fcitx-bin (>= 4.2.9) | fcitx5 (>= 5.0.5)"
elif [ "$FCITX5_ON" = "1" ]; then
    FRAMEWORK_DEPS="fcitx5 (>= 5.0.5)"
else
    FRAMEWORK_DEPS="fcitx-bin (>= 4.2.9), fcitx-modules"
fi

VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD_DIR/CMakeCache.txt")"
ARCH="$(dpkg --print-architecture)"
SIZE="$(du -sk "$STAGE/usr" | cut -f1)"
sed -e "s/@PROJECT_VERSION@/$VERSION/g" \
    -e "s/@ZHIYI_DEB_ARCH@/$ARCH/g" \
    -e "s/@ZHIYI_INSTALLED_SIZE@/$SIZE/g" \
    -e "s/@ZHIYI_FRAMEWORK_DEPS@/$FRAMEWORK_DEPS/g" \
    "$ROOT/packaging/control.in" > "$STAGE/DEBIAN/control"
cp "$ROOT/packaging/postinst" "$ROOT/packaging/postrm" "$STAGE/DEBIAN/"
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

echo "==> dpkg-deb (gzip level 1)"
DEB="$DIST_DIR/zhiyi-ime_${VERSION}_${ARCH}.deb"
dpkg-deb -b -Zgzip -z1 "$STAGE" "$DEB"

echo "==> verify"
DEB_INFO="$(dpkg-deb -I "$DEB")"
echo "$DEB_INFO" | sed -n '1,20p'
echo
DEB_FILES="$(dpkg-deb -c "$DEB")"
echo "$DEB_FILES" | grep -E 'zhiyi.so|fcitx-zhiyi.so|zhiyi.dict.bin|zhiyi.conf|control|postinst' | head -14
echo
ls -lh "$DEB"
echo
if [ "$FCITX5_ON" = "1" ] && [ "$FCITX4_ON" = "1" ]; then
    echo "安装： sudo dpkg -i $DEB && (fcitx5 -r 或 fcitx -r)"
elif [ "$FCITX5_ON" = "1" ]; then
    echo "安装： sudo dpkg -i $DEB && fcitx5 -r"
else
    echo "安装： sudo dpkg -i $DEB && fcitx -r"
fi
