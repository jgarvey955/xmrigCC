#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/build"
exec ./xmrigDaemon --config ../config-zecnero-testnet.json
