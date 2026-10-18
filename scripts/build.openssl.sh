#!/bin/sh -e

# Compatibility entry point. OpenSSL 1.1.1 is end-of-life; keep old callers
# working while routing them to the supported OpenSSL 4 build.
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$SCRIPT_DIR/build.openssl4.sh" "$@"
