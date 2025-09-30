#!/usr/bin/env python3
"""
Push transcript (and optional SRT) to Google Doc.

Inserts transcript as fenced code blocks with proper headings.
"""

import argparse
import sys
from pathlib import Path

from gdocs import GoogleDocsClient


def eprint(*args, **kwargs):
    """Print to stderr."""
    print(*args, file=sys.stderr, **kwargs)


def load_text(path: str) -> str:
    """Load text file, raising FileNotFoundError if missing."""
    p = Path(path)
    if not p.exists() or not p.is_file():
        raise FileNotFoundError(f"File not found: {p}")
    return p.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Push transcript (and optional SRT) to Google Doc."
    )
    parser.add_argument("--doc-id", required=True, help="Google Doc ID")
    parser.add_argument("--title", required=True, help="Section title to insert in the doc")
    parser.add_argument("--txt", required=True, help="Path to transcript .txt")
    parser.add_argument("--srt", help="Path to subtitles .srt (optional)")
    parser.add_argument("--oauth-client", help="Path to OAuth client JSON (optional)")
    parser.add_argument("--token", help="Path to token JSON (optional)")
    parser.add_argument("--verbose", action="store_true", help="Verbose logs to stderr")
    args = parser.parse_args()

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
        eprint(f"ERROR: {e}")
        return 1
    except Exception as e:
        eprint(f"ERROR: {e}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
