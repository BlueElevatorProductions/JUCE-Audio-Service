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
: ${WHISPER_MODEL:=small.en}
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

bold "Step 1/10 — Select audio file to transcribe"

# Choose audio file via AppleScript file picker
AUDIO_PATH=$(osascript <<'APPLESCRIPT' 2>/dev/null || echo ""
  set audioFile to choose file with prompt "Select an audio file to transcribe:" of type {"public.audio"}
  POSIX path of audioFile
APPLESCRIPT
)

if [[ -z "$AUDIO_PATH" ]]; then
  err "No audio file selected. Cancelled."
  exit 1
fi

ok "Audio file: $AUDIO_PATH"

bold "Step 2/10 — Parse Google Doc URL"

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

bold "Step 3/10 — Build project (ENABLE_GRPC=ON)"
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

bold "Step 4/10 — Start audio engine server on :$PORT"
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

bold "Step 5/10 — Setup Python environment"
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

bold "Step 6/10 — Check ffmpeg and transcribe audio"

# Check ffmpeg availability
if ! command -v ffmpeg >/dev/null 2>&1; then
  warn "ffmpeg not found. Attempting to install via Homebrew..."
  if command -v brew >/dev/null 2>&1; then
    brew install ffmpeg || { err "Failed to install ffmpeg via Homebrew"; exit 1; }
    ok "ffmpeg installed."
  else
    err "ffmpeg not found and Homebrew not available."
    print "\nPlease install ffmpeg manually:"
    print "  brew install ffmpeg"
    exit 1
  fi
else
  ok "ffmpeg is available."
fi

# Transcribe audio (file-based JSON output)
TRANS_OUT_DIR="$OUT_DIR/transcripts"
mkdir -p "$TRANS_OUT_DIR"

TRANS_JSON_FILE="$RUN_DIR/transcribe.json"
TRANS_LOG_FILE="$RUN_DIR/transcribe.log"

print "Transcribing audio (model: $WHISPER_MODEL)..."
python "$REPO_ROOT/tools/docs_bridge/transcribe.py" \
  --audio "$AUDIO_PATH" \
  --out-dir "$TRANS_OUT_DIR" \
  --model "$WHISPER_MODEL" \
  --json-out "$TRANS_JSON_FILE" \
  --quiet 2>"$TRANS_LOG_FILE"
TRANS_RC=$?

# Check for transcription errors
if [[ $TRANS_RC -ne 0 ]] || [[ ! -s "$TRANS_JSON_FILE" ]]; then
  err "Transcription failed (exit code: $TRANS_RC)"
  print "See log: $TRANS_LOG_FILE"
  [[ -f "$TRANS_LOG_FILE" ]] && tail -n 20 "$TRANS_LOG_FILE"
  exit 1
fi

# Parse JSON from file (bulletproof)
TXT_PATH=$(python3 <<PYEOF
import json
with open("$TRANS_JSON_FILE", "r", encoding="utf-8") as f:
    data = json.load(f)
print(data["txt"])
PYEOF
)

SRT_PATH=$(python3 <<PYEOF
import json
with open("$TRANS_JSON_FILE", "r", encoding="utf-8") as f:
    data = json.load(f)
print(data.get("srt", ""))
PYEOF
)

TRANS_DUR=$(python3 <<PYEOF
import json
with open("$TRANS_JSON_FILE", "r", encoding="utf-8") as f:
    data = json.load(f)
print(data["duration_sec"])
PYEOF
)

ok "Transcription complete (${TRANS_DUR}s)"
ok "Transcript: $TXT_PATH"
[[ -n "$SRT_PATH" ]] && ok "Subtitles: $SRT_PATH"

bold "Step 7/10 — Push transcript to Google Doc"

TRANS_TITLE="$(basename "$AUDIO_PATH")"

# Verify push script supports --srt (defensive guard)
if ! python "$REPO_ROOT/tools/docs_bridge/push_transcript.py" --help 2>&1 | grep -q -- '--srt'; then
  warn "push_transcript.py does not support --srt. Falling back to TXT-only."
  SRT_ARG=""
else
  if [[ -n "$SRT_PATH" && -f "$SRT_PATH" ]]; then
    SRT_ARG="--srt $SRT_PATH"
  else
    SRT_ARG=""
  fi
fi

print "Pushing transcript to Google Doc..."
if python "$REPO_ROOT/tools/docs_bridge/push_transcript.py" \
  --doc-id "$DOC_ID" \
  --title "$TRANS_TITLE" \
  --txt "$TXT_PATH" \
  $SRT_ARG; then
  ok "Transcript pushed to Google Doc."
else
  err "Failed to push transcript to Google Doc"
  print "Check .run/bridge.log for details"
  exit 1
fi

bold "Step 8/10 — Generate Python gRPC stubs"
python -m grpc_tools.protoc \
  -I "$REPO_ROOT/proto" \
  --python_out="$REPO_ROOT/$PY_STUB_DIR" \
  --grpc_python_out="$REPO_ROOT/$PY_STUB_DIR" \
  "$REPO_ROOT/proto/audio_engine.proto" >>"$BRIDGE_LOG" 2>&1

ok "Python stubs generated at $PY_STUB_DIR"

bold "Step 9/10 — Start Docs Bridge"
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

# Wait for health check
echo "Checking http://127.0.0.1:8080/health ..."
READY=0
for i in {1..60}; do
  if curl -fsS http://127.0.0.1:8080/health >/dev/null 2>&1; then
    READY=1
    echo "Bridge health OK."
    break
  fi
  if grep -q "All services started" "$BRIDGE_LOG"; then
    READY=1
    echo "Bridge ready (log confirmed)."
    break
  fi
  printf "."
  sleep 1
done
echo

if [ "$READY" -ne 1 ]; then
  echo "Health check timeout. Check $BRIDGE_LOG"
  exit 1
fi

bold "Step 10/10 — Open dashboard & doc"
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
print "Transcript: $TXT_PATH"
print "Server log: $SERVER_LOG"
print "Bridge log: $BRIDGE_LOG"
print ""
warn "Tailing bridge logs (Ctrl+C to exit and cleanup)..."
print ""

tail -f "$BRIDGE_LOG"
