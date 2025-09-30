#!/usr/bin/env python3
"""
Transcribe audio files using OpenAI Whisper.

Outputs transcript as plain text (.txt) and subtitles (.srt).
Writes JSON result to file for bulletproof parsing.
"""

import argparse
import json
import os
import sys
import shutil
import time
from pathlib import Path


def eprint(*args, **kwargs):
    """Print to stderr."""
    print(*args, file=sys.stderr, **kwargs)


def format_srt_timestamp(seconds: float) -> str:
    """Convert seconds to SRT timestamp format: HH:MM:SS,mmm"""
    ms = int(round((seconds - int(seconds)) * 1000))
    seconds = int(seconds)
    s = seconds % 60
    m = (seconds // 60) % 60
    h = seconds // 3600
    return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"


def main():
    parser = argparse.ArgumentParser(description="Transcribe audio using Whisper")
    parser.add_argument("--audio", required=True, help="Path to audio file")
    parser.add_argument("--out-dir", default="out/transcripts", help="Output directory")
    parser.add_argument("--model", default="small.en", help="Whisper model (tiny.en, base.en, small.en, etc.)")
    parser.add_argument("--language", default=None, help="Language code (e.g., 'en')")
    parser.add_argument("--json-out", required=True, help="Path to write JSON result")
    parser.add_argument("--quiet", action="store_true", help="Suppress stderr output except fatal errors")
    args = parser.parse_args()

    # Check ffmpeg availability
    if shutil.which("ffmpeg") is None:
        eprint("ERROR: ffmpeg not found on PATH. Install with: brew install ffmpeg")
        return 2

    # Reduce noise from tokenizers
    os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

    # Import whisper
    try:
        import whisper
    except ImportError as e:
        eprint(f"ERROR: failed to import whisper: {e}")
        eprint("Install with: pip install openai-whisper")
        return 3

    # Resolve paths
    audio_path = Path(args.audio).expanduser().resolve()
    if not audio_path.exists():
        eprint(f"ERROR: Audio file not found: {audio_path}")
        return 1

    out_dir = Path(args.out_dir).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    json_out_path = Path(args.json_out).expanduser().resolve()
    json_out_path.parent.mkdir(parents=True, exist_ok=True)

    stem = audio_path.stem
    txt_path = out_dir / f"{stem}.txt"
    srt_path = out_dir / f"{stem}.srt"

    try:
        # Load model
        if not args.quiet:
            eprint(f"Loading Whisper model: {args.model}")
        model = whisper.load_model(args.model)

        # Transcribe
        if not args.quiet:
            eprint(f"Transcribing: {audio_path.name}")
        start_time = time.time()
        result = model.transcribe(str(audio_path), language=args.language, verbose=False)
        duration = time.time() - start_time

        # Write plain text
        text = result.get("text", "").strip()
        txt_path.write_text(text, encoding="utf-8")

        # Write SRT
        srt_lines = []
        for i, seg in enumerate(result.get("segments", []), 1):
            srt_lines.append(str(i))
            srt_lines.append(f"{format_srt_timestamp(seg['start'])} --> {format_srt_timestamp(seg['end'])}")
            srt_lines.append(seg.get("text", "").strip())
            srt_lines.append("")
        srt_path.write_text("\n".join(srt_lines), encoding="utf-8")

        # Write JSON result to file
        payload = {
            "txt": str(txt_path),
            "srt": str(srt_path),
            "duration_sec": round(duration, 2)
        }
        json_out_path.write_text(json.dumps(payload), encoding="utf-8")

        if not args.quiet:
            eprint(f"Transcription complete in {duration:.1f}s")

        return 0

    except Exception as e:
        eprint(f"ERROR: transcription failed: {e}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
