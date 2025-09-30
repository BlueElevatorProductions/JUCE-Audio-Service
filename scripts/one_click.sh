#!/usr/bin/env zsh
set -euo pipefail

# ===== User-tweakable defaults (optional) =====
: ${PORT:=50051}
: ${BUILD_TYPE:=Release}         # or Debug
: ${JOBS:=8}
: ${BIT_DEPTH:=16}               # 16|24|32
: ${RENDER_DUR_SEC:=0.25}        # seconds
# =============================================

SCRIPT_DIR="${0:A:h}"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
OUT_DIR="$REPO_ROOT/out"
RUN_DIR="$REPO_ROOT/.run"
LOG="$RUN_DIR/server.log"
PID="$RUN_DIR/server.pid"

VCPKG_TC="$REPO_ROOT/third_party/vcpkg/scripts/buildsystems/vcpkg.cmake"

bold() { print -P "\n%F{cyan}%B$1%b%f"; }
ok()   { print -P "%F{green}%B$1%b%f"; }
warn() { print -P "%F{yellow}%B$1%b%f"; }
err()  { print -P "%F{red}%B$1%b%f"; }

cleanup() {
  if [[ -f "$PID" ]]; then
    if kill -0 "$(cat "$PID")" 2>/dev/null; then
      kill "$(cat "$PID")" 2>/dev/null || true
      sleep 0.3 || true
    fi
    rm -f "$PID"
  fi
}
trap cleanup EXIT

mkdir -p "$RUN_DIR" "$OUT_DIR" "$REPO_ROOT/fixtures"
: > "$LOG"

bold "Step 1/6 — Configure & build (ENABLE_GRPC=ON)"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DENABLE_GRPC=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TC" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx \
  -DVCPKG_HOST_TRIPLET=arm64-osx >>"$LOG" 2>&1

cmake --build "$BUILD_DIR" -j"$JOBS" >>"$LOG" 2>&1
ok "Build complete."

SERVER_BIN="$BUILD_DIR/bin/audio_engine_server"
CLI_BIN="$BUILD_DIR/tools/grpc_client_cli"
FIX_BIN="$BUILD_DIR/tools/make_fixture_cli"

for f in "$SERVER_BIN" "$CLI_BIN" "$FIX_BIN"; do
  [[ -x "$f" ]] || { err "Missing binary: $f"; tail -n 120 "$LOG" || true; exit 1; }
done

bold "Step 2/6 — Start server on :$PORT"
"$SERVER_BIN" --port "$PORT" >> "$LOG" 2>&1 & echo $! > "$PID"

# Wait until server prints 'Listening'
for i in {1..100}; do
  if grep -q "Listening" "$LOG" 2>/dev/null; then
    ok "Server is listening (port $PORT)."
    break
  fi
  sleep 0.1
  [[ $i -eq 100 ]] && { err "Server did not start. See $LOG"; open -R "$LOG" || true; exit 1; }
done

bold "Step 3/6 — Generate voice fixture & basic file render"
ABS_FIX="$REPO_ROOT/fixtures/voice.wav"
"$FIX_BIN" --out "$ABS_FIX" >>"$LOG" 2>&1 || true

"$CLI_BIN" load --addr "localhost:$PORT" --path "$ABS_FIX" >>"$LOG" 2>&1
VOICE_OUT="$OUT_DIR/voice_0_250ms.wav"
"$CLI_BIN" render --addr "localhost:$PORT" \
  --path "$ABS_FIX" --start 0 --dur 0.25 --out "$VOICE_OUT" >>"$LOG" 2>&1

ok "Voice render complete."
if command -v shasum >/dev/null; then
  print "SHA256 (voice): $(shasum -a 256 "$VOICE_OUT" | awk '{print $1}')"
fi

bold "Step 4/6 — Minimal EDL update"
EDL_JSON="$RUN_DIR/edl_min.json"
cat > "$EDL_JSON" <<JSON
{
  "id": "demo",
  "sample_rate": 48000,
  "media": [
    { "id": "m1", "path": "$ABS_FIX", "sample_rate": 48000, "channels": 1 }
  ],
  "tracks": [
    {
      "id": "t1",
      "gain_db": 0.0,
      "muted": false,
      "clips": [
        {
          "id": "c1",
          "media_id": "m1",
          "start_in_media": 0,
          "start_in_timeline": 0,
          "duration": 24000,
          "gain_db": 0.0,
          "muted": false
        }
      ]
    }
  ]
}
JSON

"$CLI_BIN" edl-update --addr "localhost:$PORT" --edl "$EDL_JSON" --replace >>"$LOG" 2>&1
ok "EDL updated."

bold "Step 5/6 — EDL window render"
EDL_OUT="$OUT_DIR/edl_demo_0_${RENDER_DUR_SEC//./}ms.wav"
"$CLI_BIN" edl-render --addr "localhost:$PORT" \
  --edl-id demo --start 0.0 --dur "$RENDER_DUR_SEC" \
  --out "$EDL_OUT" --bit-depth "$BIT_DEPTH" >>"$LOG" 2>&1

ok "EDL render complete."
if command -v shasum >/dev/null; then
  print "SHA256 (edl):   $(shasum -a 256 "$EDL_OUT" | awk '{print $1}')"
fi

bold "Step 6/6 — Opening output folder"
open "$OUT_DIR" >/dev/null 2>&1 || true

ok "SUCCESS. Server log: $LOG"