#!/usr/bin/env bash
# macOS double-click launcher for Docs Bridge
# Preserves GUI dialog prompting and passes through arguments
DIR="$(cd "$(dirname "$0")" && pwd)"
exec /bin/zsh "$DIR/one_click_docs.sh" "$@"