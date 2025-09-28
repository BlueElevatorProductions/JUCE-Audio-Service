JUCE-Audio-Service

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


	2.	Build the JUCE backend

mkdir -p build && cd build
cmake .. -DUSE_JUCE=ON
make



⸻

📂 Repo Structure

/src         → JUCE backend sources
/include     → Headers
/build       → CMake build outputs
/docs        → Specs, diagrams
/tests       → Unit + integration tests


⸻

📌 Roadmap
	•	Core playback & transport (JUCE backend)
	•	JSON EDL import/export with validation
	•	gRPC bridge for Google Docs / Lexical integration
	•	Plugin support for audio processing
	•	AAF/OMF export for Pro Tools round-trips
