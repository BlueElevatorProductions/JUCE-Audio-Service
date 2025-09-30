#!/usr/bin/env python3
"""
Push transcript to Google Doc.

Appends a heading, transcript text (as code block), and optional SRT block.
"""

import argparse
import sys
from pathlib import Path

from gdocs import GoogleDocsClient


def eprint(*args, **kwargs):
    """Print to stderr."""
    print(*args, file=sys.stderr, **kwargs)


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
        eprint(f"ERROR: Transcript file not found: {txt_path}")
        return 1

    text = txt_path.read_text(encoding="utf-8").strip()
    if not text:
        eprint("WARN: Transcript text is empty")

    # Read SRT if provided
    srt_content = None
    if args.srt:
        srt_path = Path(args.srt)
        if srt_path.exists():
            srt_content = srt_path.read_text(encoding="utf-8")
        else:
            eprint(f"WARN: SRT file not found: {srt_path}")

    # Authenticate
    if args.verbose:
        eprint("Authenticating with Google Docs API...")

    client = GoogleDocsClient()
    if not client.authenticate(
        oauth_client_path=args.oauth_client,
        token_override=args.token,
        verbose=args.verbose
    ):
        eprint("ERROR: Authentication failed")
        return 1

    # Append main heading
    if args.verbose:
        eprint(f"Appending heading: {args.title}")

    error = client.append_heading(args.doc_id, f"Transcript — {args.title}", level=2)
    if error:
        eprint(f"ERROR: Failed to append heading: {error}")
        return 1

    # Append transcript as text code block
    if text:
        if args.verbose:
            eprint(f"Appending transcript text ({len(text)} chars)")

        error = client.append_code_block(args.doc_id, "text", text)
        if error:
            eprint(f"ERROR: Failed to append transcript text: {error}")
            return 1
    else:
        eprint("WARN: Skipping empty transcript text")

    # Append SRT subheading and code block if provided
    if srt_content:
        if args.verbose:
            eprint("Appending SRT subtitles")

        # Add subtitle subheading
        error = client.append_heading(args.doc_id, "Subtitles (.srt)", level=3)
        if error:
            eprint(f"ERROR: Failed to append SRT subheading: {error}")
            return 1

        # Add SRT code block
        error = client.append_code_block(args.doc_id, "srt", srt_content)
        if error:
            eprint(f"ERROR: Failed to append SRT content: {error}")
            return 1

    # Success message to stdout
    print(f"Pushed transcript to document: {args.doc_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
