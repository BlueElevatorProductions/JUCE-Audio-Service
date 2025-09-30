#!/usr/bin/env zsh
set -euo pipefail

# ===== User-tweakable defaults =====
: ${PORT:=50051}
: ${BUILD_TYPE:=Release}
: ${JOBS:=8}
: ${BIT_DEPTH:=16}
: ${RENDER_DUR_SEC:=0.25}
: ${BRIDGE_PORT:=8080}
: ${PY_STUB_DIR:=tools/docs_bridge/stubs}
: ${HEALTH_URL:=http://127.0.0.1:8080/health}
# ===================================

SCRIPT_DIR="${0:A:h}"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
OUT_DIR="$REPO_ROOT/out"
RUN_DIR="$REPO_ROOT/.run"
VENV_DIR="$REPO_ROOT/.venv"
SERVER_LOG="$RUN_DIR/server.log"
BRIDGE_LOG="$RUN_DIR/bridge.log"
SERVER_PID="$RUN_DIR/server.pid"
BRIDGE_PID="$RUN_DIR/bridge.pid"

VCPKG_TC="$REPO_ROOT/third_party/vcpkg/scripts/buildsystems/vcpkg.cmake"

bold() { print -P "\n%F{cyan}%B$1%b%f"; }
ok()   { print -P "%F{green}%B$1%b%f"; }
warn() { print -P "%F{yellow}%B$1%b%f"; }
err()  { print -P "%F{red}%B$1%b%f"; }

cleanup() {
  if [[ -f "$BRIDGE_PID" ]]; then
    if kill -0 "$(cat "$BRIDGE_PID")" 2>/dev/null; then
      kill "$(cat "$BRIDGE_PID")" 2>/dev/null || true
      sleep 0.3 || true
    fi
    rm -f "$BRIDGE_PID"
  fi
  if [[ -f "$SERVER_PID" ]]; then
    if kill -0 "$(cat "$SERVER_PID")" 2>/dev/null; then
      kill "$(cat "$SERVER_PID")" 2>/dev/null || true
      sleep 0.3 || true
    fi
    rm -f "$SERVER_PID"
  fi
}
trap cleanup EXIT INT TERM

mkdir -p "$RUN_DIR" "$OUT_DIR" "$PY_STUB_DIR"
: > "$SERVER_LOG"
: > "$BRIDGE_LOG"

# ===== Parse Doc ID =====
parse_doc_id() {
  local input="$1"
  local id=""

  # Try extracting from URL pattern /d/{ID}/
  if [[ "$input" =~ /d/([A-Za-z0-9_-]+) ]]; then
    id="${match[1]}"
  # If input looks like a raw ID (20+ chars, base62)
  elif [[ "$input" =~ ^[A-Za-z0-9_-]{20,}$ ]]; then
    id="$input"
  else
    return 1
  fi

  # Validate final ID
  if [[ "$id" =~ ^[A-Za-z0-9_-]{20,}$ ]]; then
    echo "$id"
    return 0
  else
    return 1
  fi
}

bold "Step 1/8 — Parse Google Doc URL"

# Get doc URL from args, env, or GUI dialog
DOC_URL=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --doc-url)
      DOC_URL="$2"
      shift 2
      ;;
    *)
      shift
      ;;
  esac
done

# Fallback to env var
if [[ -z "$DOC_URL" ]]; then
  DOC_URL="${DOC_URL:-}"
fi

# Fallback to GUI dialog
if [[ -z "$DOC_URL" ]]; then
  DOC_URL=$(osascript -e 'text returned of (display dialog "Paste Google Doc link:" default answer "")' 2>/dev/null || echo "")
fi

if [[ -z "$DOC_URL" ]]; then
  err "No Google Doc URL provided"
  print "\nProvide URL via:"
  print "  1. --doc-url <URL>"
  print "  2. DOC_URL env var"
  print "  3. GUI dialog (just double-click this script)"
  exit 1
fi

DOC_ID=$(parse_doc_id "$DOC_URL")
if [[ $? -ne 0 ]] || [[ -z "$DOC_ID" ]]; then
  err "Invalid Google Doc URL or ID"
  print "\nValid formats:"
  print "  https://docs.google.com/document/d/1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE/edit"
  print "  https://docs.google.com/document/d/1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE/"
  print "  1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE"
  print "\nYou provided: $DOC_URL"
  exit 1
fi

ok "Doc ID: $DOC_ID"

