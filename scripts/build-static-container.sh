#!/bin/sh
# Invoked by the repository's build-static.sh inside its musl build image.
set -eu

JOBS=$(nproc)
# Count completed build tasks, including linking, rather than tasks started.
export NINJA_STATUS='[compile %p | %f/%t completed] '
cmake -S /source -B /output/musl -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/output \
    -DXMRIG_DEPS:PATH=/opt/dependency-scripts/deps \
    -DBUILD_STATIC=ON \
    -DWITH_TLS=ON \
    -DWITH_HWLOC=ON \
    -DWITH_ZLIB=ON \
    -DWITH_CC_CLIENT=ON \
    -DWITH_CC_SERVER=ON \
    -DWITH_OPENCL=OFF \
    -DWITH_CUDA=OFF \
    -DMINER_EXECUTABLE_NAME=xmrigMiner \
    -DDAEMON_EXECUTABLE_NAME=xmrigDaemon
cmake --build /output/musl --parallel "$JOBS"
printf '[compile 100%%] Build complete\n'

printf '\nStage 3/3: Verifying static binaries\n'
for name in xmrigMiner xmrigDaemon xmrigServer; do
    binary="/output/$name"
    program_headers=$(readelf -lW "$binary")
    dynamic_section=$(readelf -dW "$binary")
    case "$program_headers" in
        *INTERP*) printf 'Static verification failed: %s has a dynamic interpreter.\n' "$name" >&2; exit 1 ;;
    esac
    case "$dynamic_section" in
        *'(NEEDED)'*) printf 'Static verification failed: %s requires shared libraries.\n' "$name" >&2; exit 1 ;;
    esac
    printf 'Verified static binary: %s\n' "$name"
done

"/output/xmrigMiner" --version
