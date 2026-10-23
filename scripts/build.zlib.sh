#!/bin/sh -e

ZLIB_VERSION="1.3.2"
ZLIB_SHA256="bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS_DIR="$SCRIPT_DIR/deps"
. "$SCRIPT_DIR/dependency-cache.sh"

if dependency_is_current zlib "$ZLIB_VERSION" "$DEPS_DIR/include/zlib.h" "$DEPS_DIR/lib/libz.a"; then
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

wget -4 --tries=3 https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/zlib-${ZLIB_VERSION}.tar.gz -O zlib-${ZLIB_VERSION}.tar.gz
verify_sha256 "zlib-${ZLIB_VERSION}.tar.gz" "$ZLIB_SHA256"
rm -rf -- "zlib-${ZLIB_VERSION}"
tar -xzf zlib-${ZLIB_VERSION}.tar.gz

cd zlib-${ZLIB_VERSION}
./configure --static
make -j"${JOBS:-$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)}"
cp -fr zlib.h zconf.h ../../deps/include
cp libz.a ../../deps/lib
dependency_mark_current zlib "$ZLIB_VERSION"
cd ..
