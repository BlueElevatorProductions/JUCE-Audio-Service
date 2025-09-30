#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
SHORTCUT_B64="$DIR/Launch Audio Docs Bridge.shortcut.b64"
SHORTCUT_FILE="$DIR/Launch Audio Docs Bridge.shortcut"

echo "================================================"
echo "Launch Audio Docs Bridge - Shortcut Installer"
echo "================================================"
echo ""

if ! command -v shortcuts >/dev/null 2>&1; then
  echo "❌ The 'shortcuts' CLI is not available."
  echo ""
  echo "To install shortcuts manually:"
  echo "1. Open the Shortcuts app"
  echo "2. Click '+' to create a new shortcut"
  echo "3. Name it: 'Launch Audio Docs Bridge'"
  echo "4. Add these actions:"
  echo "   - Ask for Input (Text): 'Paste Google Doc link:'"
  echo "   - If: Provided Input contains 'docs.google.com' OR length >= 20"
  echo "   - Run Shell Script:"
  echo "       cd \"$DIR/../..\""
  echo "       ./scripts/one_click_docs.command --doc-url \"\$INPUT\""
  echo "   - Otherwise: Show Alert 'Invalid Google Doc URL'"
  echo ""
  echo "See README_docs_bridge.md for detailed instructions."
  exit 1
fi

if [[ ! -f "$SHORTCUT_B64" ]]; then
  echo "❌ Shortcut file not found: $SHORTCUT_B64"
  exit 1
fi

echo "Decoding shortcut from base64..."
base64 -d "$SHORTCUT_B64" > "$SHORTCUT_FILE"

echo "Importing shortcut via shortcuts CLI..."
shortcuts import "$SHORTCUT_FILE" --replace 2>/dev/null || true

echo ""
echo "✅ Shortcut installed successfully!"
echo ""
echo "You can now:"
echo "1. Find 'Launch Audio Docs Bridge' in Spotlight"
echo "2. Add it to your Dock or menu bar"
echo "3. Click to launch → paste Google Doc link"
echo ""

# Clean up temporary file
rm -f "$SHORTCUT_FILE"