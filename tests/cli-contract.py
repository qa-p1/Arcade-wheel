#!/usr/bin/env python3
"""CLI-1..4 against a built binary, in an isolated profile (no display needed).

    cli-contract.py PATH/TO/arcade-wheel

Not run in the authoring sandbox (needs a Qt 6.8 build); CI runs it.
"""
import json
import os
import subprocess
import sys
import tempfile

binary = sys.argv[1]
home = tempfile.mkdtemp(prefix="wheel-cli-")
env = {**os.environ, "HOME": home, "XDG_CONFIG_HOME": f"{home}/.config", "XDG_DATA_HOME": f"{home}/.local/share",
       "XDG_CACHE_HOME": f"{home}/.cache", "XDG_RUNTIME_DIR": home, "ARCADE_HOME": f"{home}/arcade-link",
       "QT_QPA_PLATFORM": "offscreen", "ARCADE_WHEEL_INSTANCE": home}
fail = []


def run(*args):
    return subprocess.run([binary, *args], env=env, capture_output=True, text=True, timeout=60)


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        fail.append(what)


r = run("--status")
lines = [l for l in r.stdout.splitlines() if l.strip()]
check(r.returncode in (0, 3) and len(lines) == 1 and isinstance(json.loads(lines[0]), dict), "CLI-1 --status: one JSON line, exit 0 or 3")
check(json.loads(lines[0]).get("running") is False, "CLI-1 --status reports running:false without a resident")
r = run("--bogus")
check(r.returncode == 2 and r.stderr.strip(), "CLI-2 unknown argument: usage on stderr, exit 2")
r = run("--status", "extra")
check(r.returncode == 2, "CLI-2 an extra argument is refused")
r = run("--help")
check(r.returncode == 0 and r.stdout.strip(), "CLI-2 --help: exit 0 with usage on stdout")
r = run("--version")
check(r.returncode == 0 and r.stdout.startswith("Arcade Wheel ") and r.stdout.split()[-1].count(".") == 2, "CLI-3 --version")
r = run("--arcade-manifest")
check(r.returncode == 0 and json.loads(r.stdout).get("id") == "arcade.wheel", "CLI-4 --arcade-manifest prints the manifest")
check(not os.path.exists(f"{home}/.config/arcade/wheel/settings.json"), "CLI-4 --arcade-manifest creates nothing")
sys.exit(1 if fail else 0)
