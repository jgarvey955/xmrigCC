#!/bin/sh -e

HWLOC_VERSION="1.11.13"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS_DIR="$SCRIPT_DIR/deps"
. "$SCRIPT_DIR/dependency-cache.sh"

if dependency_is_current hwloc "$HWLOC_VERSION" "$DEPS_DIR/include/hwloc.h" "$DEPS_DIR/lib/libhwloc.a"; then
    exit 0
fi

cd "$SCRIPT_DIR"

mkdir -p deps
mkdir -p deps/include
mkdir -p deps/lib

mkdir -p build && cd build

wget -4 https://download.open-mpi.org/release/hwloc/v1.11/hwloc-${HWLOC_VERSION}.tar.gz -O hwloc-${HWLOC_VERSION}.tar.gz
tar -xzf hwloc-${HWLOC_VERSION}.tar.gz

cd hwloc-${HWLOC_VERSION}
./configure --disable-shared --enable-static --disable-io --disable-libudev --disable-libxml2
make -j$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)
cp -fr include ../../deps
cp src/.libs/libhwloc.a ../../deps/lib
dependency_mark_current hwloc "$HWLOC_VERSION"
cd ..
