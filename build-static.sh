#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

usage() {
    printf '%s\n' \
        'Usage: ./build-static.sh' \
        '' \
        'Build the bundled dependencies and static Linux CPU miner, daemon, and server with TLS.' \
        'Uses all available CPU cores for dependency and application builds.' \
        'BUILD_DIR selects the output directory (default: build-static).'
}

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 1 ;;
    esac
fi

if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' 'This script builds native Linux binaries. Run it on the target Linux machine.' >&2
    exit 1
fi

# Always use all available CPU cores for dependencies and applications.
JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN)
export JOBS

BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build-static}"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$SCRIPT_DIR/$BUILD_DIR" ;;
esac

for command in cmake make perl readelf; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Required build tool not found: %s\n' "$command" >&2
        exit 1
    fi
done

"$SCRIPT_DIR/scripts/build_deps.sh"

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DXMRIG_DEPS:PATH="$SCRIPT_DIR/scripts/deps" \
    -DBUILD_STATIC=ON \
    -DWITH_TLS=ON \
    -DWITH_HWLOC=ON \
    -DWITH_ZLIB=ON \
    -DWITH_CC_CLIENT=ON \
    -DWITH_CC_SERVER=ON \
    -DMINER_EXECUTABLE_NAME=xmrigMiner \
    -DDAEMON_EXECUTABLE_NAME=xmrigDaemon
cmake --build "$BUILD_DIR" --parallel "$JOBS"

for name in xmrigMiner xmrigDaemon xmrigServer; do
    binary="$BUILD_DIR/$name"
    program_headers=$(readelf -lW "$binary")
    dynamic_section=$(readelf -dW "$binary")
    case "$program_headers" in
        *INTERP*) printf 'Static verification failed: %s has a dynamic interpreter.\n' "$name" >&2; exit 1 ;;
    esac
    case "$dynamic_section" in
        *'(NEEDED)'*) printf 'Static verification failed: %s requires shared libraries.\n' "$name" >&2; exit 1 ;;
    esac
    printf 'Verified static binary: %s\n' "$binary"
done

"$BUILD_DIR/xmrigMiner" --version
