#!/bin/sh -e

LIBRESSL_VERSION="4.4.0"
LIBRESSL_SHA256="7d02522ad5070b9adb32eb253ecabce181184a3750cf65abac0675ee908687c1"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS_DIR="$SCRIPT_DIR/deps"
. "$SCRIPT_DIR/dependency-cache.sh"

if dependency_is_current tls "libressl-$LIBRESSL_VERSION" "$DEPS_DIR/include/openssl/ssl.h" "$DEPS_DIR/lib/libssl.a" "$DEPS_DIR/lib/libcrypto.a"; then
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

wget -4 --tries=3 https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-${LIBRESSL_VERSION}.tar.gz -O libressl-${LIBRESSL_VERSION}.tar.gz
verify_sha256 "libressl-${LIBRESSL_VERSION}.tar.gz" "$LIBRESSL_SHA256"
rm -rf -- "libressl-${LIBRESSL_VERSION}"
tar -xzf libressl-${LIBRESSL_VERSION}.tar.gz

cd libressl-${LIBRESSL_VERSION}
./configure --disable-shared
make -j$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)
cp -fr include ../../deps
cp crypto/.libs/libcrypto.a ../../deps/lib
cp ssl/.libs/libssl.a ../../deps/lib
dependency_mark_current tls "libressl-$LIBRESSL_VERSION"
cd ..
