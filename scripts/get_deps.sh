#!/usr/bin/env bash
set -euo pipefail

# Default JUCE version
JUCE_TAG=${JUCE_TAG:-7.0.12}
JUCE_DIR="third_party/JUCE"

echo "=== JUCE Dependency Setup ==="

# Check if JUCE directory exists and has content
if [ -d "${JUCE_DIR}" ] && [ -f "${JUCE_DIR}/CMakeLists.txt" ]; then
    echo "✓ JUCE found at ${JUCE_DIR}"

    # Print the current JUCE commit for verification
    if [ -d "${JUCE_DIR}/.git" ]; then
        JUCE_COMMIT=$(git -C "${JUCE_DIR}" rev-parse HEAD 2>/dev/null || echo "unknown")
        JUCE_VERSION=$(git -C "${JUCE_DIR}" describe --tags --exact-match 2>/dev/null || echo "unknown")
        echo "  Version: ${JUCE_VERSION}"
        echo "  Commit: ${JUCE_COMMIT}"
    else
        echo "  Source: Manual installation"
    fi
else
    echo "⚠ JUCE not found at ${JUCE_DIR}"
    echo "Attempting to clone JUCE ${JUCE_TAG}..."

    # Create third_party directory if it doesn't exist
    mkdir -p third_party

    # Clone JUCE at the specific tag
    if git clone --depth 1 --branch "${JUCE_TAG}" https://github.com/juce-framework/JUCE.git "${JUCE_DIR}"; then
        echo "✓ Successfully cloned JUCE ${JUCE_TAG}"

        # Print commit info
        JUCE_COMMIT=$(git -C "${JUCE_DIR}" rev-parse HEAD)
        echo "  Commit: ${JUCE_COMMIT}"
    else
        echo "❌ Failed to clone JUCE"
        echo "Please check your network connection or manually install JUCE to ${JUCE_DIR}"
        echo "See docs/deps.md for manual installation instructions"
        exit 1
    fi
fi

echo "=== Dependencies ready ==="