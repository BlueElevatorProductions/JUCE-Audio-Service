JUCE-Audio-Service

![Build Status](https://img.shields.io/badge/ci-pending-lightgrey)
![License](https://img.shields.io/badge/license-TBD-lightgrey)

JUCE-based backend for audio playback, transport, and editing.
Integrates with text-first editors (Google Docs, Lexical) via JSON EDLs and bridge layers (IPC, gRPC, WebSockets). Built for podcasters and audio engineers alike.

⸻

🚀 Features (initial goals)
	•	Load, play, pause, seek, and stop audio
	•	JSON Edit Decision List (EDL) parsing and validation
	•	Diagnostics on EDL load/apply failures
	•	Bridge layer for external editors (IPC, gRPC, WebSockets)
	•	Extensible plugin support for mixing/mastering
	•	Export to AAF/OMF for Pro Tools and DAW integration

⸻

🛠️ Setup

**Requirements:**
- Apple Silicon Mac (arm64 only)
- macOS 12.0 or later
- Git LFS (for binary cache)
- CMake 3.20+

	1.	Clone the repo

git clone https://github.com/<your-org>/JUCE-Audio-Service.git
cd JUCE-Audio-Service
git lfs install
git submodule update --init --recursive

	2.	Basic build (without gRPC)

./scripts/build.sh Release

	3.	Build with gRPC support

./scripts/build.sh Release -DENABLE_GRPC=ON

	4.	Run the test suite

./scripts/test.sh Release

	5.	Run gRPC tests (when enabled)

cd build && ctest -L grpc

	6.	Run specific EDL integration tests

cd build && ctest -R GrpcEdlIntegrationTest -V

**Note:** This project is Apple Silicon only. The first gRPC build will take longer as it downloads and caches dependencies via vcpkg. Subsequent builds use the local binary cache.


⸻

🎯 gRPC Server Usage

When built with `-DENABLE_GRPC=ON`, two additional executables are created:

**Start the gRPC server:**
```bash
# Start server on default port (50051)
./build/bin/audio_engine_server

# Start server on custom port
./build/bin/audio_engine_server --port 50052
```
Server listens on `0.0.0.0:50051` by default.

**Use the gRPC client CLI:**
```bash
# Test server connectivity
./build/tools/grpc_client_cli ping

# Load an audio file (new format)
./build/tools/grpc_client_cli load --path /path/to/audio.wav

# Render audio (full file)
./build/tools/grpc_client_cli render --path /path/to/input.wav --out /path/to/output.wav

# Render audio segment (start at 1.0s, duration 5.0s)
./build/tools/grpc_client_cli render --path /path/to/input.wav --out /path/to/output.wav --start 1.0 --dur 5.0

# Legacy format still supported
./build/tools/grpc_client_cli load /path/to/audio.wav
./build/tools/grpc_client_cli render /path/to/input.wav /path/to/output.wav 1.0 5.0
```

**EDL Commands:**
```bash
# Update EDL from JSON file
./build/tools/grpc_client_cli edl-update --edl fixtures/test_edl.json

# Update EDL with replace option
./build/tools/grpc_client_cli edl-update --edl fixtures/test_edl.json --replace

# Render EDL window (start at 0s, duration 5s, 24-bit output)
./build/tools/grpc_client_cli edl-render --edl-id abc123def --start 0 --dur 5 --out output.wav --bit-depth 24

# Render EDL window (16-bit default)
./build/tools/grpc_client_cli edl-render --edl-id abc123def --start 1.5 --dur 2.5 --out segment.wav

# Subscribe to EDL events (outputs NDJSON stream)
./build/tools/grpc_client_cli subscribe --edl-id abc123def
```

**Run automated smoke test:**
```bash
# Quick end-to-end test
./scripts/smoke_test.sh

# Use different port or config
PORT=50052 CFG=Debug ./scripts/smoke_test.sh
```

**Implemented gRPC methods:**
- `LoadFile`: Load and validate audio files
- `Render`: Offline render with streaming progress updates
- `UpdateEdl`: Validate and store EDL with JSON/protobuf conversion
- `RenderEdlWindow`: Offline render EDL segments with streaming progress
- `Subscribe`: Real-time event streaming for EDL operations (NDJSON output)

⸻

📂 Repo Structure

/CMakeLists.txt  → Root CMake project definition
/src             → JUCE backend sources
/include         → Public headers
/proto           → Protocol buffer definitions and RPC contracts
/tests           → Unit and integration tests
/tools           → Developer utilities and helper binaries
/scripts         → Build & CI helper scripts
/docs            → Specs, diagrams, and architecture notes
/fixtures        → Sample assets for tests and documentation
/.github/workflows → Continuous integration pipelines


⸻

📌 Roadmap
	•	Core playback & transport (JUCE backend)
	•	JSON EDL import/export with validation
	•	gRPC bridge for Google Docs / Lexical integration
	•	Plugin support for audio processing
	•	AAF/OMF export for Pro Tools round-trips
