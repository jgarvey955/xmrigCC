#!/bin/sh -e

UV_VERSION="1.53.0"

mkdir -p deps
mkdir -p deps/include
mkdir -p deps/lib

mkdir -p build && cd build

wget  -4 https://dist.libuv.org/dist/v${UV_VERSION}/libuv-v${UV_VERSION}.tar.gz -O v${UV_VERSION}.tar.gz
tar -xzf v${UV_VERSION}.tar.gz

cmake -S "libuv-v${UV_VERSION}" -B "libuv-v${UV_VERSION}/cmake-build" \
    -DCMAKE_BUILD_TYPE=Release -DLIBUV_BUILD_SHARED=OFF -DBUILD_TESTING=OFF
cmake --build "libuv-v${UV_VERSION}/cmake-build" --parallel "$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)"
cp -R "libuv-v${UV_VERSION}/include/." ../deps/include/
cp "libuv-v${UV_VERSION}/cmake-build/libuv.a" ../deps/lib/
