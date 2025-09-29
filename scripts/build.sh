#!/usr/bin/env bash
set -euo pipefail

cfg="${1:-Release}"
shift || true

# Source Apple Silicon environment and vcpkg configuration
source "$(dirname "$0")/vcpkg_env.sh"

# Ensure dependencies are available
"$(dirname "$0")/get_deps.sh"

cmake_args=(
  -S .
  -B build
  -DCMAKE_BUILD_TYPE="$cfg"
  -DCMAKE_OSX_ARCHITECTURES="$CMAKE_OSX_ARCHITECTURES"
)

# If caller passed -DENABLE_GRPC=ON anywhere, we provide the toolchain
if printf '%s\n' "$@" | grep -q 'ENABLE_GRPC=ON'; then
  cmake_args+=(-DCMAKE_TOOLCHAIN_FILE=third_party/vcpkg/scripts/buildsystems/vcpkg.cmake)
fi

cmake "${cmake_args[@]}" "$@"
cmake --build build --config "$cfg" --parallel
