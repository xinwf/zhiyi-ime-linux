#!/usr/bin/env bash
# Fetch the Fcitx 4 development files without root privileges.
#
# The runtime libraries are already installed on a normal Ubuntu/Fcitx system;
# only headers and unversioned .so link stubs are missing. apt-get download
# works as an unprivileged user, so we unpack them into .deps/sysroot and point
# CMake at that tree.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBS="$ROOT/.deps/debs"
SYSROOT="$ROOT/.deps/sysroot"
mkdir -p "$DEBS" "$SYSROOT"

PACKAGES=(
    fcitx-libs-dev
    # Runtime soname libraries: without them the addon and the ABI test
    # cannot link on machines where fcitx4 itself is not installed (the dev
    # packages only ship unversioned linker stubs).
    libfcitx-config4
    libfcitx-core0
    libfcitx-utils0
    # Fcitx 5 SDK (used by the fcitx5 front end when the distro dev packages
    # are not installed; harmless to fetch alongside fcitx4). The CMake
    # package configs reference the versioned runtime libraries, so the
    # runtime soname packages must be unpacked alongside the dev ones.
    libfcitx5core-dev
    libfcitx5utils-dev
    libfcitx5config-dev
    fcitx5-modules-dev
    libfcitx5core7
    libfcitx5utils2
    libfcitx5config6
)

echo "Downloading Fcitx SDK packages into $DEBS ..."
(cd "$DEBS" && apt-get download "${PACKAGES[@]}")

echo "Unpacking into $SYSROOT ..."
for deb in "$DEBS"/*.deb; do
    dpkg-deb -x "$deb" "$SYSROOT"
done

# The dev packages ship unversioned libfcitx-core.so -> libfcitx-core.so.0
# symlinks, but the versioned runtime libraries are not unpacked here. Point
# them at the already-installed system libraries so the linker can resolve
# -lfcitx-core without needing root or a copy of the runtime .so files.
LIBDIR="$SYSROOT/usr/lib/$(gcc -dumpmachine)"
SYSLIBDIR="/usr/lib/$(gcc -dumpmachine)"
if [ -d "$LIBDIR" ]; then
    for soname in libfcitx-core.so.0 libfcitx-config.so.4 \
                  libfcitx-utils.so.0 libfcitx-gclient.so.1; do
        if [ -e "$SYSLIBDIR/$soname" ]; then
            ln -sfn "$SYSLIBDIR/$soname" "$LIBDIR/$soname"
        fi
    done
fi

if [ ! -f "$SYSROOT/usr/include/fcitx/ime.h" ]; then
    echo "ERROR: fcitx/ime.h missing after unpack" >&2
    exit 1
fi
echo "OK: Fcitx 4 headers and link stubs are in $SYSROOT"
