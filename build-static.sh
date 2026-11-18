#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

usage() {
    printf '%s\n' \
        'Usage: ./build-static.sh' \
        '' \
        'Build native static Linux CPU miner, daemon, and server binaries with OpenSSL/TLS.' \
        'Uses the installed C/C++ toolchain and memory-aware parallelism.' \
        'JOBS overrides the detected parallel job count (for example, JOBS=1).' \
        'BUILD_DIR selects the output directory (default: build).'
}

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 1 ;;
    esac
fi

if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' 'Run this script on the target Linux machine.' >&2
    exit 1
fi

detect_jobs() {
    cpu_jobs=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')
    case "$cpu_jobs" in
        ''|*[!0-9]*) cpu_jobs=1 ;;
        0) cpu_jobs=1 ;;
    esac

    memory_jobs=$cpu_jobs
    if [ -r /proc/meminfo ]; then
        available_kb=$(awk '/^MemAvailable:/ { print $2; exit }' /proc/meminfo)
        case "$available_kb" in
            ''|*[!0-9]*) ;;
            *)
                # Leave 1 GiB for the OS and existing services, then budget
                # 1.5 GiB for each compiler/linker job. This deliberately
                # favors reliability on Raspberry Pi systems.
                if [ "$available_kb" -le 1048576 ]; then
                    memory_jobs=1
                else
                    memory_jobs=$(( (available_kb - 1048576) / 1572864 ))
                    [ "$memory_jobs" -ge 1 ] || memory_jobs=1
                fi
                ;;
        esac
    fi

    if [ "$memory_jobs" -lt "$cpu_jobs" ]; then
        printf '%s\n' "$memory_jobs"
    else
        printf '%s\n' "$cpu_jobs"
    fi
}

if [ -n "${JOBS:-}" ]; then
    case "$JOBS" in
        ''|*[!0-9]*|0) printf 'JOBS must be a positive integer.\n' >&2; exit 1 ;;
    esac
else
    JOBS=$(detect_jobs)
fi
export JOBS

printf 'Using %s parallel build job(s). Override with JOBS=<count>.\n' "$JOBS"
if [ -r /proc/meminfo ]; then
    AVAILABLE_MEMORY_KB=$(awk '/^MemAvailable:/ { print $2; exit }' /proc/meminfo)
    case "$AVAILABLE_MEMORY_KB" in
        ''|*[!0-9]*) ;;
        *)
            if [ "$AVAILABLE_MEMORY_KB" -lt 2097152 ]; then
                printf '%s\n' \
                    'Warning: less than 2 GiB is available. Stop active miners or enable swap if the build is killed.' >&2
            fi
            ;;
    esac
fi

BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$SCRIPT_DIR/$BUILD_DIR" ;;
esac

for command in cmake make perl patch readelf; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Required build tool not found: %s\n' "$command" >&2
        exit 1
    fi
done

printf '\nStage 1/3: Building dependencies\n'
"$SCRIPT_DIR/scripts/build_deps.sh"

printf '\nStage 2/3: Building the miner, daemon, and server\n'
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY:PATH="$BUILD_DIR" \
    -DXMRIG_DEPS:PATH="$SCRIPT_DIR/scripts/deps" \
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
cmake --build "$BUILD_DIR" --parallel "$JOBS"

printf '\nStage 3/3: Verifying static binaries\n'
for name in xmrigMiner xmrigDaemon xmrigServer; do
    binary="$BUILD_DIR/$name"
    program_headers=$(readelf -lW "$binary")
    dynamic_section=$(readelf -dW "$binary")
    case "$program_headers" in
        *INTERP*) printf 'Static verification failed: %s has a dynamic interpreter.\n' "$name" >&2; exit 1 ;;
        *' RWE '*) printf 'Static verification failed: %s has a writable executable segment.\n' "$name" >&2; exit 1 ;;
    esac
    case "$dynamic_section" in
        *'(NEEDED)'*) printf 'Static verification failed: %s requires shared libraries.\n' "$name" >&2; exit 1 ;;
        *TEXTREL*) printf 'Static verification failed: %s contains text relocations.\n' "$name" >&2; exit 1 ;;
    esac
    printf 'Verified static binary: %s\n' "$binary"
done

"$BUILD_DIR/xmrigMiner" --version
printf '\nStatic binaries are in %s\n' "$BUILD_DIR"
