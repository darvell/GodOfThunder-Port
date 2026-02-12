#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"

exec ./build/native/got_raylib "$@"
