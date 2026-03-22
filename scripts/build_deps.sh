#!/bin/sh -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

cd "$SCRIPT_DIR"

./build.uv.sh
./build.hwloc.sh
./build.openssl3.sh
./build.zlib.sh