bold "Step 2/8 — Build project (ENABLE_GRPC=ON)"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DENABLE_GRPC=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TC" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx \
  -DVCPKG_HOST_TRIPLET=arm64-osx >>"$SERVER_LOG" 2>&1

cmake --build "$BUILD_DIR" -j"$JOBS" >>"$SERVER_LOG" 2>&1
ok "Build complete."

SERVER_BIN="$BUILD_DIR/bin/audio_engine_server"
[[ -x "$SERVER_BIN" ]] || { err "Server binary not found: $SERVER_BIN"; tail -n 120 "$SERVER_LOG" || true; exit 1; }

bold "Step 3/8 — Start audio engine server on :$PORT"
"$SERVER_BIN" --port "$PORT" >> "$SERVER_LOG" 2>&1 & echo $! > "$SERVER_PID"

# Wait until server prints 'Listening'
for i in {1..100}; do
  if grep -q "Listening" "$SERVER_LOG" 2>/dev/null; then
    ok "Server is listening (port $PORT)."
    break
  fi
  sleep 0.1
  [[ $i -eq 100 ]] && { err "Server did not start. See $SERVER_LOG"; open -R "$SERVER_LOG" || true; exit 1; }
done

bold "Step 4/8 — Setup Python environment"
if [[ ! -d "$VENV_DIR" ]]; then
  python3 -m venv "$VENV_DIR" >>"$BRIDGE_LOG" 2>&1
  ok "Created venv."
else
  ok "venv exists."
fi

source "$VENV_DIR/bin/activate"
pip install -q --upgrade pip >>"$BRIDGE_LOG" 2>&1
pip install -q -r "$REPO_ROOT/tools/docs_bridge/requirements.txt" >>"$BRIDGE_LOG" 2>&1
ok "Python packages installed."

bold "Step 5/8 — Generate Python gRPC stubs"
python -m grpc_tools.protoc \
  -I "$REPO_ROOT/proto" \
  --python_out="$REPO_ROOT/$PY_STUB_DIR" \
  --grpc_python_out="$REPO_ROOT/$PY_STUB_DIR" \
  "$REPO_ROOT/proto/audio_engine.proto" >>"$BRIDGE_LOG" 2>&1

ok "Python stubs generated at $PY_STUB_DIR"

bold "Step 6/8 — Start Docs Bridge"
: "${PYTHONPATH:=}"
export PYTHONPATH="$REPO_ROOT:$REPO_ROOT/$PY_STUB_DIR:${PYTHONPATH}"

cd "$REPO_ROOT/tools/docs_bridge"
python bridge.py \
  --doc-id "$DOC_ID" \
  --server "localhost:$PORT" \
  --http-port "$BRIDGE_PORT" \
  --verbose >> "$BRIDGE_LOG" 2>&1 & echo $! > "$BRIDGE_PID"

cd "$REPO_ROOT"
ok "Bridge started (PID: $(cat "$BRIDGE_PID"))."

bold "Step 7/8 — Wait for health check"
print -n "Checking $HEALTH_URL "
for i in {1..30}; do
  if curl -sf "$HEALTH_URL" >/dev/null 2>&1; then
    print ""
    ok "Bridge is healthy!"
    break
  fi
  printf "."
  sleep 1
  if [[ $i -eq 30 ]]; then
    print ""
    err "Health check timeout. Check $BRIDGE_LOG"
    tail -n 50 "$BRIDGE_LOG" || true
    exit 1
  fi
done

bold "Step 8/8 — Open dashboard & doc"
DOC_FULL_URL="https://docs.google.com/document/d/$DOC_ID/edit"

ok "Opening Google Doc..."
open "$DOC_FULL_URL" >/dev/null 2>&1 || true

ok "Opening bridge dashboard..."
open "http://127.0.0.1:$BRIDGE_PORT" >/dev/null 2>&1 || true

ok "Opening output folder..."
open "$OUT_DIR" >/dev/null 2>&1 || true

print ""
ok "SUCCESS. Docs Bridge is running!"
print ""
print "Dashboard: http://127.0.0.1:$BRIDGE_PORT"
print "Google Doc: $DOC_FULL_URL"
print "Server log: $SERVER_LOG"
print "Bridge log: $BRIDGE_LOG"
print ""
warn "Tailing bridge logs (Ctrl+C to exit and cleanup)..."
print ""

tail -f "$BRIDGE_LOG"