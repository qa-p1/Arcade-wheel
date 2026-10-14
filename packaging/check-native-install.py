#!/usr/bin/env python3
"""Exercise the installed Windows/macOS app, only in CI or Link's isolated runner."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def run(executable, environment, *arguments, check=True):
    result = subprocess.run([str(executable), *arguments], env=environment,
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=60)
    if check and result.returncode:
        raise AssertionError(f"{arguments}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


def json_command(executable, environment, *arguments):
    return json.loads(run(executable, environment, *arguments).stdout)


def resident_round_trip(executable, environment, root, login_shortcut=None):
    with (root / "resident.log").open("wb") as log:
        process = subprocess.Popen([str(executable), "--background"], env=environment,
                                   stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 15
            while True:
                result = run(executable, environment, "--status", check=False)
                if result.returncode == 0 and json.loads(result.stdout).get("running"):
                    break
                assert process.poll() is None, "Installed resident exited before becoming ready"
                assert time.monotonic() < deadline, "Installed resident did not become ready"
            exported = root / "portable.json"
            run(executable, environment, "--settings-export", str(exported))
            settings = json.loads(exported.read_text(encoding="utf-8"))
            settings["decks"][0]["name"] = "Installed copy round trip"
            if login_shortcut:
                settings["general"]["startOnLogin"] = True
                exported.write_text(json.dumps(settings), encoding="utf-8")
                run(executable, environment, "--settings-import", str(exported))
                assert login_shortcut.exists(), "Settings missed the installer's login shortcut"
                settings["general"]["startOnLogin"] = False
                exported.write_text(json.dumps(settings), encoding="utf-8")
                run(executable, environment, "--settings-import", str(exported))
                receipt = json_command(executable, environment, "--integration-status")["receipt"]
                assert not login_shortcut.exists() and receipt["integration"]["autostart"] is None
                settings["general"]["startOnLogin"] = True
            exported.write_text(json.dumps(settings), encoding="utf-8")
            run(executable, environment, "--settings-import", str(exported))
            run(executable, environment, "--settings-export", str(exported))
            restored = json.loads(exported.read_text(encoding="utf-8"))
            assert restored["decks"][0]["name"] == settings["decks"][0]["name"]
            if login_shortcut:
                receipt = json_command(executable, environment, "--integration-status")["receipt"]
                assert login_shortcut.exists() and Path(receipt["integration"]["autostart"]) == login_shortcut
                assert restored["general"]["startOnLogin"] is True
                import winreg
                try:
                    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                                        r"Software\Microsoft\Windows\CurrentVersion\Run") as key:
                        winreg.QueryValueEx(key, "ArcadeWheel")
                except FileNotFoundError:
                    pass
                else:
                    raise AssertionError("Start at login left a duplicate legacy Run entry")
        except BaseException:
            log.flush()
            print((root / "resident.log").read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
            raise
        finally:
            run(executable, environment, "--quit", check=False)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def windows(executable):
    executable = executable.resolve(strict=True)
    arcade_root = Path(os.environ.get("ARCADE_HOME") or Path(os.environ["LOCALAPPDATA"]) / "Arcade")
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen",
                       QT_QUICK_BACKEND="software", ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT="1")
    manifest = json_command(executable, environment, "--arcade-manifest")
    assert manifest["install"] == "windows-installer" and manifest["menu"][0]["id"] == "show-wheel"
    assert (executable.parent / "docs/user/images/settings.png").is_file()
    status = json_command(executable, environment, "--integration-status")
    receipt = status["receipt"]
    assert Path(receipt["path"]) == executable and all(entry["exists"] for entry in status["entries"])
    assert receipt["integration"]["desktopShortcut"] is None, "Desktop shortcut should be opt-in"
    startup = Path(receipt["integration"]["autostart"])
    assert startup.is_file()
    with tempfile.TemporaryDirectory(prefix="wheel-installed-", dir=os.environ["RUNNER_TEMP"]) as directory:
        resident_round_trip(executable, environment, Path(directory), startup)
        repaired = json_command(executable, environment, "--repair")
        assert Path(repaired["path"]) == executable and Path(repaired["integration"]["cli"]).is_file()
    assert (arcade_root / "apps/arcade.wheel.json").is_file(), "Resident did not write its Link manifest"
    print("Installed Windows copy: manifest, docs, settings, login toggle and repair passed")


def macos(bundle):
    with tempfile.TemporaryDirectory(prefix="wheel-native-", dir=os.environ["RUNNER_TEMP"]) as directory:
        root = Path(directory)
        home = root / "home"
        cli_folder = home / ".local/bin"
        environment = dict(os.environ, HOME=str(home), ARCADE_HOME=str(home),
                           XDG_CONFIG_HOME=str(home / ".config"), XDG_CACHE_HOME=str(home / ".cache"),
                           XDG_RUNTIME_DIR=str(home / "run"), QT_QPA_PLATFORM="offscreen",
                           QT_QUICK_BACKEND="software", ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT="1",
                           PATH=f"{cli_folder}{os.pathsep}{os.environ['PATH']}")
        home.mkdir()
        (home / "run").mkdir()
        download = root / "Downloads/Arcade Wheel.app"
        subprocess.run(["/usr/bin/ditto", str(bundle.resolve(strict=True)), str(download)], check=True, timeout=120)
        source = download / "Contents/MacOS/arcade-wheel"
        receipt = json_command(source, environment, "--install", "--silent", "--no-launch", "--channel", "nightly")
        installed = Path(receipt["path"])
        assert installed == home / "Applications/Arcade Wheel.app"
        assert receipt["method"] == "macos-bundle" and receipt["channel"] == "nightly"
        executable = installed / "Contents/MacOS/arcade-wheel"
        cli = Path(receipt["integration"]["cli"])
        assert cli.is_symlink() and cli.resolve(strict=True) == executable
        assert (installed / "Contents/Resources/docs/user/images/settings.png").is_file()
        manifest = json_command(executable, environment, "--arcade-manifest")
        assert manifest["install"] == "macos-bundle" and manifest["docs"]["version"] == receipt["version"]
        resident_round_trip(executable, environment, root)
        moved = home / "Applications/Moved Wheel.app"
        installed.rename(moved)
        executable = moved / "Contents/MacOS/arcade-wheel"
        repaired = json_command(executable, environment, "--repair")
        assert Path(repaired["path"]) == moved and cli.resolve(strict=True) == executable
        status = json_command(executable, environment, "--integration-status")
        assert all(entry["exists"] for entry in status["entries"])
        run(executable, environment, "--uninstall", "--silent")
        assert not moved.exists() and not cli.is_symlink()
        assert not (home / "installs/arcade.wheel.json").exists()
        assert not (home / "apps/arcade.wheel.json").exists()
        assert download.is_dir(), "Explicit CLI installation should preserve the downloaded bundle"
    print("Installed macOS copy: install, manifest, docs, settings, moved-bundle repair and uninstall passed")


def main():
    if os.environ.get("CI") != "true" and os.environ.get("ARCADE_E2E_INNER") != "1":
        raise SystemExit("Run native installation checks in CI or Link's isolated session")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=("windows", "macos"))
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    if args.platform == "windows":
        windows(args.app)
    else:
        macos(args.app)


if __name__ == "__main__":
    main()
