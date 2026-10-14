#!/usr/bin/env python3
"""Install and uninstall the shipped AppImage in Link's isolated session."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


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
    sentinel = root / "wheel/keep.txt"
    sentinel.write_text("keep app data")
    run(installed, "--uninstall", "--silent")
    for path in paths:
        assert not path.exists() and not path.is_symlink(), f"Uninstall retained {path}"
    assert sentinel.read_text() == "keep app data"
    assert source.exists(), "Explicit CLI install should preserve the downloaded file"
print("AppImage install, receipt, launcher, icons, CLI, uninstall and data preservation passed (CI only).")
