#!/usr/bin/env python3
"""
Transcribe audio files using OpenAI Whisper.

Outputs transcript as plain text (.txt) and subtitles (.srt).
"""

import argparse
import json
import os
import sys
import shutil
import time
from pathlib import Path


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
    args = parser.parse_args()

    # Check ffmpeg availability
    if shutil.which("ffmpeg") is None:
        print("ERROR: ffmpeg not found on PATH. Install with Homebrew: brew install ffmpeg", file=sys.stderr)
        return 2

    # Import whisper (may take a moment on first import)
    try:
        import whisper
    except ImportError:
        print("ERROR: openai-whisper not installed. Install with: pip install openai-whisper", file=sys.stderr)
        return 2

    # Load model
    try:
        model = whisper.load_model(args.model)
    except Exception as e:
        print(f"ERROR: Failed to load Whisper model '{args.model}': {e}", file=sys.stderr)
        return 1

    # Resolve paths
    audio_path = Path(args.audio).expanduser().resolve()
    if not audio_path.exists():
        print(f"ERROR: Audio file not found: {audio_path}", file=sys.stderr)
        return 1

    out_dir = Path(args.out_dir).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    stem = audio_path.stem
    txt_path = out_dir / f"{stem}.txt"
    srt_path = out_dir / f"{stem}.srt"

    # Transcribe
    start_time = time.time()
    try:
        result = model.transcribe(str(audio_path), language=args.language, verbose=False)
    except Exception as e:
        print(f"ERROR: Transcription failed: {e}", file=sys.stderr)
        return 1
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

    # Output JSON result
    print(json.dumps({
        "txt": str(txt_path),
        "srt": str(srt_path),
        "duration_sec": round(duration, 2)
    }), flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
