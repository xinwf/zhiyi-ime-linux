#!/usr/bin/env bash
# Build, test and package zhiyi-ime as a .deb (no root privileges needed).
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
         "$ROOT/scripts/zhiyi-config" "$ROOT/scripts/fetch-sdk.sh"

if [ ! -f "$ROOT/.deps/sysroot/usr/include/fcitx/ime.h" ]; then
    echo "Fcitx 4 SDK missing, fetching development packages ..."
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

VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD_DIR/CMakeCache.txt")"
ARCH="$(dpkg --print-architecture)"
SIZE="$(du -sk "$STAGE/usr" | cut -f1)"
sed -e "s/@PROJECT_VERSION@/$VERSION/g" \
    -e "s/@ZHIYI_DEB_ARCH@/$ARCH/g" \
    -e "s/@ZHIYI_INSTALLED_SIZE@/$SIZE/g" \
    "$ROOT/packaging/control.in" > "$STAGE/DEBIAN/control"
cp "$ROOT/packaging/postinst" "$ROOT/packaging/postrm" "$STAGE/DEBIAN/"
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

echo "==> dpkg-deb (gzip level 1)"
DEB="$DIST_DIR/zhiyi-ime_${VERSION}_${ARCH}.deb"
dpkg-deb -b -Zgzip -z1 "$STAGE" "$DEB"

echo "==> verify"
dpkg-deb -I "$DEB" | head -18
echo
dpkg-deb -c "$DEB" | grep -E 'fcitx-zhiyi.so|zhiyi.dict.bin|zhiyi.conf|control|postinst' | head -12
echo
ls -lh "$DEB"
echo
echo "安装： sudo dpkg -i $DEB && fcitx -r"
