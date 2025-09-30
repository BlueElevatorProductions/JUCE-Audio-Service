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

## 🚀 One-Click Launcher

For the fastest way to see everything working:

1. **Open Finder** → Navigate to `JUCE-Audio-Service/scripts/`
2. **Double-click** `one_click.command`
3. **Watch it run**: The script will automatically:
   - Build the project with gRPC enabled (arm64, Release)
   - Start the audio engine server on port 50051
   - Generate a voice fixture
   - Render a 250ms voice clip
   - Create and apply a minimal EDL
   - Render a 250ms EDL window
   - Display SHA256 checksums for both outputs
   - Open the `out/` folder in Finder with the rendered files

**Output files:**
- `out/voice_0_250ms.wav` — Basic file render
- `out/edl_demo_0_025ms.wav` — EDL window render

**Troubleshooting:**
- If anything fails, check `.run/server.log` for details
- The script will automatically open the log in Finder on server startup failure
- First run takes longer due to vcpkg dependency downloads
- The server is automatically cleaned up when the script exits

**Customization:**
Edit environment variables at the top of `scripts/one_click.sh`:
```bash
PORT=50051              # Server port
BUILD_TYPE=Release      # or Debug
JOBS=8                  # Parallel build jobs
BIT_DEPTH=16           # Audio bit depth (16|24|32)
RENDER_DUR_SEC=0.25    # Render duration in seconds
```

⸻

## 🎙️ One-Click with Transcription

Add automatic audio transcription to your Google Docs workflow:

1. **Open Finder** → Navigate to `JUCE-Audio-Service/scripts/`
2. **Double-click** `one_click_docs.command`
3. **Select audio file** → File picker appears for AIFF/WAV/MP3/M4A/etc.
4. **Paste Google Doc link** → Dialog prompts for Doc URL
5. **Watch it run**: The script will automatically:
   - Transcribe audio locally using OpenAI Whisper (on-device)
   - Insert transcript text and SRT subtitles into the Google Doc
   - Build the project with gRPC enabled
   - Start the audio engine server
   - Launch the Docs Bridge
   - Open the Google Doc, dashboard, and output folder

**Requirements:**
- macOS (Apple Silicon recommended)
- ffmpeg (auto-installs via Homebrew if available)
- Google OAuth credentials (same as Docs Bridge setup)
- Internet connection for first Whisper model download (~140MB for small.en)

**What gets inserted into your Doc:**
- **Heading**: "Transcript — [filename]" (H2)
- **Full transcript text** (fenced code block with `text` language)
- **Subtitles subheading** (H3) + **SRT subtitles** (fenced code block with `srt` language)
- Large files may take a moment to upload due to Google Docs API rate limits

**Output files:**
- `out/transcripts/[filename].txt` — Plain text transcript
- `out/transcripts/[filename].srt` — Subtitle file

**Customization:**
Edit environment variables at the top of `scripts/one_click_docs.sh`:
```bash
WHISPER_MODEL=small.en  # Options: tiny.en, base.en, small.en, medium.en, large
PORT=50051              # Server port
BRIDGE_PORT=8080        # HTTP dashboard port
```

**Troubleshooting:**
- **First run slow?** Whisper downloads model on first use (~140MB for small.en). Model download output is captured in `.run/transcribe.log`.
- **ffmpeg missing?** Script attempts auto-install via Homebrew
- **Transcription failed?** Check `.run/transcribe.log` for stderr output and errors
- **JSON parsing errors?** All transcription output is now file-based (`.run/transcribe.json`) for bulletproof parsing
- **OAuth issues?** See `README_docs_bridge.md` for credential setup

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
