#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE=${1:-Debug}
SCRIPT_DIR=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=${SCRIPT_DIR%/scripts}

"${SCRIPT_DIR}/build.sh" "${BUILD_TYPE}"

ctest --test-dir "${PROJECT_ROOT}/build/${BUILD_TYPE}" --output-on-failure -C "${BUILD_TYPE}"
