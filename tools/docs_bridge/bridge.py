#!/usr/bin/env python3
"""
Google Docs Bridge for JUCE Audio Service.

Watches a Google Doc for EDL changes and communicates with the audio engine via gRPC.
"""

import argparse
import json
import threading
import time
import uuid
from pathlib import Path
from queue import Queue
from typing import Dict, Optional

from flask import Flask, request, jsonify, current_app, Response

from edl_io import parse_and_convert, edl_cache, seconds_to_samples
from gdocs import GoogleDocsClient
from grpc_client import AudioEngineClient


class RenderJob:
    """Represents a background render job."""

    def __init__(self, job_id: str, edl_id: str, start: float, dur: float, out_path: str, bit_depth: int):
        self.job_id = job_id
        self.edl_id = edl_id
        self.start = start
        self.dur = dur
        self.out_path = out_path
        self.bit_depth = bit_depth
        self.status = 'pending'
        self.error = None
        self.result = None


class DocsBridge:
    """Main bridge service coordinating doc polling and gRPC communication."""

    def __init__(
        self,
        doc_id: str,
        server_address: str = "localhost:50051",
        poll_interval: float = 3.0,
        credentials_path: Optional[str] = None,
        oauth_client_path: Optional[str] = None,
        token_path: Optional[str] = None,
        verbose: bool = False
    ):
        """
        Initialize bridge.

        Args:
            doc_id: Google Doc ID to watch
            server_address: gRPC server address
            poll_interval: Polling interval in seconds
            credentials_path: Path to OAuth credentials or service account key
            oauth_client_path: Path to OAuth client JSON
            token_path: Path to OAuth token file
            verbose: Enable verbose logging
        """
        self.doc_id = doc_id
        self.server_address = server_address
        self.poll_interval = poll_interval
        self.oauth_client_path = oauth_client_path
        self.token_path = token_path
        self.verbose = verbose

        self.gdocs_client = GoogleDocsClient(
            credentials_path=credentials_path,
            token_path=token_path
        )
        self.grpc_client = None

        self.last_revision_id = None
        self.last_edl_id = None

        self.running = False
        self.poll_thread = None
        self.worker_thread = None

        self.render_queue = Queue()
        self.jobs: Dict[str, RenderJob] = {}

    def start(self):
        """Start the bridge service."""
        if self.verbose:
            print("\n" + "="*60)
            print("Google Docs Bridge - Starting Authentication")
            print("="*60)

        if not self.gdocs_client.authenticate(
            oauth_client_path=self.oauth_client_path,
            token_override=self.token_path,
            verbose=self.verbose
        ):
            print("\n❌ Failed to authenticate with Google")
            return False

        if self.verbose:
            print("\n" + "="*60)
            print("Connecting to Audio Engine")
            print("="*60)
            print(f"[bridge] Server address: {self.server_address}")

        self.grpc_client = AudioEngineClient(self.server_address)
        try:
            self.grpc_client.connect()
            if self.verbose:
                print("[bridge] Connected successfully")
        except Exception as e:
            print(f"\n❌ Failed to connect to audio engine: {e}")
            return False

        if self.verbose:
            print("\n" + "="*60)
            print("Starting Bridge Services")
            print("="*60)
            print(f"[bridge] Document ID: {self.doc_id}")
            print(f"[bridge] Poll interval: {self.poll_interval}s")

        self.running = True
        self.poll_thread = threading.Thread(target=self._poll_loop, daemon=True)
        self.poll_thread.start()

        self.worker_thread = threading.Thread(target=self._worker_loop, daemon=True)
        self.worker_thread.start()

        if self.verbose:
            print("[bridge] All services started")
            print("="*60 + "\n")
        else:
            print("✅ Bridge started successfully")

        return True

    def stop(self):
        """Stop the bridge service."""
        print("Stopping bridge...")
        self.running = False

        if self.poll_thread:
            self.poll_thread.join(timeout=5)

        if self.worker_thread:
            self.worker_thread.join(timeout=5)

        if self.grpc_client:
            self.grpc_client.close()

        print("Bridge stopped")

    def _poll_loop(self):
        """Polling loop that watches for doc changes."""
        while self.running:
            try:
                self._check_doc_update()
            except Exception as e:
                print(f"Error in poll loop: {e}")

            time.sleep(self.poll_interval)

    def _check_doc_update(self):
        """Check if doc has changed and process if needed."""
        content, revision_id, error = self.gdocs_client.get_doc_content(self.doc_id)

        if error:
            print(f"Failed to fetch doc: {error}")
            return

        # Check if revision changed
        if revision_id == self.last_revision_id:
            return

        print(f"Doc revision changed: {revision_id}")
        self.last_revision_id = revision_id

        # Parse and convert EDL
        edl_proto, parse_error = parse_and_convert(content, self.doc_id)

        if parse_error:
            print(f"EDL parse error: {parse_error}")
            self._append_error("ParseError", parse_error)
            return

        print(f"Parsed EDL: id={edl_proto.id}, tracks={len(edl_proto.tracks)}")

        # Update EDL on server
        result, grpc_error = self.grpc_client.update_edl(edl_proto, replace=True)

        if grpc_error:
            print(f"UpdateEdl failed: {grpc_error}")
            self._append_error("UpdateEdl", grpc_error)
            return

        print(f"UpdateEdl success: {result}")
        self.last_edl_id = result['edl_id']

        # Append success feedback
        feedback = (
            f"✅ EDL updated: id={result['edl_id']}, "
            f"tracks={result['track_count']}, clips={result['clip_count']}"
        )
        self.gdocs_client.append_paragraph(self.doc_id, feedback)

    def _append_error(self, operation: str, error_msg: str):
        """Append error message to doc."""
        try:
            self.gdocs_client.append_paragraph(self.doc_id, f"❌ {operation}: {error_msg}")
        except Exception as e:
            print(f"Failed to append error to doc: {e}")

    def enqueue_render(
        self,
        edl_id: str,
        start: float,
        dur: float,
        out_path: str,
        bit_depth: int = 16
    ) -> str:
        """
        Enqueue a render job.

        Args:
            edl_id: EDL identifier
            start: Start time in seconds
            dur: Duration in seconds
            out_path: Output file path
            bit_depth: Bit depth (16, 24, or 32)

        Returns:
            Job ID
        """
        job_id = str(uuid.uuid4())
        job = RenderJob(job_id, edl_id, start, dur, out_path, bit_depth)

        self.jobs[job_id] = job
        self.render_queue.put(job)

        print(f"Enqueued render job: {job_id}")
        return job_id

    def get_job_status(self, job_id: str) -> Optional[Dict]:
        """Get status of a render job."""
        job = self.jobs.get(job_id)
        if not job:
            return None

        return {
            'job_id': job.job_id,
            'status': job.status,
            'error': job.error,
            'result': job.result,
        }

    def _worker_loop(self):
        """Worker loop that processes render jobs."""
        while self.running:
            try:
                job = self.render_queue.get(timeout=1)
                self._process_render_job(job)
            except Exception:
                # Queue.get timeout or other error
                continue

    def _process_render_job(self, job: RenderJob):
        """Process a single render job."""
        print(f"Processing render job: {job.job_id}")
        job.status = 'running'

        try:
            # Get sample rate for EDL
            sample_rate = edl_cache.get(job.edl_id, 48000)

            # Convert seconds to samples
            start_samples = seconds_to_samples(job.start, sample_rate)
            duration_samples = seconds_to_samples(job.dur, sample_rate)

            # Collect events
            events = []

            for event, error in self.grpc_client.render_edl_window(
                job.edl_id,
                start_samples,
                duration_samples,
                job.out_path,
                job.bit_depth
            ):
                if error:
                    job.status = 'failed'
                    job.error = error
                    print(f"Render job {job.job_id} failed: {error}")
                    self._append_error("RenderEdlWindow", error)
                    return

                events.append(event)

                # Handle complete event
                if event['type'] == 'complete':
                    job.status = 'completed'
                    job.result = event
                    print(f"Render job {job.job_id} completed")

                    # Append success feedback
                    feedback = (
                        f"✅ Render complete: "
                        f"SHA256={event['sha256']}, "
                        f"output={event['out_path']}"
                    )
                    self.gdocs_client.append_paragraph(self.doc_id, feedback)

                    # Append events as code block
                    events_json = '\n'.join(json.dumps(e) for e in events)
                    self.gdocs_client.append_code_block(self.doc_id, 'engineevents', events_json)
                    return

        except Exception as e:
            job.status = 'failed'
            job.error = str(e)
            print(f"Render job {job.job_id} exception: {e}")
            self._append_error("RenderJob", str(e))


