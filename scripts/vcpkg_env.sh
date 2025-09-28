#!/usr/bin/env bash
set -euo pipefail

# Apple Silicon only
export CMAKE_OSX_ARCHITECTURES=arm64
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-12.0}

# vcpkg triplets
export VCPKG_TARGET_TRIPLET=arm64-osx
export VCPKG_HOST_TRIPLET=arm64-osx
export VCPKG_BUILD_TYPE=release   # single-config to keep cache small

# Local file binary cache
export VCPKG_DEFAULT_BINARY_CACHE="$(pwd)/third_party/vcpkg_cache"
mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"

# Only use our file cache (readwrite locally; CI can read)
export VCPKG_BINARY_SOURCES="clear;files,$VCPKG_DEFAULT_BINARY_CACHE,readwrite"