# Google Docs Bridge for JUCE Audio Service

A Python service that watches a Google Doc for EDL (Edit Decision List) changes and synchronizes them with the JUCE Audio Service gRPC server. Enables collaborative audio editing workflows using Google Docs as the EDL editor.

## Features

- **Document Polling**: Tracks Google Doc revisions and processes changes automatically (2-5s configurable interval)
- **EDL Parsing**: Extracts EDL JSON from fenced code blocks (````edljson` or ````json` with `edl:` marker)
- **Dual Authentication**: Supports OAuth desktop flow and service account credentials
- **Background Rendering**: HTTP endpoint for triggering renders with async progress updates
- **Error Feedback**: Writes errors and completion status back to the Google Doc
- **Sample Rate Caching**: Automatically tracks EDL sample rates for accurate time conversions

## Requirements

- Python 3.11+
- Built JUCE Audio Service with gRPC enabled
- Google Cloud Project with Docs and Drive APIs enabled
- OAuth credentials or service account key

## Setup

### 1. Install Dependencies

```bash
cd tools/docs_bridge
pip install -r requirements.txt
```

### 2. Google Cloud Setup

#### Enable APIs

1. Go to [Google Cloud Console](https://console.cloud.google.com/)
2. Create a new project or select an existing one
3. Enable the following APIs:
   - Google Docs API
   - Google Drive API

#### OAuth Desktop Flow (Recommended for development)

1. Go to **APIs & Services** → **Credentials**
2. Click **Create Credentials** → **OAuth client ID**
3. Choose **Desktop app** as application type
4. Download the JSON credentials file
5. Follow **Step A2** below to configure the credentials

#### Service Account (Recommended for production)

1. Go to **APIs & Services** → **Credentials**
2. Click **Create Credentials** → **Service account**
3. Grant appropriate permissions
4. Create and download a JSON key
5. Set environment variable:
   ```bash
   export GOOGLE_APPLICATION_CREDENTIALS=/path/to/service-account-key.json
   ```
6. Share your Google Doc with the service account email (found in the key file)

### A2. Configure OAuth Credentials

After downloading your OAuth client JSON from Google Cloud Console, choose **one** of the following options:

#### Option 1 — File Path (Recommended)

Place the OAuth client JSON at the default config location:

```bash
mkdir -p "$HOME/.config/juce-audio-service/google"
cp "/ABSOLUTE/PATH/TO/your-oauth-client.json" \
   "$HOME/.config/juce-audio-service/google/oauth_client.json"
chmod 600 "$HOME/.config/juce-audio-service/google/oauth_client.json"
```

Verify the setup:
```bash
python tools/docs_bridge/auth_check.py
```

#### Option 2 — Environment Variable (Path)

Point to your OAuth client JSON using an environment variable:

```bash
export GOOGLE_OAUTH_CLIENT_JSON="/ABSOLUTE/PATH/TO/your-oauth-client.json"
```

#### Option 3 — Environment Variables (ID + Secret, No File)

Extract the client ID and secret from your downloaded OAuth client JSON and set them as environment variables:

```bash
# Extract from the JSON file or copy from Google Cloud Console
export GOOGLE_OAUTH_CLIENT_ID="204xxxxx.apps.googleusercontent.com"
export GOOGLE_OAUTH_CLIENT_SECRET="GOCSPX-xxxxx"

# Optional: Override token storage location
export GOOGLE_TOKEN_PATH="$HOME/.config/juce-audio-service/google/token.json"
```

**Verification**: Run the auth check script to verify your configuration:

```bash
python tools/docs_bridge/auth_check.py
```

Expected output:
```
============================================================
Google Docs Bridge - Credential Check
============================================================

Credentials source: env-vars  (or cli, home-config, etc.)
Credentials path  : NONE (using env-provided client id/secret)

Env-synth creds   : present
  Client ID       : 204xxxxx.apps.googleusercontent.com
  Client secret   : ******************** (hidden)

Token path (def)  : /Users/you/.config/juce-audio-service/google/token.json
  Status          : NOT FOUND (will be created on first auth)

============================================================
✅ Credentials configured correctly
============================================================
```

### 3. Build Audio Engine Protos

The bridge requires compiled protobuf files from the audio engine:

```bash
cd ../../  # Back to project root
./scripts/build.sh Release -DENABLE_GRPC=ON
```

This generates Python protobuf files in `build/proto/`.

### 4. Start Audio Engine Server

```bash
./build/bin/audio_engine_server
```

Server listens on `localhost:50051` by default.

## 🚀 One-Click: Docs Bridge (Paste Google Doc Link)

The easiest way to launch the Docs Bridge! No Terminal typing required—just paste a Google Doc link and everything starts automatically.

### Launch Methods

#### Method 1: Finder Double-Click

1. Open Finder → Navigate to `JUCE-Audio-Service/scripts/`
2. Double-click `one_click_docs.command`
3. macOS dialog appears: "Paste Google Doc link:"
4. Paste your Google Doc URL (or just the Doc ID)
5. The script automatically:
   - Builds the project with gRPC enabled
   - Starts the audio engine server
   - Sets up Python environment and generates gRPC stubs
   - Launches the Docs Bridge
   - Waits for health check
   - Opens your Google Doc, bridge dashboard (http://127.0.0.1:8080), and `out/` folder
   - Tails bridge logs (Ctrl+C to exit and cleanup)

#### Method 2: macOS Shortcut

1. Install the shortcut: Double-click `scripts/shortcuts/install_shortcut.command`
2. Find "Launch Audio Docs Bridge" in Spotlight or your Shortcuts library
3. Click the shortcut → paste Google Doc link
4. Same automated workflow as Method 1!

### What It Does

**Step-by-step breakdown**:

1. **Parse URL**: Extracts Doc ID from various URL formats
2. **Build**: Compiles with ENABLE_GRPC=ON, arm64, Release
3. **Start Server**: Audio engine server on port 50051
4. **Python Setup**: Creates venv (if needed) and installs requirements
5. **Generate Stubs**: Python gRPC bindings from proto files
6. **Launch Bridge**: Starts Docs Bridge with your Doc ID
7. **Health Check**: Waits up to 30s for bridge to be ready
8. **Open**: Google Doc + dashboard + output folder
9. **Tail Logs**: Shows live bridge activity

**Expected Terminal Output**:

```
Step 1/8 — Parse Google Doc URL
Doc ID: 1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE

Step 2/8 — Build project (ENABLE_GRPC=ON)
Build complete.

Step 3/8 — Start audio engine server on :50051
Server is listening (port 50051).

Step 4/8 — Setup Python environment
Python packages installed.

Step 5/8 — Generate Python gRPC stubs
Python stubs generated at tools/docs_bridge/stubs

Step 6/8 — Start Docs Bridge
Bridge started (PID: 12345).

Step 7/8 — Wait for health check
Checking http://127.0.0.1:8080/health ......
Bridge is healthy!

Step 8/8 — Open dashboard & doc
Opening Google Doc...
Opening bridge dashboard...
Opening output folder...

SUCCESS. Docs Bridge is running!

Dashboard: http://127.0.0.1:8080
Google Doc: https://docs.google.com/document/d/1PAZ.../edit
Server log: .run/server.log
Bridge log: .run/bridge.log

Tailing bridge logs (Ctrl+C to exit and cleanup)...
```

### Valid Google Doc URLs

The script accepts multiple formats:

- Full edit URL: `https://docs.google.com/document/d/1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE/edit`
- URL without /edit: `https://docs.google.com/document/d/1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE/`
- Minimal URL: `https://docs.google.com/document/d/1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE`
- Raw Doc ID: `1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE`

### Customization

Edit environment variables at the top of `scripts/one_click_docs.sh`:

```bash
PORT=50051              # Audio engine server port
BUILD_TYPE=Release      # or Debug
JOBS=8                  # Parallel build jobs
BIT_DEPTH=16           # Audio bit depth (16|24|32)
RENDER_DUR_SEC=0.25    # Render duration in seconds
BRIDGE_PORT=8080       # HTTP dashboard port
PY_STUB_DIR=tools/docs_bridge/stubs  # Python stub location
HEALTH_URL=http://127.0.0.1:8080/health  # Health check endpoint
```

### Installing the Shortcut

#### Option 1: Automatic (Recommended)

```bash
# Run the installer
./scripts/shortcuts/install_shortcut.command
```

The installer will decode and import the shortcut using the `shortcuts` CLI.

#### Option 2: Manual Creation

If the automatic installer doesn't work, create the shortcut manually:

1. Open the **Shortcuts** app
2. Click **+** to create a new shortcut
3. Name it: **Launch Audio Docs Bridge**
4. Add these actions in order:

   **Action 1: Ask for Input**
   - Type: Text
   - Prompt: "Paste Google Doc link:"

   **Action 2: If**
   - Condition: Provided Input contains "docs.google.com"
   - (The OR length >= 20 check happens in the shell script)

   **Action 3: Run Shell Script** (inside the If block)
   - Shell: /bin/zsh
   - Script:
     ```bash
     cd "$HOME/path/to/JUCE-Audio-Service"
     ./scripts/one_click_docs.command --doc-url "$WF_INPUT"
     ```
   - Input: Shortcut Input
   - Pass: as arguments

   **Action 4: Otherwise**

   **Action 5: Show Alert** (inside the Otherwise block)
   - Message: "Invalid Google Doc URL. Please provide a valid docs.google.com link or document ID."

   **Action 6: End If**

5. Save the shortcut
6. Test by clicking it and pasting a Doc URL

### One-Click Troubleshooting

#### Invalid URL Error

If you see `Invalid Google Doc URL or ID`, ensure your input matches one of the valid formats above.

**Examples of invalid URLs**:
- ❌ `docs.google.com/document/d/...` (missing https://)
- ❌ `https://drive.google.com/file/d/...` (Google Drive, not Docs)
- ❌ Short or malformed IDs (< 20 characters)

#### Health Check Timeout

If "Health check timeout" appears after 30 seconds:

1. Check `.run/bridge.log` for errors:
   ```bash
   tail -50 .run/bridge.log
   ```
2. Common issues:
   - **OAuth not configured**: See "A2. Configure OAuth Credentials" above
   - **Server not started**: Check `.run/server.log`
   - **Port conflict**: Change `BRIDGE_PORT` in script

#### OAuth Setup Reminders

The bridge uses the OAuth discovery system documented earlier. Ensure you have:

- **Option 1**: `~/.config/juce-audio-service/google/oauth_client.json` exists
- **Option 2**: `$GOOGLE_OAUTH_CLIENT_JSON` points to valid file
- **Option 3**: `$GOOGLE_OAUTH_CLIENT_ID` and `$GOOGLE_OAUTH_CLIENT_SECRET` are set

Run the auth check:
```bash
python tools/docs_bridge/auth_check.py
```

#### First Run Delays

- **vcpkg dependencies**: First build takes 10-20 minutes (downloads C++ deps)
- **Python packages**: First venv setup takes 1-2 minutes (downloads pip packages)
- Subsequent runs are much faster (cached)

#### Log Locations

- **Server log**: `.run/server.log` (audio engine output)
- **Bridge log**: `.run/bridge.log` (Python bridge output, OAuth flow, HTTP requests)

Open logs in Finder:
```bash
open -R .run/server.log
open -R .run/bridge.log
```

---

## Usage

### Basic Usage (With File-Based Credentials)

If you've placed the OAuth client JSON at `~/.config/juce-audio-service/google/oauth_client.json`:

```bash
python tools/docs_bridge/bridge.py --doc-id <GOOGLE_DOC_ID>
```

### Using Environment Variables (ID + Secret)

```bash
export GOOGLE_OAUTH_CLIENT_ID="204xxxxx.apps.googleusercontent.com"
export GOOGLE_OAUTH_CLIENT_SECRET="GOCSPX-xxxxx"
python tools/docs_bridge/bridge.py \
  --doc-id <GOOGLE_DOC_ID> \
  --server localhost:50051 \
  --http-port 8080 \
  --verbose
```

**Expected output with --verbose:**

```
============================================================
Google Docs Bridge - Starting Authentication
============================================================
[gdocs] OAuth mode: installed-app
[gdocs] Credentials source: env-vars
[gdocs] Credentials path: NONE (using env-provided client id/secret)
[gdocs] Token path: /Users/<you>/.config/juce-audio-service/google/token.json
[gdocs] Scopes: https://www.googleapis.com/auth/documents, https://www.googleapis.com/auth/drive.readonly
[gdocs] Starting OAuth consent flow...
Please visit this URL to authorize this application: https://...
[gdocs] OAuth consent completed successfully
[gdocs] Token saved to: /Users/<you>/.config/juce-audio-service/google/token.json

============================================================
Connecting to Audio Engine
============================================================
[bridge] Server address: localhost:50051
[bridge] Connected successfully

============================================================
Starting Bridge Services
============================================================
[bridge] Document ID: 1PAZRYvJxWuxrEpMo1GmN5HwDd_V70N4CqmvB-JMG5cE
[bridge] Poll interval: 3.0s
[bridge] All services started
============================================================

[bridge] Starting HTTP server on 0.0.0.0:8080
```

### Using Custom OAuth Client Path

```bash
python tools/docs_bridge/bridge.py \
  --doc-id <GOOGLE_DOC_ID> \
  --oauth-client /path/to/oauth_client.json \
  --verbose
```

### Using Service Account

```bash
export GOOGLE_APPLICATION_CREDENTIALS=/path/to/service-account-key.json
python tools/docs_bridge/bridge.py --doc-id <GOOGLE_DOC_ID>
```

### CLI Options

```
--doc-id <ID>           Google Doc ID to watch (required)
--server <addr>         gRPC server address (default: localhost:50051)
--poll-interval <secs>  Polling interval in seconds (default: 3.0)
--oauth-client <path>   Path to OAuth client JSON file
--token <path>          Override token storage path
--credentials <path>    Path to service account key (GOOGLE_APPLICATION_CREDENTIALS)
--http-port <port>      HTTP server port (default: 5000)
--verbose               Enable verbose logging (shows resolved paths + scopes)
--creds-mode installed  Credentials mode (only "installed" supported)
```

## EDL Format

The bridge extracts EDL JSON from fenced code blocks in your Google Doc. Use either format:

### Format 1: `edljson` block

````markdown
```edljson
{
  "id": "my-project-v1",
  "sample_rate": 48000,
  "media": [
    {
      "id": "m1",
      "path": "fixtures/voice.wav",
      "sample_rate": 48000,
      "channels": 1
    }
  ],
  "tracks": [
    {
      "id": "t1",
      "gain_db": -3.0,
      "muted": false,
      "clips": [
        {
          "id": "c1",
          "media_id": "m1",
          "start_in_media": 0,
          "start_in_timeline": 0,
          "duration": 24000,
          "gain_db": 0.0,
          "fade_in": {
            "duration_samples": 1000,
            "shape": "LINEAR"
          },
          "fade_out": {
            "duration_samples": 2000,
            "shape": "EQUAL_POWER"
          },
          "muted": false
        }
      ]
    }
  ]
}
```
````

### Format 2: `json` block with `edl:` marker

````markdown
```json
edl:
{
  "sample_rate": 48000,
  "tracks": [...]
}
```
````

**Notes:**
- If multiple blocks exist, the **last one** is used
- If `id` is missing, it's derived as `doc-<docId>`
- All fields use **snake_case** to match protobuf schema
- Fade shapes: `"LINEAR"` or `"EQUAL_POWER"`

## HTTP API

### Trigger Render

```bash
curl "http://localhost:5000/render?edl_id=my-project-v1&start=0&dur=5&bit=24&out=/tmp/output.wav"
```

**Parameters:**
- `edl_id` (required): EDL identifier
- `start` (required): Start time in seconds
- `dur` (required): Duration in seconds
- `bit` (optional): Bit depth (16, 24, or 32), default 16
- `out` (optional): Output path, default `/tmp/render_<edl_id>_<timestamp>.wav`

**Response (202 Accepted):**
```json
{
  "job_id": "550e8400-e29b-41d4-a716-446655440000",
  "status": "accepted"
}
```

### Check Job Status

```bash
curl "http://localhost:5000/job/<job_id>"
```

**Response:**
```json
{
  "job_id": "550e8400-e29b-41d4-a716-446655440000",
  "status": "completed",
  "error": null,
  "result": {
    "type": "complete",
    "out_path": "/tmp/output.wav",
    "duration_sec": 5.0,
    "sha256": "abc123..."
  }
}
```

## Feedback to Google Doc

The bridge automatically appends feedback to your Google Doc:

### Success Messages

```
✅ EDL updated: id=my-project-v1, tracks=2, clips=5

✅ Render complete: SHA256=abc123def..., output=/tmp/output.wav
```

### Error Messages

```
❌ UpdateEdl INVALID_ARGUMENT: Missing media reference for clip c1

❌ RenderEdlWindow NOT_FOUND: EDL 'my-project-v1' not found
```

### Progress Events

Render progress is appended as a code block:

````markdown
```engineevents
{"type":"progress","fraction":0.5,"eta":"1s"}
{"type":"progress","fraction":1.0,"eta":"0s"}
{"type":"complete","out_path":"/tmp/output.wav","duration_sec":5.0,"sha256":"abc123"}
```
````

## Testing

### Run Unit Tests

```bash
cd tools/docs_bridge
python -m pytest tests/test_edl_io.py -v
```

### Run Integration Tests

```bash
python -m pytest tests/test_integration.py -v
```

**Note:** Integration tests use mocked gRPC stubs and don't require a running server.

### Manual End-to-End Test

1. Start audio engine server:
   ```bash
   ./build/bin/audio_engine_server
   ```

2. Create a test Google Doc with an EDL block

3. Run the bridge:
   ```bash
   python tools/docs_bridge/bridge.py --doc-id <YOUR_DOC_ID> --credentials credentials.json
   ```

4. Edit the EDL in the doc (change a clip duration, add a fade, etc.)

5. Watch the bridge logs for parse → UpdateEdl → success

6. Trigger a render:
   ```bash
   curl "http://localhost:5000/render?edl_id=<EDL_ID>&start=0&dur=5"
   ```

7. Check the doc for completion feedback and event log

## Architecture

```
┌─────────────────┐
│  Google Doc     │  ← User edits EDL JSON
│  (EDL JSON)     │
└────────┬────────┘
         │ Polling (revisionId tracking)
         ↓
┌─────────────────┐
│  bridge.py      │  ← Main service
│  - Poll loop    │
│  - HTTP server  │
│  - Worker thread│
└────┬────┬───┬───┘
     │    │   │
     │    │   └─→ gdocs.py (Google Docs API)
     │    │
     │    └─→ edl_io.py (JSON → protobuf)
     │
     └─→ grpc_client.py (Audio Engine gRPC)
              ↓
     ┌─────────────────┐
     │ Audio Engine    │
     │ gRPC Server     │
     └─────────────────┘
```

## Troubleshooting

### "No OAuth credentials found"

**Cause:** Bridge cannot find OAuth client credentials.

**Solution:**

Run the auth check script to diagnose:
```bash
python tools/docs_bridge/auth_check.py
```

The script will show exactly what the bridge is looking for. If credentials are not found, it will display:

```
❌ No valid credentials found

To fix this, choose one option:

Option 1 - Place file at default location:
  mkdir -p ~/.config/juce-audio-service/google
  cp /path/to/oauth_client.json ~/.config/juce-audio-service/google/

Option 2 - Set environment variable:
  export GOOGLE_OAUTH_CLIENT_JSON='/path/to/oauth_client.json'

Option 3 - Use client ID and secret:
  export GOOGLE_OAUTH_CLIENT_ID='xxxx.apps.googleusercontent.com'
  export GOOGLE_OAUTH_CLIENT_SECRET='yyyy'
```

The bridge searches in this order:
1. `--oauth-client` CLI flag
2. `$GOOGLE_OAUTH_CLIENT_JSON` (path or inline JSON)
3. `$GOOGLE_OAUTH_CLIENT_ID` + `$GOOGLE_OAUTH_CLIENT_SECRET`
4. `$XDG_CONFIG_HOME/juce-audio-service/google/oauth_client.json`
5. `~/.config/juce-audio-service/google/oauth_client.json`
6. `tools/docs_bridge/oauth_client.json` (repo fallback)

### "Client not authenticated"

**Cause:** OAuth token expired or invalid.

**Solution:**
- Delete token and re-authenticate:
  ```bash
  rm ~/.config/juce-audio-service/google/token.json
  python tools/docs_bridge/bridge.py --doc-id <ID> --verbose
  ```
- Check that credentials are still valid in Google Cloud Console
- For service accounts, verify `GOOGLE_APPLICATION_CREDENTIALS` points to valid key

### "Protobuf module not available"

**Cause:** Audio engine protos not built.

**Solution:**
```bash
cd ../..
./scripts/build.sh Release -DENABLE_GRPC=ON
```

### "Failed to connect to audio engine"

**Cause:** gRPC server not running or wrong address.

**Solution:**
- Start server: `./build/bin/audio_engine_server`
- Use `--server` flag if running on different host/port

### "No EDL block found"

**Cause:** Fenced code block not formatted correctly.

**Solution:**
- Use ````edljson` or ````json` with `edl:` as first line
- Ensure closing ` ``` ` is present
- Check for typos in fence markers

### "HTTP error 403" when accessing doc

**Cause:** Service account doesn't have access.

**Solution:**
- Share doc with service account email
- Or use OAuth desktop flow instead

## Advanced Configuration

### Custom Token Path

```python
from gdocs import GoogleDocsClient

client = GoogleDocsClient(
    token_path='/custom/path/token.json',
    credentials_path='/path/to/credentials.json'
)
```

### Multiple Document Support

To watch multiple docs, run multiple bridge instances:

```bash
# Terminal 1
python bridge.py --doc-id <DOC1> --http-port 5000

# Terminal 2
python bridge.py --doc-id <DOC2> --http-port 5001
```

### Production Deployment

For production, consider:
- Using service account credentials
- Running behind a reverse proxy (nginx)
- Adding authentication to HTTP endpoints
- Logging to file instead of stdout
- Running as a systemd service

Example systemd service:

```ini
[Unit]
Description=JUCE Audio Docs Bridge
After=network.target

[Service]
Type=simple
User=audio
Environment=GOOGLE_APPLICATION_CREDENTIALS=/etc/juce-audio/service-account.json
ExecStart=/usr/bin/python3 /opt/juce-audio/tools/docs_bridge/bridge.py --doc-id <DOC_ID>
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

## License

Same as JUCE-Audio-Service project.

## Contributing

See main project README for contribution guidelines.