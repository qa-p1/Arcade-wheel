#!/usr/bin/env python3
"""Launch the deployed UI without inheriting the build machine's Qt paths."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    environment = os.environ.copy()
    for key in list(environment):
        if key.startswith(("QT_", "QML", "DYLD_")) or key in ("LD_LIBRARY_PATH", "LD_PRELOAD"):
            environment.pop(key)
    if sys.platform == "win32":
        # os.environ is case-insensitive on Windows; its plain dict copy is not.
        system_root = Path(os.environ["SystemRoot"])
        environment["PATH"] = os.pathsep.join(map(str, (system_root / "System32", system_root)))
    else:
        environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    environment.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                       ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT="1", APPIMAGE_EXTRACT_AND_RUN="1")
    with tempfile.TemporaryDirectory(prefix="arcade-wheel-smoke-") as directory:
        environment.update(XDG_CONFIG_HOME=directory, XDG_CACHE_HOME=directory,
                           XDG_RUNTIME_DIR=directory, ARCADE_WHEEL_INSTANCE=directory)
        result = subprocess.run([str(executable), "--smoke-test"], env=environment,
                                cwd=directory, capture_output=True, text=True, timeout=60)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if result.returncode:
            raise SystemExit(f"Packaged application failed to start: {result.returncode}")
    print(f"Packaged Settings and overlay loaded successfully: {executable.name}")


if __name__ == "__main__":
    main()