# Flask HTTP server for render endpoint
app = Flask(__name__)
bridge_instance = None


@app.route('/render', methods=['GET'])
def render_endpoint():
    """
    HTTP endpoint for triggering renders.

    Query parameters:
        edl_id: EDL identifier
        start: Start time in seconds
        dur: Duration in seconds
        bit: Bit depth (16, 24, or 32) - optional, default 16
        out: Output file path - optional, default based on edl_id

    Returns:
        JSON with job_id and status 202
    """
    global bridge_instance

    if not bridge_instance:
        return jsonify({'error': 'Bridge not initialized'}), 500

    # Parse parameters
    edl_id = request.args.get('edl_id')
    start_str = request.args.get('start')
    dur_str = request.args.get('dur')
    bit_str = request.args.get('bit', '16')
    out_path = request.args.get('out')

    if not edl_id or not start_str or not dur_str:
        return jsonify({'error': 'Missing required parameters: edl_id, start, dur'}), 400

    try:
        start = float(start_str)
        dur = float(dur_str)
        bit_depth = int(bit_str)
    except ValueError:
        return jsonify({'error': 'Invalid numeric parameter'}), 400

    if bit_depth not in (16, 24, 32):
        return jsonify({'error': 'bit_depth must be 16, 24, or 32'}), 400

    # Default output path
    if not out_path:
        out_path = f"/tmp/render_{edl_id}_{int(time.time())}.wav"

    # Enqueue job
    job_id = bridge_instance.enqueue_render(edl_id, start, dur, out_path, bit_depth)

    return jsonify({
        'job_id': job_id,
        'status': 'accepted',
    }), 202


