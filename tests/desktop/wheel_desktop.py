#!/usr/bin/env python3
"""Tier 1 (headless Hyprland, Link's desktop harness) for Arcade Wheel's surfaces.

    wheel_desktop.py <AppImage>

In a private profile, from the AppImage the way a user starts it (it hands over to its unpacked copy):
- ID-1: Settings' class is `dev.arcade.Wheel`.
- A plain start opens Settings; --status reports the resident.
- --show maps the wheel as a full-screen layer surface (`arcade-wheel`, overlay layer) on the pointer's monitor;
  --hide removes it. --toggle opens it, and a second --toggle dismisses it.
- The wheel never takes a window's place: no Wheel client appears in Hyprland's window list while it is shown.
- Settings closes on Ctrl+W, and Wheel keeps running in the background.
- --quit stops the resident and no Wheel surface is left.
Exit 0 when every check passes, 1 otherwise.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, ".arcade-link/tools/desktop")
from arcade_desktop import Desktop, DesktopError  # noqa: E402

CLASS = "dev.arcade.Wheel"
SETTINGS = "Settings — Arcade Wheel"
LAYER = "arcade-wheel"
OVERLAY_LEVEL = 3  # wlr-layer-shell: background 0, bottom 1, top 2, overlay 3
d = Desktop()
home = tempfile.mkdtemp(prefix="wheel-desktop-")
downloads = Path(home, "Downloads")
downloads.mkdir()
app = str(downloads / "Arcade-Wheel.AppImage")
shutil.copy2(sys.argv[1], app)
os.chmod(app, 0o755)
env = {**os.environ, "HOME": home, "XDG_CONFIG_HOME": f"{home}/.config", "XDG_DATA_HOME": f"{home}/.local/share",
       "XDG_CACHE_HOME": f"{home}/.cache", "ARCADE_HOME": f"{home}/arcade-link", "ARCADE_NO_INSTALL_PROMPT": "1"}
# The harness marks AppImages "already unpacked" so tests keep the process they start; Wheel is followed through
# --status and the compositor instead, so it runs as a user's would.
env.pop("ARCADE_UNPACKED", None)
log = open(d.artifact("wheel.log"), "wb")
results = []


def check(name, ok, detail=""):
    results.append(("PASS" if ok else "FAIL", name, detail))
    print(f"{'PASS' if ok else 'FAIL'}: {name}{' (' + detail + ')' if detail and not ok else ''}", flush=True)
    return ok


def run(*args, timeout=60):
    return subprocess.run([app, *args], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)


def launch(*args):
    # A first start keeps running as the resident (after handing over to its unpacked copy); a second one forwards
    # the command to it and exits.
    return subprocess.Popen([app, *args], env=env, stdout=log, stderr=subprocess.STDOUT)


def status():
    out = subprocess.run([app, "--status"], env=env, capture_output=True, text=True, timeout=30)
    try:
        return json.loads(out.stdout.strip().splitlines()[-1])
    except (IndexError, ValueError):
        return {}


def wait_for(probe, what, timeout=15):
    try:
        return d.wait(probe, timeout, what)
    except DesktopError:
        return None


def wheel():
    return d.find_layer(LAYER)


def settings():
    return d.find_window(cls=CLASS, title=SETTINGS)


try:
    # --- Settings and the resident ------------------------------------------------------------------------------
    launch()
    window = wait_for(settings, "Settings", 45)
    if not check("a plain start opens Settings", window is not None):
        raise SystemExit(1)
    check("ID-1: Settings' class is Wheel's desktop id", window.get("class") == CLASS, window.get("class", ""))
    resident = wait_for(lambda: status() if status().get("running") else None, "--status") or status()
    check("--status reports the running Wheel", bool(resident.get("running")), json.dumps(resident))

    # --- the wheel: a layer surface, not a window --------------------------------------------------------------
    clients_before = {c["address"] for c in d.clients() if c.get("class") == CLASS}
    run("--show")
    layer = wait_for(wheel, "the wheel")
    if check("--show maps the wheel as a layer surface", layer is not None, json.dumps(d.layers())):
        check("the wheel is on the overlay layer", layer.get("level") == OVERLAY_LEVEL, str(layer.get("level")))
        monitor = next((m for m in d.monitors() if m.get("name") == layer.get("monitor")), d.monitors()[0])
        size = (layer.get("w"), layer.get("h"))
        check("the wheel covers its monitor", size == (monitor["width"], monitor["height"]),
              f"{size} on {monitor['width']}x{monitor['height']}")
        clients_now = {c["address"] for c in d.clients() if c.get("class") == CLASS}
        check("the wheel adds no window", clients_now == clients_before, str(clients_now - clients_before))
    run("--hide")
    check("--hide removes the wheel", wait_for(lambda: wheel() is None, "the wheel to go", 5) is not None)

    run("--toggle")
    check("--toggle opens the wheel", wait_for(wheel, "the wheel", 10) is not None)
    run("--toggle")
    check("a second --toggle dismisses it", wait_for(lambda: wheel() is None, "the wheel to go", 5) is not None)

    # --- Settings closes, Wheel stays ----------------------------------------------------------------------------
    window = settings()
    if window:
        d.focus(window["address"])
        time.sleep(0.3)
        d.send_shortcut(window["address"], "CTRL", "w")
        check("Ctrl+W closes Settings", wait_for(lambda: settings() is None, "Settings to close", 5) is not None)
        time.sleep(1)
        check("Wheel keeps running in the background after Settings closes", bool(status().get("running")))

    # --- quit ----------------------------------------------------------------------------------------------------
    run("--quit")
    check("--quit stops Wheel", wait_for(lambda: not status().get("running"), "Wheel to quit", 20) is not None)
    check("no Wheel surface is left after --quit",
          wait_for(lambda: d.find_window(cls=CLASS) is None and wheel() is None, "Wheel's surfaces to go", 10)
          is not None)
finally:
    subprocess.run([app, "--quit"], env=env, capture_output=True, timeout=30)
    log.flush()
    failed = [r for r in results if r[0] == "FAIL"]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed", flush=True)
    if failed:  # Also when a check stopped the run early.
        print(Path(d.artifact("wheel.log")).read_text(errors="replace")[-6000:], flush=True)

sys.exit(1 if failed else 0)
