#!/bin/sh -e

HWLOC_VERSION_MAJOR="2"
HWLOC_VERSION_MINOR="15"
HWLOC_VERSION_PATCH="0"

HWLOC_VERSION="${HWLOC_VERSION_MAJOR}.${HWLOC_VERSION_MINOR}.${HWLOC_VERSION_PATCH}"
HWLOC_SHA256="f54bbdfc9f8015308c2e7bbab3e7789e47436a4a016689e5749a0c35d6877619"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS_DIR="$SCRIPT_DIR/deps"
. "$SCRIPT_DIR/dependency-cache.sh"

if dependency_is_current hwloc "$HWLOC_VERSION" "$DEPS_DIR/include/hwloc.h" "$DEPS_DIR/lib/libhwloc.a"; then
    exit 0
fi

cd "$SCRIPT_DIR"

verify_sha256() {
    actual=$(sha256sum "$1" | awk '{print $1}')
    [ "$actual" = "$2" ] || { printf 'SHA-256 mismatch for %s\n' "$1" >&2; exit 1; }
}

mkdir -p deps
mkdir -p deps/include
mkdir -p deps/lib

mkdir -p build && cd build

wget -4 --tries=3 https://download.open-mpi.org/release/hwloc/v${HWLOC_VERSION_MAJOR}.${HWLOC_VERSION_MINOR}/hwloc-${HWLOC_VERSION}.tar.gz -O hwloc-${HWLOC_VERSION}.tar.gz
verify_sha256 "hwloc-${HWLOC_VERSION}.tar.gz" "$HWLOC_SHA256"
rm -rf -- "hwloc-${HWLOC_VERSION}"
tar -xzf hwloc-${HWLOC_VERSION}.tar.gz

cd hwloc-${HWLOC_VERSION}
# Keep hwloc's successful visibility-flag check as a notice, preserving the flag.
patch -p1 < ../../hwloc-visibility-notice.patch
./configure --disable-shared --enable-static --disable-io --disable-libudev --disable-libxml2
make -j"${JOBS:-$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)}"
cp -fr include ../../deps
cp hwloc/.libs/libhwloc.a ../../deps/lib
dependency_mark_current hwloc "$HWLOC_VERSION"
cd ..
