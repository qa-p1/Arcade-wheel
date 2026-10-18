#!/usr/bin/env python3
"""Fail if the AppImage needs libOpenGL.so.0 from the host.

linuxdeploy leaves GL libraries to the host, but libOpenGL.so.0 (glvnd's
OpenGL dispatcher, needed by the bundled libLayerShellQtInterface) comes from
the libopengl0 package, which desktop installs don't always have: without it
the AppImage did nothing at all. package.sh ships it; this checks the result
by reading ELF headers only.

    python3 check-bundle.py PATH/ArcadeWheel-VERSION-Linux-x86_64.AppImage
"""

import subprocess
import sys
import tempfile
from pathlib import Path


def needed(path: Path) -> list[str]:
    out = subprocess.run(["readelf", "-d", str(path)], capture_output=True, text=True).stdout
    return [line.split("[", 1)[1].rstrip("]") for line in out.splitlines() if "(NEEDED)" in line]


with tempfile.TemporaryDirectory() as tmp:
    subprocess.run([str(Path(sys.argv[1]).resolve()), "--appimage-extract"], cwd=tmp,
                   capture_output=True, check=True)
    root = Path(tmp) / "squashfs-root"
    elfs = [p for p in root.rglob("*") if p.is_file() and not p.is_symlink()
            and p.read_bytes()[:4] == b"\x7fELF"]
    bundled = {p.name for p in root.rglob("*.so*")}
    users = [str(elf.relative_to(root)) for elf in elfs if "libOpenGL.so.0" in needed(elf)]

if users and "libOpenGL.so.0" not in bundled:
    sys.exit(f"libOpenGL.so.0 is needed by {users} but not bundled")
print(f"bundle check: libOpenGL.so.0 {'bundled' if users else 'not needed'}")
