#!/bin/sh -e

UV_VERSION="1.53.0"
UV_SHA256="cb0d6dd2128d5a95bd242c6cc982a24fe608fa93da57b6b4ec763b0018c53e64"

verify_sha256() {
    actual=$(sha256sum "$1" | awk '{print $1}')
    [ "$actual" = "$2" ] || { printf 'SHA-256 mismatch for %s\n' "$1" >&2; exit 1; }
}

mkdir -p deps
mkdir -p deps/include
mkdir -p deps/lib

mkdir -p build && cd build

wget -4 --tries=3 https://dist.libuv.org/dist/v${UV_VERSION}/libuv-v${UV_VERSION}.tar.gz -O v${UV_VERSION}.tar.gz
verify_sha256 "v${UV_VERSION}.tar.gz" "$UV_SHA256"
rm -rf -- "libuv-v${UV_VERSION}"
tar -xzf v${UV_VERSION}.tar.gz

cmake -S "libuv-v${UV_VERSION}" -B "libuv-v${UV_VERSION}/cmake-build" \
    -DCMAKE_BUILD_TYPE=Release -DLIBUV_BUILD_SHARED=OFF -DBUILD_TESTING=OFF
cmake --build "libuv-v${UV_VERSION}/cmake-build" --parallel "${JOBS:-$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)}"
cp -R "libuv-v${UV_VERSION}/include/." ../deps/include/
cp "libuv-v${UV_VERSION}/cmake-build/libuv.a" ../deps/lib/
printf '[libuv 100%%] Build complete\n'
