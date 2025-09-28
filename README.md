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
	1.	Clone the repo

git clone https://github.com/<your-org>/JUCE-Audio-Service.git
cd JUCE-Audio-Service


	2.	Configure & build

./scripts/build.sh Debug

	3.	Run the test suite

./scripts/test.sh Debug

Set `JUCE_SOURCE_DIR=/path/to/JUCE` to reuse an existing checkout and avoid fetching during configure steps.


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
