#!/usr/bin/env python3
"""
Push transcript to Google Doc.

Appends a heading, transcript text (chunked), and optional SRT block.
"""

import argparse
import sys
from pathlib import Path

from gdocs import GoogleDocsClient

CHUNK_SIZE = 8000


def chunk_text(text: str, chunk_size: int = CHUNK_SIZE):
    """Yield chunks of text up to chunk_size characters."""
    for i in range(0, len(text), chunk_size):
        yield text[i:i + chunk_size]


def main():
    parser = argparse.ArgumentParser(description="Push transcript to Google Doc")
    parser.add_argument("--doc-id", required=True, help="Google Doc ID")
    parser.add_argument("--title", required=True, help="Section title (heading)")
    parser.add_argument("--txt", required=True, help="Path to transcript .txt file")
    parser.add_argument("--srt", default=None, help="Path to transcript .srt file (optional)")
    parser.add_argument("--oauth-client", help="Path to OAuth client JSON file")
    parser.add_argument("--token", help="Path to OAuth token file")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging")
    args = parser.parse_args()

    # Read transcript text
    txt_path = Path(args.txt)
    if not txt_path.exists():
        print(f"ERROR: Transcript file not found: {txt_path}", file=sys.stderr)
        return 1

    text = txt_path.read_text(encoding="utf-8").strip()

    # Read SRT if provided
    srt_content = None
    if args.srt:
        srt_path = Path(args.srt)
        if srt_path.exists():
            srt_content = srt_path.read_text(encoding="utf-8")

    # Authenticate
    client = GoogleDocsClient()
    if not client.authenticate(
        oauth_client_path=args.oauth_client,
        token_override=args.token,
        verbose=args.verbose
    ):
        print("ERROR: Authentication failed", file=sys.stderr)
        return 1

    # Append heading
    error = client.append_heading(args.doc_id, args.title, level=2)
    if error:
        print(f"ERROR: Failed to append heading: {error}", file=sys.stderr)
        return 1

    # Append transcript text (chunked)
    if text:
        for chunk in chunk_text(text):
            error = client.append_paragraph(args.doc_id, chunk)
            if error:
                print(f"ERROR: Failed to append transcript chunk: {error}", file=sys.stderr)
                return 1
    else:
        print("WARN: Transcript text is empty", file=sys.stderr)

    # Append SRT code block
    if srt_content:
        error = client.append_code_block(args.doc_id, "srt", srt_content)
        if error:
            print(f"ERROR: Failed to append SRT: {error}", file=sys.stderr)
            return 1

    print(f"Transcript appended to doc: {args.doc_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
