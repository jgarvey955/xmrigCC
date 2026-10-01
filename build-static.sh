#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

usage() {
    printf '%s\n' \
        'Usage: ./build-static.sh' \
        '' \
        'Build static Linux CPU miner, daemon, and server binaries with OpenSSL/TLS using musl.' \
        'Requires Docker or Podman; uses all available CPU cores.' \
        'BUILD_DIR selects the output directory (default: build-static).' \
        'The resulting binaries run directly on Linux without a container.'
}

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 1 ;;
    esac
fi

if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' 'Run this script on the target Linux architecture.' >&2
    exit 1
fi

if command -v docker >/dev/null 2>&1; then
    engine=docker
elif command -v podman >/dev/null 2>&1; then
    engine=podman
else
    printf '%s\n' 'Install Docker or Podman to build static binaries with musl.' >&2
    exit 1
fi

if ! "$engine" info >/dev/null 2>&1; then
    printf 'Cannot access %s. Start it and ensure your user has permission to run it.\n' "$engine" >&2
    exit 1
fi

BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build-static}"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$SCRIPT_DIR/$BUILD_DIR" ;;
esac
mkdir -p "$BUILD_DIR"
BUILD_DIR=$(CDPATH= cd -- "$BUILD_DIR" && pwd)

builder_image=xmrigcc-static-builder:alpine-3.24.2
"$engine" build --file "$SCRIPT_DIR/scripts/Dockerfile.static" \
    --tag "$builder_image" "$SCRIPT_DIR/scripts"

# A separate CMake directory prevents reuse of any earlier glibc build cache.
set --
if [ "$engine" = podman ]; then
    set -- --userns=keep-id
fi
"$engine" run --rm "$@" --user "$(id -u):$(id -g)" \
    --mount "type=bind,source=$SCRIPT_DIR,target=/source,readonly" \
    --mount "type=bind,source=$BUILD_DIR,target=/output" \
    --workdir /output "$builder_image" \
    /bin/sh /source/scripts/build-static-container.sh

printf '\nStatic binaries are in %s\n' "$BUILD_DIR"
