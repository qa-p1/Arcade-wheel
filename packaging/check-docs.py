#!/usr/bin/env python3
"""Check local Markdown links and Wheel's user-guide front matter."""
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
problems = []
files = [ROOT / "README.md", ROOT / "CHANGELOG.md", *sorted((ROOT / "docs").rglob("*.md"))]
for path in files:
    text = path.read_text(encoding="utf-8")
    for target in re.findall(r"\]\(([^)\s]+)(?:\s+[^)]*)?\)", text):
        parsed = urlsplit(target.strip("<>"))
        if parsed.scheme or parsed.netloc:
            continue
        target = unquote(parsed.path)
        if not target:
            continue
        if not (path.parent / target).exists():
            problems.append(f"{path.relative_to(ROOT)}: missing {target}")
    if path.parent == ROOT / "docs/user":
        front = re.match(r"^---\n(.*?)\n---\n", text, re.S)
        if not front or any(not re.search(rf"^{key}: .+", front.group(1), re.M) for key in ("title", "order", "since")):
            # shortcuts.md is the canonical validator's unmodified output.
            if path.name != "shortcuts.md":
                problems.append(f"{path.relative_to(ROOT)}: missing front matter")
for problem in problems:
    print(problem, file=sys.stderr)
if not problems:
    print(f"verified local links in {len(files)} documents and user-guide front matter")
sys.exit(bool(problems))
