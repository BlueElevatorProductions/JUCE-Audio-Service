#!/usr/bin/env python3
"""
push_transcript.py — v2.0 (supports_srt=1, signature=PUSH_TRANSCRIPT_HELP_SIGNATURE_V2)

Push transcript (and optional SRT) to Google Doc with version stamping.
"""

import argparse
import sys
import pathlib
from typing import Optional

SIGNATURE = "PUSH_TRANSCRIPT_HELP_SIGNATURE_V2"
VERSION = "2.0"

# Local import
from gdocs import GoogleDocsClient


def eprint(*args, **kwargs):
    """Print to stderr."""
    print(*args, file=sys.stderr, **kwargs)


def load_text(path: str) -> str:
    """Load text file, raising FileNotFoundError if missing."""
    p = pathlib.Path(path)
    if not p.exists() or not p.is_file():
        raise FileNotFoundError(f"File not found: {p}")
    return p.read_text(encoding="utf-8", errors="replace")


def build_parser() -> argparse.ArgumentParser:
    """Build argument parser with signature in description."""
    ap = argparse.ArgumentParser(
        description=f"Push transcript (and optional SRT) to Google Doc.\n[signature:{SIGNATURE}]"
    )
    ap.add_argument("--version", action="store_true", help="Print version and exit")
    ap.add_argument("--doc-id", required=False, help="Google Doc ID")
    ap.add_argument("--title", required=False, help="Section title to insert in the doc")
    ap.add_argument("--txt", required=False, help="Path to transcript .txt")
    ap.add_argument("--srt", required=False, help="Path to subtitles .srt (optional)")
    ap.add_argument("--oauth-client", help="Path to OAuth client JSON (optional)")
    ap.add_argument("--token", help="Path to token JSON (optional)")
    ap.add_argument("--verbose", action="store_true", help="Verbose logs to stderr")
    return ap


def main() -> int:
    ap = build_parser()
    args, unknown = ap.parse_known_args()

    # Handle --version
    if args.version:
        print(f"push_transcript.py v{VERSION} supports_srt=1 signature={SIGNATURE}")
        return 0

    # Strict validation (--help still shows signature even if args missing)
    required = ["doc_id", "title", "txt"]
    missing = [k for k in required if getattr(args, k) in (None, "")]
    if missing:
        eprint(f"ERROR: Missing required arguments: {', '.join(missing)}")
        ap.print_help()
        return 2

    try:
        if args.verbose:
            eprint(f"[push] doc={args.doc_id}")
            eprint(f"[push] title={args.title}")
            eprint(f"[push] txt={args.txt}")
            if args.srt:
                eprint(f"[push] srt={args.srt}")

        # Authenticate
        client = GoogleDocsClient()
        if not client.authenticate(
            oauth_client_path=args.oauth_client,
            token_override=args.token,
            verbose=args.verbose
        ):
            eprint("ERROR: Authentication failed")
            return 1

        # Insert heading
        error = client.append_heading(args.doc_id, f"Transcript — {args.title}", level=2)
        if error:
            eprint(f"ERROR: Failed to append heading: {error}")
            return 1

        # Insert transcript as fenced code block
        txt_content = load_text(args.txt)
        error = client.append_code_block(args.doc_id, "text", txt_content)
        if error:
            eprint(f"ERROR: Failed to append transcript: {error}")
            return 1

        # Optional SRT
        if args.srt:
            # Add SRT subheading
            error = client.append_heading(args.doc_id, "Subtitles (.srt)", level=3)
            if error:
                eprint(f"ERROR: Failed to append SRT subheading: {error}")
                return 1

            # Add SRT as fenced code block
            srt_content = load_text(args.srt)
            error = client.append_code_block(args.doc_id, "srt", srt_content)
            if error:
                eprint(f"ERROR: Failed to append SRT: {error}")
                return 1

        # Success message to stdout
        print(f"Pushed transcript to document: {args.doc_id}")
        return 0

    except FileNotFoundError as e:
        eprint(f"[push][error] {e}")
        return 1
    except Exception as e:
        eprint(f"[push][error] {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
