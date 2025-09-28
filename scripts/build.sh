#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE=${1:-Debug}
BUILD_DIR="build/${BUILD_TYPE}"

cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}"
