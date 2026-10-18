#!/bin/sh -e

LIBRESSL_VERSION="4.3.3"
LIBRESSL_SHA256="ff97c432457f349e6ba3d416ab903bc7468f1436f0f32efe5fff808de292c7b8"

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
cd ..
