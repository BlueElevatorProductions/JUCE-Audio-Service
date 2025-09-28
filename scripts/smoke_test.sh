#!/usr/bin/env bash
# One-shot local smoke test for JUCE-Audio-Service (Apple Silicon)
set -euo pipefail

# --- config ---
BRANCH="feature/grpc-cpp-with-cache-arm64"
PORT="${PORT:-50051}"                  # override by: PORT=50052 bash smoke_test.sh
CFG="${CFG:-Release}"                  # Debug/Release
BUILD_DIR="build/${CFG}"
SRV_BIN="${BUILD_DIR}/bin/audio_engine_server"
CLI_BIN="${BUILD_DIR}/tools/grpc_client_cli"
FIX_BIN="${BUILD_DIR}/tools/make_fixture_cli"
SINE_BIN="${BUILD_DIR}/tools/render_cli"
OUT_DIR="out"
FIX_DIR="fixtures"
LOG_DIR="${OUT_DIR}/logs"
SRV_LOG="${LOG_DIR}/grpc_server.log"

# --- sanity: arch & tools ---
[[ "$(uname -m)" == "arm64" ]] || { echo "This script requires Apple Silicon (arm64)."; exit 1; }
command -v git >/dev/null || { echo "git missing"; exit 1; }

# --- repo setup ---
git fetch --all -q
git checkout "$BRANCH"
git lfs install
git submodule update --init --recursive

# --- build (gRPC ON) & vcpkg env if present ---
[[ -f scripts/vcpkg_env.sh ]] && source scripts/vcpkg_env.sh || true
mkdir -p "$OUT_DIR" "$FIX_DIR" "$LOG_DIR"
./scripts/build.sh "$CFG" -DENABLE_GRPC=ON

# --- verify artifacts ---
test -x "$SRV_BIN" || { echo "server binary missing: $SRV_BIN"; exit 1; }
test -x "$CLI_BIN" || { echo "client binary missing: $CLI_BIN"; exit 1; }

# --- start server in background ---
pkill -f "audio_engine_server --port" || true
"$SRV_BIN" --port "$PORT" >"$SRV_LOG" 2>&1 &
SRV_PID=$!
echo "[INFO] server PID=$SRV_PID log=$SRV_LOG"

# wait for readiness
ATTEMPTS=60
until grep -q "Listening" "$SRV_LOG"; do
  sleep 0.5
  ATTEMPTS=$((ATTEMPTS-1))
  [[ $ATTEMPTS -gt 0 ]] || { echo "Server did not report Listening; last 100 lines:"; tail -n 100 "$SRV_LOG"; kill $SRV_PID || true; exit 1; }
done
echo "[OK] server is listening on port $PORT"

# --- make a small fixture (voice preferred; fallback to sine) ---
FIX="$FIX_DIR/voice.wav"
if [[ -x "$FIX_BIN" ]]; then
  "$FIX_BIN" --out "$FIX"
else
  FIX="$FIX_DIR/sine1k.wav"
  test -x "$SINE_BIN" || { echo "No fixture tool found (make_fixture_cli/render_cli)."; kill $SRV_PID || true; exit 1; }
  "$SINE_BIN" --sine --freq 1000 --dur 1 --sr 48000 --ch 1 --bit-depth 16 --out "$FIX"
fi
[[ -s "$FIX" ]] || { echo "Failed to create fixture file: $FIX"; kill $SRV_PID || true; exit 1; }

# --- gRPC smoke: load + render window ---
"$CLI_BIN" --addr "127.0.0.1:${PORT}" load --path "$FIX"
OUT_WAV="$OUT_DIR/test_window.wav"
"$CLI_BIN" --addr "127.0.0.1:${PORT}" render --path "$FIX" --start 0.0 --dur 0.25 --out "$OUT_WAV"

# verify output
[[ -s "$OUT_WAV" ]] || { echo "Rendered file missing/empty: $OUT_WAV"; kill $SRV_PID || true; exit 1; }
echo "[OK] render complete → $OUT_WAV"
shasum -a 256 "$OUT_WAV" || true

# --- run CTest label (optional but recommended) ---
echo "[INFO] Running gRPC tests..."
ctest --test-dir "build" -C "$CFG" -L grpc --output-on-failure || echo "[WARN] gRPC tests failed or not found"

# --- show last server logs, then cleanup ---
echo "---- server tail ----"; tail -n 100 "$SRV_LOG" || true
kill $SRV_PID || true
wait $SRV_PID 2>/dev/null || true
echo "[DONE] gRPC smoke test finished."