#!/usr/bin/env python3
"""Verify the complete released Qt bundle and its commit-pinned provenance."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "src/link"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    pin = json.loads((ROOT / "VENDORED.json").read_text(encoding="utf-8"))
    commit = pin["commit"]
    if len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
        parser.error("VENDORED.json must pin a full source commit")
    problems = []
    for name, expected in pin["files"].items():
        path = Path(name)
        if path.is_absolute() or ".." in path.parts:
            problems.append(f"invalid vendor path: {name}")
            continue
        local = ROOT / path
        if not local.is_file() or hashlib.sha256(local.read_bytes()).hexdigest() != expected:
            problems.append(f"{name}: missing or changed since vendoring")
        if args.source:
            result = subprocess.run(["git", "-C", str(args.source), "show", f"{commit}:{name}"], capture_output=True)
            if result.returncode or hashlib.sha256(result.stdout).hexdigest() != expected:
                problems.append(f"{name}: differs from the released source")
    for problem in problems:
        print(problem, file=sys.stderr)
    if not problems:
        print(f"Arcade Link {commit}: {len(pin['files'])} bundled files verified")
    return bool(problems)

if __name__ == "__main__":
    sys.exit(main())
