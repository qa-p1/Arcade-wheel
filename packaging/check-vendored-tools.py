#!/usr/bin/env python3
"""Verify Wheel's pinned shared packaging and shortcut assets."""
import hashlib
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINS = (ROOT / "scripts/VENDORED", ROOT / "packaging/windows/VENDORED")
UPSTREAM_PATHS = {
    "scripts/arcade-release.py": "tools/arcade-release.py",
    "scripts/validate_shortcuts.py": "tools/validate_shortcuts.py",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="read-only Arcade Link checkout at the pinned commit")
    args = parser.parse_args()
    problems = []
    checked = 0
    for pin in PINS:
        text = pin.read_text(encoding="utf-8")
        match = re.search(r"^Commit: ([0-9a-f]{40})$", text, re.MULTILINE)
        if args.source and not match:
            problems.append(f"{pin.relative_to(ROOT)}: no full source commit pin")
            continue
        revision = match.group(1) if match else ""
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
            if args.source:
                upstream_path = UPSTREAM_PATHS.get(relative, relative)
                upstream = subprocess.run(
                    ["git", "-C", str(args.source), "show", f"{revision}:{upstream_path}"],
                    check=False, capture_output=True,
                )
                if upstream.returncode or hashlib.sha256(upstream.stdout).hexdigest() != expected:
                    problems.append(f"{relative}: differs from Arcade Link {upstream_path} at {revision}")
            checked += 1
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    suffix = " and pinned source" if args.source else ""
    print(f"verified {checked} vendored shared assets{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
