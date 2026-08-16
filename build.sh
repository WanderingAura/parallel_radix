#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: build.sh [release|debug]" >&2
    exit 1
fi

case "$1" in
    debug)
        FLAGS="-std=c++20 -g -O0"
        ;;
    release)
        FLAGS="-std=c++20 -O2 -DNDEBUG"
        ;;
    *)
        echo "Usage: build.sh [release|debug]" >&2
        exit 1
        ;;
esac

if ! command -v g++ >/dev/null 2>&1; then
    echo "g++ not found on PATH." >&2
    exit 1
fi

g++ $FLAGS -pthread parallel_radix_main.cpp -o parallel_radix_main
