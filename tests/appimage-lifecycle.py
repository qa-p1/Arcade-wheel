#!/usr/bin/env python3
"""Install and uninstall the shipped AppImage in Link's isolated session."""
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time


if os.environ.get("ARCADE_E2E_INNER") != "1":
    raise SystemExit("Run through Link tools/e2e.py run --")

artifact = Path(sys.argv[1]).resolve(strict=True)
with tempfile.TemporaryDirectory(prefix="wheel-package-", dir=os.environ["ARCADE_E2E_ROOT"]) as temporary:
    root = Path(temporary)
    environment = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1", ARCADE_NO_INSTALL_PROMPT="1")
    for key, name in (("HOME", "home"), ("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                      ("XDG_CACHE_HOME", "cache"), ("ARCADE_HOME", "arcade"), ("ARCADE_WHEEL_INSTANCE", "wheel")):
        environment[key] = str(root / name)
        (root / name).mkdir()
    for key in ("APPIMAGE", "APPDIR", "ARCADE_DEV_BUILD"):
        environment.pop(key, None)
    source = root / "Downloads/Arcade Wheel.AppImage"
    source.parent.mkdir()
    shutil.copy2(artifact, source)

    def run(executable, *arguments):
        result = subprocess.run([str(executable), *arguments], env=environment, capture_output=True,
                                text=True, timeout=60)
        assert result.returncode == 0, (arguments, result.stdout, result.stderr)
        return json.loads(result.stdout) if result.stdout.strip() else None

    assert run(source, "--integration-status")["receipt"] is None
    receipt = run(source, "--install", "--silent", "--no-launch")
    installed = Path(receipt["path"])
    assert receipt["method"] == "appimage" and receipt["id"] == "arcade.wheel"
    integration = receipt["integration"]
    paths = [installed, root / "arcade/installs/arcade.wheel.json"]
    paths += [Path(integration[key]) for key in ("desktopEntry", "cli", "autostart") if integration.get(key)]
    paths += list(map(Path, integration["icons"]))
    assert all(path.is_relative_to(root) and path.exists() for path in paths)
    assert installed.read_bytes() == artifact.read_bytes()
    assert Path(integration["cli"]).is_symlink()
    assert all(entry["exists"] for entry in run(installed, "--integration-status")["entries"])

    # U1: a start that keeps Wheel resident hands over to an unpacked copy and exits 0 at once, and Wheel carries
    # on from <data>/arcade/runtime/wheel-<version>-<16 hex digits>/, also after Restart. The isolated session sets
    # ARCADE_UNPACKED=1 so that other checks keep the process they started; this one needs the real thing.
    resident_environment = {key: value for key, value in environment.items() if key != "ARCADE_UNPACKED"}
    # With ARCADE_HOME set, Link's installer looks for the data folder under it and ignores XDG_DATA_HOME, which the
    # unpacked copies follow (without ARCADE_HOME both are the same folder), so point XDG_DATA_HOME where Link looks.
    data_home = Path(environment["ARCADE_HOME"]) / ".local/share"
    resident_environment["XDG_DATA_HOME"] = str(data_home)
    version = subprocess.run([str(installed), "--version"], env=resident_environment, capture_output=True, text=True,
                             timeout=60, check=True).stdout.split()[-1]
    runtime = (data_home / "arcade/runtime").resolve()
    copy_pattern = re.compile(re.escape(str(runtime)) + "/wheel-" + re.escape(version) + "-[0-9a-f]{16}/usr/bin/arcade-wheel")
    assert not runtime.exists(), "One-shot commands must not unpack Wheel"

    def resident_pid(different_from=None):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            status = subprocess.run([str(installed), "--status"], env=resident_environment, capture_output=True,
                                    text=True, timeout=30)
            if status.returncode == 0 and json.loads(status.stdout)["pid"] != different_from:
                return json.loads(status.stdout)["pid"]
            time.sleep(.2)
        raise AssertionError("Wheel did not report a new resident")

    def gone(pid):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if not Path(f"/proc/{pid}").exists():
                return True
            time.sleep(.1)
        return False

    resident = None
    try:
        with (root / "resident.log").open("wb") as log:
            started = subprocess.run([str(installed), "--background"], env=resident_environment, stdout=log,
                                     stderr=log, timeout=60)
        assert started.returncode == 0, (started.returncode, (root / "resident.log").read_text(errors="replace"))
        resident = resident_pid()
        assert copy_pattern.fullmatch(os.readlink(f"/proc/{resident}/exe")), os.readlink(f"/proc/{resident}/exe")
        assert len(list(runtime.glob("wheel-*"))) == 1, list(runtime.iterdir())
        subprocess.run([str(installed), "--restart"], env=resident_environment, check=True, timeout=60)
        successor = resident_pid(different_from=resident)
        assert gone(resident), "Restart left the old resident running"
        resident = successor
        assert copy_pattern.fullmatch(os.readlink(f"/proc/{resident}/exe")), "Restart left the unpacked copy: " + os.readlink(f"/proc/{resident}/exe")
        assert len(list(runtime.glob("wheel-*"))) == 1, list(runtime.iterdir())
        subprocess.run([str(installed), "--quit"], env=resident_environment, check=True, timeout=60)
        assert gone(resident), "--quit left the resident running"
        resident = None
    finally:
        if resident is not None:
            try:
                os.kill(resident, signal.SIGKILL)
            except ProcessLookupError:
                pass
    # Uninstall removes Wheel's copies and leaves other apps' (Link ArcadeInstall::uninstall).
    other_app_copy = runtime / "shelf-1.0.0-0123456789abcdef"
    other_app_copy.mkdir()
    sentinel = root / "wheel/keep.txt"
    sentinel.write_text("keep app data")
    run(installed, "--uninstall", "--silent")
    for path in paths:
        assert not path.exists() and not path.is_symlink(), f"Uninstall retained {path}"
    assert sentinel.read_text() == "keep app data"
    assert not list(runtime.glob("wheel-*")), "Uninstall retained Wheel's unpacked copy"
    assert other_app_copy.is_dir(), "Uninstall removed another app's unpacked copy"
    assert source.exists(), "Explicit CLI install should preserve the downloaded file"
print("AppImage install, receipt, launcher, icons, CLI, resident unpacked copy and restart, uninstall and data preservation passed (CI only).")
