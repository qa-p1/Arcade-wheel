#!/usr/bin/env python3
"""Check the vendored Arcade Link files against src/link/VENDORED.json.

Wheel vendors the Qt implementation of Arcade Link (src/link/ArcadeLink.*)
and its conformance vectors (tests/link-vectors/). VENDORED.json pins the
Arcade-link version they came from and their SHA-256 sums.

    python3 packaging/check-link-vendor.py            # files match the pins
    python3 packaging/check-link-vendor.py --update   # re-pin after copying
    python3 packaging/check-link-vendor.py --source ../../Rust/Arcade-link
                                                      # also compare with a checkout
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = ROOT / "src/link/VENDORED.json"
FILES = {
    "src/link/ArcadeLink.h": "qt/ArcadeLink.h",
    "src/link/ArcadeLink.cpp": "qt/ArcadeLink.cpp",
}
FILES.update({f"tests/link-vectors/{p.name}": f"spec/vectors/{p.name}" for p in sorted((ROOT / "tests/link-vectors").glob("*.json"))})


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--update", action="store_true")
    p.add_argument("--source", type=Path)
    args = p.parse_args()
    current = {name: sha(ROOT / name) for name in FILES}
    if args.update:
        pin = json.loads(PIN.read_text()) if PIN.exists() else {"arcadeLink": "unreleased"}
        pin["files"] = current
        PIN.write_text(json.dumps(pin, indent=2) + "\n")
        print(f"pinned {len(current)} files")
        return 0
    pin = json.loads(PIN.read_text())
    problems = [f"{n}: changed since it was vendored" for n, h in current.items() if pin["files"].get(n) != h]
    problems += [f"{n}: pinned but missing" for n in pin["files"] if n not in current]
    if args.source:
        for name, upstream in FILES.items():
            src = args.source / upstream
            if not src.exists() or sha(src) != current[name]:
                problems.append(f"{name}: differs from {src}")
    for problem in problems:
        print(problem, file=sys.stderr)
    if not problems:
        print(f"vendored Arcade Link ({pin.get('arcadeLink')}) matches: {len(current)} files")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
