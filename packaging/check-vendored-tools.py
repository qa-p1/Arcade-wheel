#!/usr/bin/env python3
"""Verify Wheel's pinned shared packaging and shortcut assets."""
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINS = (ROOT / "scripts/VENDORED", ROOT / "packaging/windows/VENDORED")


def main() -> int:
    problems = []
    checked = 0
    for pin in PINS:
        text = pin.read_text(encoding="utf-8")
        entries = re.findall(r"^([0-9a-f]{64})  ([^\r\n]+)$", text, re.MULTILINE)
        if not entries:
            problems.append(f"{pin.relative_to(ROOT)}: no SHA-256 entries")
            continue
        for expected, relative in entries:
            path = ROOT / relative
            if not path.is_file():
                problems.append(f"{relative}: missing")
                continue
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual != expected:
                problems.append(f"{relative}: checksum mismatch")
            checked += 1
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print(f"verified {checked} vendored shared assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
