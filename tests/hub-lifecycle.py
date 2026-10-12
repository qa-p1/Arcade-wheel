#!/usr/bin/env python3
"""Wheel process lifecycle checks. Run ONLY through Link tools/e2e.py run --.

The executable copy simulates the two AppImage runtime paths to exercise
integration against the actual Wheel build, without touching the live profile.
CI additionally smoke-tests the real packaged AppImage.
"""
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

if os.environ.get("ARCADE_E2E_INNER") != "1" or not os.environ.get("ARCADE_HOME"):
    raise SystemExit("Use Arcade Link tools/e2e.py run -- python3 tests/hub-lifecycle.py EXE LINK_CLI")

source = Path(sys.argv[1]).resolve(strict=True)
link_cli = Path(sys.argv[2]).resolve(strict=True)
root = Path(os.environ["ARCADE_E2E_ROOT"])
env = os.environ.copy()
for name in ("ARCADE_DEV_BUILD", "ARCADE_NO_INSTALL_PROMPT"):
    env.pop(name, None)
residents = []
hosts = []

def run(executable, *args, environment=None, check=True):
    result = subprocess.run([str(executable), *args], env=environment or env,
                            capture_output=True, text=True, timeout=30)
    if check and result.returncode:
        raise AssertionError(f"{args}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result

def status(executable):
    result = run(executable, "--status", check=False)
    return json.loads(result.stdout) if result.returncode == 0 else {}

def wait_for(executable, predicate, timeout=10):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = status(executable)
        if predicate(last):
            return last
    raise AssertionError(f"status timeout: {last}")

def spawn_host():
    host = subprocess.Popen([str(link_cli), "tray", "host"], env=env, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    hosts.append(host)
    with selectors.DefaultSelector() as selected:
        selected.register(host.stdout, selectors.EVENT_READ)
        assert selected.select(10), "fake host did not become ready"
        assert host.stdout.readline().strip() == "ready"
    return host

def start(executable):
    log = open(root / "wheel-hub.log", "ab")
    process = subprocess.Popen([str(executable), "--background"], env=env, stdout=log, stderr=log)
    log.close()
    residents.append(process)
    return process

try:
    # --arcade-manifest and integration status must not install a dev build.
    manifest = json.loads(run(source, "--arcade-manifest").stdout)
    assert manifest["install"] == "dev" and manifest["docs"]["version"]
    assert manifest["menu"][0]["id"] == "show-wheel" and "logs" in manifest
    empty = json.loads(run(source, "--integration-status").stdout)
    assert empty["receipt"] is None
    assert not list(Path(env["ARCADE_HOME"]).glob("installs/*.json"))

    with tempfile.TemporaryDirectory(prefix="wheel-image-", dir=root) as directory:
        image_root = Path(directory)
        appdir = image_root / "mount"
        mounted = appdir / "usr/bin/arcade-wheel"
        downloaded = image_root / "Downloads/Arcade Wheel.AppImage"
        mounted.parent.mkdir(parents=True)
        downloaded.parent.mkdir()
        shutil.copy2(source, mounted)
        shutil.copy2(source, downloaded)
        image_env = dict(env, APPIMAGE=str(downloaded), APPDIR=str(appdir))
        receipt = json.loads(run(mounted, "--install", "--silent", "--no-launch", environment=image_env).stdout)
        installed = Path(receipt["path"])
        assert installed.name == "Arcade-Wheel.AppImage" and installed.is_file()
        assert receipt["method"] == "appimage" and receipt["managedBy"] == "self"
        assert installed.stat().st_mode & 0o111 == 0o111
        integration = receipt["integration"]
        desktop = Path(integration["desktopEntry"])
        cli = Path(integration["cli"])
        text = desktop.read_text()
        assert desktop.name == "com.arcadewheel.ArcadeWheel.desktop"
        assert "X-Arcade-Id=arcade.wheel" in text and "Actions=Settings;Quit;Uninstall;" in text
        assert str(installed) in text and str(appdir) not in text
        assert cli.is_symlink() and cli.resolve() == installed
        assert all(Path(path).exists() for path in integration["icons"])
        assert downloaded.exists()
        desktop.unlink()
        run(installed, "--repair")
        assert desktop.exists()
        status_json = json.loads(run(installed, "--integration-status").stdout)
        assert status_json["receipt"]["id"] == "arcade.wheel"
        assert all(entry["exists"] for entry in status_json["entries"])

        # Tools first: no startup ownership flash in the policy/unit suite.
        host = spawn_host()
        process = start(installed)
        wait_for(installed, lambda s: s.get("tray") == "hosted")
        methods = json.loads(run(link_cli, "call", "wheel", "describe").stdout)["methods"]
        assert all(method in methods for method in ["app.settings", "app.restart", "app.menu", "app.menu.invoke",
            "app.shortcuts.set", "app.settings.export", "app.settings.import"])
        assert json.loads(run(link_cli, "call", "wheel", "app.menu").stdout)["items"][0]["id"] == "show-wheel"
        exported = json.loads(run(link_cli, "call", "wheel", "app.settings.export").stdout)["outputs"]
        portable = json.loads(Path(exported[0]["path"]).read_text())
        portable["trigger"]["shortcut"] = "F9"
        Path(exported[0]["path"]).write_text(json.dumps(portable))
        imported = json.loads(run(link_cli, "call", "wheel", "app.settings.import", "--params", json.dumps({"inputs": exported})).stdout)
        assert imported["imported"] and not imported["restartRequired"]
        assert list(Path(os.environ["XDG_CONFIG_HOME"]).rglob("config.json.backup-*.json"))
        host.stdin.write("exclude wheel\n"); host.stdin.flush()
        wait_for(installed, lambda s: s.get("tray") == "own")
        host.stdin.write("include wheel\n"); host.stdin.flush()
        wait_for(installed, lambda s: s.get("tray") == "hosted")
        latency = time.monotonic(); host.kill(); host.wait(timeout=5)
        wait_for(installed, lambda s: s.get("tray") == "own", 0.95)
        assert time.monotonic() - latency < 1
        # Tools later, and its declared restart grace.
        host = spawn_host()
        wait_for(installed, lambda s: s.get("tray") == "hosted")
        host.stdin.write("restarting\n"); host.stdin.flush()
        with selectors.DefaultSelector() as selected:
            selected.register(host.stdout, selectors.EVENT_READ)
            assert selected.select(10)
            while "ok restarting" not in host.stdout.readline():
                pass
        host.kill(); host.wait(timeout=5)
        assert status(installed)["tray"] == "hosted"
        host = spawn_host()
        wait_for(installed, lambda s: s.get("tray") == "hosted")
        original_pid = status(installed)["pid"]
        run(link_cli, "call", "wheel", "app.restart")
        wait_for(installed, lambda s: s.get("pid") and s["pid"] != original_pid)
        process.wait(timeout=10)
        wait_for(installed, lambda s: s.get("tray") == "hosted")
        host.kill(); host.wait(timeout=5)
        wait_for(installed, lambda s: s.get("tray") == "own", 0.95)

        # An update preserves saved settings and writes previous/version.
        run(installed, "--quit")
        wait_for(installed, lambda s: not s.get("running"))
        receipt_path = Path(env["ARCADE_HOME"]) / "installs/arcade.wheel.json"
        previous = json.loads(receipt_path.read_text()); previous["version"] = "0.1.0"
        receipt_path.write_text(json.dumps(previous))
        updated = json.loads(run(mounted, "--install", "--silent", "--no-launch", environment=image_env).stdout)
        assert updated["previous"]["version"] == "0.1.0"
        assert Path(updated["previous"]["path"]).exists()
        config = next(Path(env["XDG_CONFIG_HOME"]).rglob("config.json"))
        assert json.loads(config.read_text())["trigger"]["shortcut"] == "F9"
        # Repair a moved file and then remove only receipt-owned integration.
        moved = image_root / "Moved Wheel.AppImage"
        installed.rename(moved)
        moved_env = dict(image_env, APPIMAGE=str(moved))
        repaired = json.loads(run(mounted, "--repair", environment=moved_env).stdout)
        assert Path(repaired["path"]) == moved
        assert cli.resolve() == moved
        marker = config.parent / "kept-data.txt"; marker.write_text("keep")
        run(moved, "--uninstall", "--silent")
        assert not moved.exists() and not desktop.exists() and not cli.exists()
        assert marker.exists() and not receipt_path.exists()
        assert all(not Path(path).exists() for path in integration["icons"])
        # Explicit data removal is also tested via the lifecycle runner.
        reinstalled = json.loads(run(mounted, "--install", "--silent", "--no-launch", environment=image_env).stdout)
        run(Path(reinstalled["path"]), "--uninstall", "--silent", "--remove-data")
        assert not marker.exists()

    if len(sys.argv) > 3:
        # Capture this isolated process's Settings window, never the real desktop.
        screenshot = Path(sys.argv[3]); screenshot.parent.mkdir(parents=True, exist_ok=True)
        process = start(source)
        wait_for(source, lambda s: s.get("running"))
        run(source, "--settings")
        window = subprocess.run(["xdotool", "search", "--sync", "--onlyvisible", "--pid", str(process.pid), "--name", "Arcade Wheel"],
            env=env, capture_output=True, text=True, timeout=10, check=True).stdout.splitlines()[0]
        raw = screenshot.with_name("settings-full.png")
        subprocess.run(["magick", "import", "-window", window, str(raw)], env=env, timeout=10, check=True)
        subprocess.run(["magick", str(raw), "-resize", "960x", str(screenshot)], env=env, timeout=10, check=True)
        raw.unlink(); assert screenshot.stat().st_size <= 300_000
        run(source, "--quit"); process.wait(timeout=10)
    print("Wheel hub lifecycle: install, update, repair, uninstall, settings, restart and tray recovery passed")
finally:
    for host in hosts:
        if host.poll() is None:
            host.kill(); host.wait(timeout=5)
    # A detached restart successor is reachable by the same isolated command socket.
    for executable in [source, locals().get("installed"), locals().get("moved")]:
        if executable and executable.exists():
            run(executable, "--quit", check=False)
    for process in residents:
        if process.poll() is None:
            process.terminate(); process.wait(timeout=5)
