#!/bin/sh -e

OPENSSL_VERSION="4.0.3"
OPENSSL_SHA256="325b5c806167c13b40b1ffeadfe0248197c00eccc4cf123ec1e28d2d2fd216d9"

verify_sha256() {
    actual=$(sha256sum "$1" | awk '{print $1}')
    [ "$actual" = "$2" ] || { printf 'SHA-256 mismatch for %s\n' "$1" >&2; exit 1; }
}

mkdir -p deps
mkdir -p deps/include
mkdir -p deps/lib

mkdir -p build && cd build

wget -4 --tries=3 https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz -O openssl-${OPENSSL_VERSION}.tar.gz
verify_sha256 "openssl-${OPENSSL_VERSION}.tar.gz" "$OPENSSL_SHA256"
rm -rf openssl-${OPENSSL_VERSION}
tar -xzf openssl-${OPENSSL_VERSION}.tar.gz

cd openssl-${OPENSSL_VERSION}
./config -no-shared -no-zlib -no-comp -no-dgram -no-filenames -no-cms no-jitter no-fips-jitter no-tests
make -j"${JOBS:-$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)}" build_libs
cp -fr include ../../deps
cp libcrypto.a ../../deps/lib
cp libssl.a ../../deps/lib
cd ..
