#!/bin/sh -e

OPENSSL_VERSION="4.1.0"
OPENSSL_SHA256="50c17baf54cb008f6ce42d50deb4b28050a1aad711636cabd4b241c7e90cc2fd"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS_DIR="$SCRIPT_DIR/deps"
. "$SCRIPT_DIR/dependency-cache.sh"

if dependency_is_current tls "openssl-$OPENSSL_VERSION" "$DEPS_DIR/include/openssl/ssl.h" "$DEPS_DIR/lib/libssl.a" "$DEPS_DIR/lib/libcrypto.a"; then
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
dependency_mark_current tls "openssl-$OPENSSL_VERSION"
cd ..