@app.route('/job/<job_id>', methods=['GET'])
def job_status_endpoint(job_id: str):
    """Get status of a render job."""
    global bridge_instance

    if not bridge_instance:
        return jsonify({'error': 'Bridge not initialized'}), 500

    status = bridge_instance.get_job_status(job_id)

    if not status:
        return jsonify({'error': 'Job not found'}), 404

    return jsonify(status), 200


@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint."""
    return jsonify({
        'status': 'ok',
        'doc_id': current_app.config.get('DOC_ID'),
        'server': current_app.config.get('SERVER_ADDR')
    }), 200


@app.get("/")
def index():
    """Dashboard showing bridge status and links."""
    doc_id = current_app.config.get("DOC_ID") or ""
    server = current_app.config.get("SERVER_ADDR") or "localhost:50051"
    doc_url = f"https://docs.google.com/document/d/{doc_id}/edit" if doc_id else "#"

    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <title>JUCE Audio Service – Docs Bridge</title>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <style>
    :root {{
      --fg:#111; --muted:#666; --ok:#0a7; --bg:#fafafa; --card:#fff; --border:#eee;
      --mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
      --sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    }}
    body {{ margin:0; background:var(--bg); color:var(--fg); font-family:var(--sans); }}
    .wrap {{ max-width:900px; margin:40px auto; padding:0 20px; }}
    .card {{ background:var(--card); border:1px solid var(--border); border-radius:14px; padding:20px; box-shadow:0 1px 2px rgba(0,0,0,0.03); }}
    h1 {{ margin:0 0 12px; font-size:22px; }}
    .grid {{ display:grid; grid-template-columns: 1fr 1fr; gap:14px; margin-top:14px; }}
    .kv {{ font-family:var(--mono); font-size:14px; padding:10px; background: #fcfcfc; border:1px solid var(--border); border-radius:10px; }}
    a.button {{ display:inline-block; padding:10px 14px; border-radius:10px; border:1px solid var(--border); text-decoration:none; }}
    .ok {{ color:var(--ok); font-weight:600; }}
    .muted {{ color:var(--muted); }}
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <h1>Docs Bridge is running <span class="ok">●</span></h1>
      <div class="grid">
        <div class="kv">Doc ID: <strong>{doc_id or "—"}</strong></div>
        <div class="kv">Engine: <strong>{server}</strong></div>
      </div>
      <p style="margin-top:16px">
        <a class="button" href="/health">Health JSON</a>
        {'<a class="button" href="'+doc_url+'" target="_blank" rel="noopener noreferrer">Open Google Doc</a>' if doc_id else ''}
        <a class="button" href="https://127.0.0.1:8080" style="display:none">noop</a>
      </p>
      <p class="muted">Add an <code>edljson</code> fenced block in the Google Doc to push EDLs automatically.</p>
    </div>
  </div>
</body>
</html>"""
    return Response(html, mimetype="text/html")


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description='Google Docs Bridge for JUCE Audio Service')
    parser.add_argument('--doc-id', required=True, help='Google Doc ID to watch')
    parser.add_argument('--server', default='localhost:50051', help='gRPC server address')
    parser.add_argument('--poll-interval', type=float, default=3.0, help='Polling interval in seconds')
    parser.add_argument('--credentials', help='Path to OAuth credentials or service account key (GOOGLE_APPLICATION_CREDENTIALS)')
    parser.add_argument('--oauth-client', help='Path to OAuth client JSON file')
    parser.add_argument('--token', help='Path to OAuth token file (override default)')
    parser.add_argument('--creds-mode', default='installed', choices=['installed'], help='Credentials mode (only "installed" supported)')
    parser.add_argument('--http-port', type=int, default=5000, help='HTTP server port')
    parser.add_argument('--verbose', action='store_true', help='Enable verbose logging (shows resolved paths + scopes)')

    args = parser.parse_args()

    # Create and start bridge
    global bridge_instance
    bridge_instance = DocsBridge(
        doc_id=args.doc_id,
        server_address=args.server,
        poll_interval=args.poll_interval,
        credentials_path=args.credentials,
        oauth_client_path=args.oauth_client,
        token_path=args.token,
        verbose=args.verbose
    )

    if not bridge_instance.start():
        print("\n❌ Failed to start bridge")
        return 1

    # Configure Flask app
    app.config['DOC_ID'] = args.doc_id
    app.config['SERVER_ADDR'] = args.server

    # Start Flask HTTP server
    if args.verbose:
        print(f"\n[bridge] Starting HTTP server on 0.0.0.0:{args.http_port}")
    else:
        print(f"Starting HTTP server on port {args.http_port}...")

    try:
        app.run(host='0.0.0.0', port=args.http_port, debug=False)
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        bridge_instance.stop()

    return 0


if __name__ == '__main__':
    exit(main())