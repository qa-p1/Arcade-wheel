#!/usr/bin/env python3
"""Verify the normal Wheel binary, fresh and schema-3 configs, inside e2e.py run.

    python3 ../../Rust/Arcade-link/tools/e2e.py run -- python3 tests/verify-wheel.py
    python3 ../../Rust/Arcade-link/tools/e2e.py run -- python3 tests/verify-wheel.py --with-peers

Starts only known binaries and terminates only PIDs started by this script.
Screenshots go to ARCADE_E2E_SHOTS or the isolated session's temporary root.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time

REPO = Path(__file__).resolve().parents[1]
CODING = REPO.parents[1]
WHEEL = REPO / "build/arcade-wheel"
CLI = CODING / "Rust/Arcade-link/target/debug/arcade-link"
PEERS = {
    "arcade.box": CODING / "Rust/Arcade Box/target/release/arcade-desktop",
    "arcade.lens": CODING / "Rust/Arcade-lens/target/release/arcade-lens",
    "arcade.look": CODING / "Rust/arcade-look/src-tauri/target/release/arcade-look",
    "arcade.clipboard": CODING / "App dev/Arcade-clipboard/apps/flutter_app/build/linux/x64/release/bundle/clipboard",
}


def run(*arguments, check=True):
    return subprocess.run(list(map(str, arguments)), capture_output=True, text=True,
                          check=check, timeout=10).stdout.strip()


def wait(predicate, message, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.1)
    raise AssertionError(message)


def window(pid, title=None, fullscreen=False):
    screen = list(map(int, run("xdotool", "getdisplaygeometry").split()))
    ids = run("xdotool", "search", "--onlyvisible", "--pid", pid, check=False).splitlines()
    for win in ids:
        if title and title not in run("xdotool", "getwindowname", win):
            continue
        if fullscreen:
            geometry = dict(line.split("=", 1) for line in run("xdotool", "getwindowgeometry", "--shell", win).splitlines() if "=" in line)
            if [int(geometry["WIDTH"]), int(geometry["HEIGHT"])] != screen:
                continue
        return win
    return None


def capture(shots, name, win):
    run("xdotool", "windowraise", win)
    path = shots / (name + ".png")
    # The native reveal notification precedes the entrance animation's painted
    # frame. Wait for rendered content rather than capturing that blank frame.
    def rendered():
        run("import", "-window", win, path)
        return path.stat().st_size > 5000

    wait(rendered, "Window did not paint: " + str(path), timeout=5)
    time.sleep(.5)  # Let the visible entrance animation settle for inspection.
    run("import", "-window", win, path)
    return path


def text(win, shots, psm=11):
    path = capture(shots, "wheel-final-check", win)
    return run("env", "OMP_THREAD_LIMIT=1", "tesseract", path, "stdout", "--psm", psm)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            # A heavily loaded host can take longer to reap a killed peer.
            process.wait(timeout=30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--with-peers", action="store_true")
    args = parser.parse_args()
    assert os.environ.get("ARCADE_E2E_INNER") == "1", "Use the isolated e2e.py run wrapper"
    assert not os.environ.get("WAYLAND_DISPLAY") and not os.environ.get("HYPRLAND_INSTANCE_SIGNATURE")
    root = Path(os.environ["ARCADE_E2E_ROOT"])
    shots = Path(os.environ.get("ARCADE_E2E_SHOTS", root))
    shots.mkdir(parents=True, exist_ok=True)
    config = Path(os.environ["XDG_CONFIG_HOME"]) / "Arcade Wheel/Arcade Wheel/config.json"
    processes = []
    screenshots = []
    logs = []
    prefix = "wheel-all-peers" if args.with_peers else "wheel-standalone"
    try:
        if args.with_peers:
            for app, binary in PEERS.items():
                assert binary.is_file(), binary
                log = (root / (app + ".final.log")).open("w")
                logs.append(log)
                process = subprocess.Popen([str(binary), "--background"], stdout=log, stderr=log,
                                           start_new_session=True)
                processes.append(process)
                wait(lambda: json.loads(run(CLI, "status", app, "--json", check=False) or "{}"),
                     app + " did not serve its endpoint")
                assert process.poll() is None, app

        for legacy in (False, True):
            if legacy:
                config.write_bytes((REPO / "tests/fixtures/wheel-schema3.json").read_bytes())
            log = (root / (prefix + ("-legacy" if legacy else "") + ".log")).open("w")
            logs.append(log)
            process = subprocess.Popen([str(WHEEL), "--background"], stdout=log, stderr=log,
                                       start_new_session=True)
            processes.append(process)

            def ready():
                status = run(WHEEL, "--status", check=False)
                return json.loads(status) if status.startswith("{") else None

            wait(ready, "normal background Wheel did not start")
            wait(lambda: config.exists(), "Wheel did not load a config")
            wait(lambda: json.loads(run(CLI, "status", "wheel", "--json", check=False) or "{}"),
                 "Wheel did not serve Link")
            mode = json.loads(run(CLI, "status", "wheel", "--json"))["status"]["mode"]
            assert mode == "background", mode
            saved = json.loads(config.read_text())
            assert saved["schemaVersion"] == 4, saved
            if legacy:
                original = json.loads((REPO / "tests/fixtures/wheel-schema3.json").read_text())
                deck = saved["decks"][0]
                assert deck["id"] == "saved-deck" and deck["name"] == "My shortcuts"
                action = deck["actions"][0]
                assert action["id"] == "box-slot" and action["type"] == "arcade"
                assert action["payload"]["legacyPayload"] == original["decks"][0]["actions"][0]["payload"]
                assert deck["actions"][1] == original["decks"][0]["actions"][1]
                assert saved["trigger"]["shortcut"] == "Ctrl+Shift+F8"
                gesture = saved["centerGestures"]["doubleClick"]["actions"][0]
                assert gesture["id"] == "box-gesture" and gesture["payload"]["legacyPayload"]["custom"] == {"quality": 80}

            name = prefix + ("-legacy" if legacy else "")
            run(WHEEL, "--show")
            wait(lambda: ready()["overlayRevealed"], "Wheel did not reveal")
            overlay = wait(lambda: window(process.pid, fullscreen=True), "Wheel surface missing")
            run("xdotool", "windowraise", overlay)
            width, height = map(int, run("xdotool", "getdisplaygeometry").split())
            run("xdotool", "mousemove", width // 2, height // 2)
            screenshots.append(str(capture(shots, name + "-wheel", overlay)))
            run(WHEEL, "--cancel")
            wait(lambda: not ready()["overlayVisible"], "Wheel did not cancel")
            time.sleep(.3)
            run(WHEEL, "--settings")
            settings = wait(lambda: window(process.pid, "Settings"), "Settings window missing")
            run("xdotool", "windowsize", settings, "1240", "820", "windowfocus", settings)
            time.sleep(.5)
            screenshots.append(str(capture(shots, name + "-settings", settings)))
            assert "Your wheel" in text(settings, shots), "Settings did not render its normal wheel page"
            if legacy:
                assert "My shortcuts" in text(settings, shots), "Old deck did not appear in Settings"
            else:
                run("xdotool", "mousemove", "--window", settings, "90", "430", "click", "1")
                time.sleep(.5)
                screenshots.append(str(capture(shots, name + "-connected-apps", settings)))
                ui = text(settings, shots, psm=6)
                assert "Connected apps" in ui, ui
                registry = json.loads(run(CLI, "ls", "--json"))
                if args.with_peers:
                    assert ui.count("Use with Arcade Wheel") == 4, ui
                    assert {app["id"] for app in registry} == {"arcade.wheel", *PEERS}, registry
                else:
                    assert "Use with Arcade Wheel" not in ui, ui
                    assert {app["id"] for app in registry} == {"arcade.wheel"}, registry
            run(WHEEL, "--quit")
            process.wait(timeout=5)
            assert process.returncode == 0, process.returncode
            if legacy:
                assert json.loads(config.read_text())["decks"][0]["actions"][0]["id"] == "box-slot"

        result = {"peers": list(PEERS) if args.with_peers else [], "freshConfig": "passed",
                  "schema3Migration": "passed", "backgroundMode": "passed", "screenshots": screenshots}
    finally:
        errors = []
        for process in reversed(processes):
            try:
                stop(process)
            except subprocess.TimeoutExpired:
                errors.append(str(process.pid))
        for log in logs:
            log.close()
        assert not errors, "Started PIDs did not exit after SIGKILL: " + ", ".join(errors)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
