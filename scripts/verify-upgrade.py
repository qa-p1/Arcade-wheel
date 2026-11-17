#!/usr/bin/env python3
"""Upgrade from a published Wheel release to a new one, keeping the user's data (release gate "Upgrade test").

    dbus-run-session -- xvfb-run -a python3 scripts/verify-upgrade.py OLD.AppImage NEW.AppImage

OLD is the previous release's published file (check it against its SHA256SUMS.txt first), NEW the candidate's. In a
private profile: OLD is installed from a download folder (`--install --silent --no-launch`, which also adds the login
entry), started with `--background`, and given the data a user of it has: a customised wheel (three decks, each with
different action types, a centre gesture with actions) and non-default settings (theme, notifications, appearance,
behaviour, trigger). Wheel keeps all of that in one file, <XDG_CONFIG_HOME>/arcade/wheel/settings.json, and has a
supported way to fill it: `--settings-import FILE` on the running Wheel, the same code as Settings > Import (it
validates the file, backs the current one up, saves and applies it). So the data is written by OLD itself and nothing
is hand-written in the shape ConfigStore uses. OLD is then stopped and started again and must still hold it.
The files of Wheel's config folder are then snapshotted (exact bytes and parsed settings). NEW is downloaded and
installed over OLD while OLD runs, the way a user updates by hand, and started. NEW must replace the installed copy
in place, stop OLD, report its own version, keep every wheel and setting value (a newer Wheel may add keys, but not
drop or change the user's), keep exactly one launcher and at most one login entry, both starting the installed file,
and stop on --quit.

A dry run against a local build of the same version sets VERIFY_UPGRADE_SAME_VERSION=1: OLD and NEW may then be the
same bytes (or the same version number); everything else is still checked.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

APP_ID = "arcade.wheel"
LAUNCHER = "dev.arcade.Wheel.desktop"
FILENAME = "Arcade-Wheel.AppImage"
SAME_VERSION = os.environ.get("VERIFY_UPGRADE_SAME_VERSION") == "1"

# What the user has set up. Defaults are in ConfigStore::defaults(): theme "system", showNotifications true, radius 148,
# labels false, opacity 0.94, customAccent false, deadZone 64, scrollReverse false, shortcut F8, holdThresholdMs 0,
# one "Applications" deck of six slots, every centre gesture off and empty. Action ids are unique across the file.
SETTINGS = {
    "general": {"theme": "dark", "showNotifications": False},
    "trigger": {"shortcut": "F9", "holdThresholdMs": 150},
    "appearance": {"radius": 180, "labels": True, "opacity": 0.8, "customAccent": True, "accentColor": "#22aa66"},
    "behaviour": {"deadZone": 90, "scrollReverse": True, "wrapDecks": False},
}
DECKS = [
    {"id": "deck-work", "name": "Work", "actions": [
        {"id": "act-term", "type": "command", "name": "Build in a terminal", "icon": "utilities-terminal",
         "payload": {"command": "sh -c 'echo upgrade-test'", "workingDirectory": "/tmp"}},
        {"id": "act-docs", "type": "url", "name": "Project docs", "icon": "text-html",
         "payload": {"url": "https://example.invalid/docs"}},
        {"id": "act-dir", "type": "file", "name": "Notes folder", "icon": "folder",
         "payload": {"path": "/tmp"}},
        {"id": "act-lock", "type": "system", "name": "Lock the screen", "icon": "system-lock-screen",
         "payload": {"id": "lock"}},
        {"id": "act-app", "type": "application", "name": "Files", "icon": "system-file-manager",
         "payload": {"desktopId": "org.example.Files.desktop"}},
    ]},
    {"id": "deck-media", "name": "Media", "actions": [
        {"id": "act-mute", "type": "media", "name": "Mute", "icon": "audio-volume-muted", "payload": {"id": "mute"}},
        {"id": "act-up", "type": "media", "name": "Louder", "icon": "audio-volume-high", "payload": {"id": "volume-up"}},
        {"id": "act-none", "type": "none", "name": "Choose action", "icon": "applications-other", "payload": {}},
        {"id": "act-radio", "type": "url", "name": "Radio", "icon": "audio-x-generic",
         "payload": {"url": "https://example.invalid/radio"}},
    ]},
    {"id": "deck-empty", "name": "Spare", "actions": []},
]
GESTURES = {
    "clickIntervalMs": 320,
    "longPressMs": 700,
    "doubleClick": {"name": "Morning routine", "enabled": True, "actions": [
        {"id": "act-g1", "type": "url", "name": "Mail", "icon": "mail-unread",
         "payload": {"url": "https://example.invalid/mail"}},
        {"id": "act-g2", "type": "command", "name": "Say hello", "icon": "utilities-terminal",
         "payload": {"command": "echo hello"}},
    ]},
}


def user_import():
    """The portable settings file the user imports: schemaVersion 4, sections as above, the decks and the gestures."""
    return {"schemaVersion": 4, **SETTINGS, "decks": DECKS, "centerGestures": GESTURES}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def alive(pid):
    """Running, not a zombie."""
    try:
        return Path(f"/proc/{pid}/stat").read_text().rpartition(") ")[2].split()[0] != "Z"
    except OSError:
        return False


def wheel_processes(root):
    """Processes of this profile: running a file under `root` (the installed AppImage, its mount or extraction, an
    unpacked copy under <data>/arcade/runtime/wheel-*), or started with its ARCADE_HOME."""
    found = {}
    marker = b"ARCADE_HOME=" + str(root / "arcade").encode()
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit() or int(proc.name) == os.getpid():
            continue
        try:
            exe = os.readlink(proc / "exe")
            environment = (proc / "environ").read_bytes().split(b"\0")
        except OSError:
            continue
        if (exe.startswith(str(root) + "/") or marker in environment) and alive(proc.name):
            found[int(proc.name)] = exe
    return found


def snapshot_config(folder):
    """{file name: (bytes, parsed JSON or None)} for every file in Wheel's config folder."""
    result = {}
    for path in sorted(folder.iterdir()):
        if path.is_file():
            data = path.read_bytes()
            try:
                parsed = json.loads(data)
            except ValueError:
                parsed = None
            result[path.name] = (data, parsed)
    return result


def is_kept(old, new, where="settings"):
    """None when everything in `old` is in `new` with the same value, else what differs. Objects may gain keys in
    `new` (a newer Wheel adds settings); lists (decks, actions) must keep their length and each element."""
    if isinstance(old, dict):
        if not isinstance(new, dict):
            return f"{where}: was an object, is {new!r}"
        for key, value in old.items():
            if key not in new:
                return f"{where}.{key}: dropped (was {value!r})"
            if problem := is_kept(value, new[key], f"{where}.{key}"):
                return problem
        return None
    if isinstance(old, list):
        if not isinstance(new, list) or len(new) != len(old):
            return f"{where}: was {len(old)} entries, is {new!r}"
        for index, value in enumerate(old):
            if problem := is_kept(value, new[index], f"{where}[{index}]"):
                return problem
        return None
    if old != new or type(old) is not type(new):
        return f"{where}: was {old!r}, is {new!r}"
    return None


def main():
    old_image, new_image = (Path(arg).resolve(strict=True) for arg in sys.argv[1:3])
    if not SAME_VERSION:
        assert digest(old_image) != digest(new_image), "OLD and NEW are the same file"
    with tempfile.TemporaryDirectory(prefix="wheel-upgrade-", dir=os.environ.get("RUNNER_TEMP")) as directory:
        root = Path(directory).resolve()
        for parent in (root, *root.parents):
            if (parent / ".git").exists():
                raise RuntimeError("Upgrade verification must run outside the checkout")
        # Wheel's command socket is <XDG_RUNTIME_DIR>/arcade-wheel-<12 hex>.sock; a unix socket path holds 107 bytes.
        assert len(str(root / "run")) + len("/arcade-wheel-0123456789ab.sock") < 107, (
            f"{root} is too deep for Wheel's command socket; set RUNNER_TEMP or TMPDIR to a short folder")
        env = {k: v for k, v in os.environ.items() if not k.startswith("ARCADE_") and k not in (
            "APPIMAGE", "APPDIR", "OWD", "ARGV0", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "WAYLAND_DISPLAY",
            "HYPRLAND_INSTANCE_SIGNATURE", "SWAYSOCK", "QT_QPA_PLATFORM")}
        arcade = root / "arcade"
        # With ARCADE_HOME set, Link's installer looks for the data folder (the unpacked copies under arcade/runtime)
        # under it, while Wheel's copies follow XDG_DATA_HOME: both are the one folder without ARCADE_HOME.
        env.update(ARCADE_HOME=str(arcade), HOME=str(root / "home"), XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(arcade / ".local/share"), XDG_CACHE_HOME=str(root / "cache"),
                   XDG_RUNTIME_DIR=str(root / "run"), TMPDIR=str(root / "tmp"), ARCADE_NO_INSTALL_PROMPT="1",
                   GDK_BACKEND="x11", APPIMAGE_EXTRACT_AND_RUN="1", LC_ALL="C.UTF-8")
        for name in ("home", "config", "cache", "run", "tmp", "arcade"):
            (root / name).mkdir(mode=0o700)
        downloads = root / "home/Downloads"
        downloads.mkdir()
        installed = arcade / "Applications/Arcade" / FILENAME
        config_folder = root / "config/arcade/wheel"
        settings_file = config_folder / "settings.json"
        runtime = arcade / ".local/share/arcade/runtime"
        launcher = arcade / ".local/share/applications" / LAUNCHER
        autostart_folder = arcade / ".config/autostart"
        log = (root / "wheel.log").open("w+b")
        launched = []

        def download(image):
            copy = downloads / image.name
            shutil.copy2(image, copy)
            copy.chmod(0o755)
            return copy

        def run(executable, *args, check=True, timeout=120):
            try:
                result = subprocess.run([str(executable), *args], env=env, capture_output=True, text=True,
                                        timeout=timeout)
            except subprocess.TimeoutExpired:
                raise AssertionError(f"{Path(executable).name} {' '.join(args)} did not finish in {timeout}s")
            if check:
                assert result.returncode == 0, (
                    f"{Path(executable).name} {' '.join(args)} exited {result.returncode}: {result.stderr[-2000:]}")
            return result.stdout

        def status():
            """--status of the resident (exit 3 and running:false when there is none)."""
            result = subprocess.run([str(installed), "--status"], env=env, capture_output=True, text=True, timeout=60)
            return json.loads(result.stdout.strip().splitlines()[-1]) if result.stdout.strip() else {}

        def wait(probe, what, seconds=30):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if result := probe():
                    return result
                time.sleep(0.2)
            raise AssertionError(f"Timed out waiting for {what}")

        def running():
            try:
                info = status()
            except (AssertionError, ValueError, subprocess.TimeoutExpired):
                return None
            return info if info.get("running") else None

        def start():
            launched.append(subprocess.Popen([str(installed), "--background"], env=env, stdout=log,
                                             stderr=subprocess.STDOUT, start_new_session=True))
            return wait(running, "Wheel to report running in --status", 60)

        def stop():
            run(installed, "--quit", timeout=60)
            wait(lambda: not running() and not wheel_processes(root) and all(p.poll() is not None for p in launched),
                 "every Wheel process to exit after --quit", 30)

        try:
            # OLD is installed from a download.
            old_version = run(download(old_image), "--version").strip().split()[-1]
            receipt = json.loads(run(downloads / old_image.name, "--install", "--silent", "--no-launch"))
            assert Path(receipt["path"]) == installed, receipt
            assert receipt["version"] == old_version and receipt["id"] == APP_ID, receipt
            assert digest(installed) == digest(old_image), "The installed copy is not OLD's bytes"
            assert Path(receipt["integration"]["desktopEntry"]) == launcher, receipt["integration"]
            assert not wheel_processes(root), "--no-launch started Wheel"

            first = start()
            assert first["version"] == old_version, first
            assert settings_file.is_file(), "OLD wrote no settings.json at start"

            # The user's wheel: Wheel's own Import (the running Wheel validates, backs up and saves the file).
            wanted = root / "home/my-wheel.json"
            wanted.write_text(json.dumps(user_import(), indent=2))
            imported = run(installed, "--settings-import", str(wanted), timeout=60)
            assert "Imported settings" in imported, imported
            wait(lambda: any(p.name.startswith("settings.json.backup-") for p in config_folder.iterdir()),
                 "OLD to back up the settings it replaced", 20)
            saved = json.loads(settings_file.read_text())
            assert saved["schemaVersion"] >= 4, saved["schemaVersion"]
            assert [d["name"] for d in saved["decks"]] == ["Work", "Media", "Spare"], saved["decks"]
            # Wheel pads every deck to at least four slots with empty ones (normalize()), so compare what the user set.
            for deck, user in zip(saved["decks"], DECKS):
                assert deck["id"] == user["id"] and deck["actions"][:len(user["actions"])] == user["actions"], deck
            for section, values in SETTINGS.items():
                for key, value in values.items():
                    assert saved[section][key] == value and type(saved[section][key]) is type(value), (section, key)
            assert saved["centerGestures"]["doubleClick"]["actions"] == GESTURES["doubleClick"]["actions"]
            assert saved["centerGestures"]["doubleClick"]["enabled"] is True
            assert saved["centerGestures"]["longPressMs"] == 700

            # Stopped and started again, OLD must load the file and keep it as it is.
            stop()
            at_import = json.loads(settings_file.read_text())
            old_resident = start()
            assert old_resident["version"] == old_version and not old_resident["error"], old_resident
            assert old_resident["deckIndex"] == 0, old_resident
            time.sleep(3)  # OLD's startup (application scan, Link registration) has run by now
            before = snapshot_config(config_folder)
            assert before["settings.json"][1] == at_import, f"{old_version} changed the settings file when it loaded it"
            assert any(name.startswith("settings.json.backup-") for name in before), sorted(before)
            old_pids = dict(wheel_processes(root))
            old_pid = old_resident["pid"]
            assert old_pid in old_pids, (old_pid, old_pids)
            print(f"{old_version} installed at {installed} and running (PID {old_pid}, {old_pids[old_pid]}) with the "
                  f"user's data: {len(saved['decks'])} decks, "
                  f"{sum(len(d['actions']) for d in saved['decks'])} slots, "
                  f"{sum(len(v) for v in SETTINGS.values())} non-default settings", flush=True)

            # The update is by hand while OLD runs: download NEW, double click (--install, then start it).
            new_download = download(new_image)
            new_version = run(new_download, "--version").strip().split()[-1]
            assert SAME_VERSION or new_version != old_version, f"NEW is {new_version}, the same release as OLD"
            # Like a double click, the update install is not --no-launch: it stops OLD, installs and starts NEW with
            # --background. Output goes to a file, not a pipe: the started Wheel inherits it.
            with (root / "update.log").open("w+b") as update:
                try:
                    result = subprocess.run([str(new_download), "--install", "--silent"], env=env, stdout=update,
                                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
                except subprocess.TimeoutExpired:
                    raise AssertionError("NEW's --install did not finish in 180s")
                update.seek(0)
                output = update.read().decode(errors="replace")
            assert result.returncode == 0, f"NEW --install exited {result.returncode}: {output[-2000:]}"
            upgraded = json.JSONDecoder().raw_decode(output[output.index("{"):])[0]
            wait(lambda: not any(alive(pid) for pid in old_pids), "every process of OLD to exit during the install", 30)
            assert digest(installed) == digest(new_image), "The installed copy is not NEW's bytes"
            if not SAME_VERSION:
                assert digest(installed) != digest(old_image), "The installed copy is still OLD's bytes"
            assert upgraded["version"] == new_version and Path(upgraded["path"]) == installed, upgraded
            assert upgraded["installedAt"] == receipt["installedAt"], "The upgrade reset the original install date"
            assert upgraded.get("previous", {}).get("version") == old_version, (
                f"The receipt does not record the replaced {old_version}: {upgraded.get('previous')}")

            new_resident = wait(lambda: (i if (i := running()) and i["pid"] != old_pid else None),
                                "NEW to be started by the install", 60)
            assert new_resident["version"] == new_version, f"--status reports {new_resident['version']}, not {new_version}"
            assert not any(alive(pid) for pid in old_pids), f"OLD's processes are still there: {old_pids}"
            assert old_pid not in wheel_processes(root), "OLD's process is still there"
            assert not new_resident["error"], new_resident
            new_exe = os.readlink(f"/proc/{new_resident['pid']}/exe")
            time.sleep(3)  # NEW's startup has run

            # The integration is NEW's: one launcher, at most one login entry, both starting the installed file.
            integration = json.loads(run(installed, "--integration-status", timeout=60))
            assert integration["receipt"]["version"] == new_version, integration["receipt"]
            assert Path(integration["receipt"]["path"]) == installed, integration["receipt"]
            missing = [entry["path"] for entry in integration["entries"] if not entry["exists"]]
            assert not missing, f"Integration entries missing after the upgrade: {missing}"
            entries = upgraded["integration"]
            assert Path(entries["desktopEntry"]) == launcher, f"The launcher moved: {entries['desktopEntry']}"
            launchers = [path for path in launcher.parent.glob("*.desktop")
                         if f"X-Arcade-Id={APP_ID}" in path.read_text() or path.name == LAUNCHER
                         or "wheel" in path.name.lower()]
            assert launchers == [launcher], f"Expected one Wheel launcher, found {launchers}"
            assert f'Exec="{installed}"\n' in launcher.read_text(), launcher.read_text()
            logins = [path for path in autostart_folder.glob("*.desktop")
                      if f"X-Arcade-Id={APP_ID}" in path.read_text() or "wheel" in path.name.lower()]
            assert len(logins) <= 1, f"More than one Wheel login entry: {logins}"
            for login in logins:
                text = login.read_text()
                assert f'Exec="{installed}" "--background"' in text, f"{login} does not start the installed file:\n{text}"
                assert "Hidden=true" not in text and "X-GNOME-Autostart-enabled=false" not in text, text
            if entries.get("autostart"):
                assert logins == [Path(entries["autostart"])], f"The login entry is not the receipt's: {logins}"
            cli = Path(entries["cli"])
            assert cli.resolve() == installed, f"{cli} -> {cli.resolve()}"
            for icon in entries["icons"]:
                assert Path(icon).is_file(), f"Icon {icon} is missing"
            beside = sorted(path.name for path in installed.parent.iterdir() if path.name.endswith(".AppImage"))
            assert beside == [FILENAME], f"Other packages beside the installed copy: {beside}"

            stop()
            assert not running(), "NEW still runs after --quit"
            assert not wheel_processes(root), f"Wheel processes left after --quit: {wheel_processes(root)}"
            assert b"panicked at" not in (root / "wheel.log").read_bytes(), "Wheel logged a panic"

            # The data, read after NEW ran and stopped: every wheel slot and setting OLD had, with the same values.
            after = snapshot_config(config_folder)
            problem = is_kept(before["settings.json"][1], after["settings.json"][1])
            assert not problem, f"{new_version} changed the user's settings: {problem}"
            for name, (data, _) in before.items():
                if name != "settings.json":
                    assert name in after and after[name][0] == data, f"{new_version} changed or removed {name}"
            settings = after["settings.json"][1]
            for deck, user in zip(settings["decks"], DECKS):
                assert deck["id"] == user["id"] and deck["actions"][:len(user["actions"])] == user["actions"], deck
            for section, values in SETTINGS.items():
                for key, value in values.items():
                    assert settings[section][key] == value, f"Setting {section}.{key} is {settings[section][key]!r}"
            added = sorted(set(settings) - set(before["settings.json"][1]))
            same_bytes = after["settings.json"][0] == before["settings.json"][0]
            print(f"{new_version} replaced {old_version} in place (PID {new_resident['pid']}, {new_exe}): "
                  f"{len(settings['decks'])} decks, {sum(len(d['actions']) for d in settings['decks'])} slots and "
                  f"{sum(len(v) for v in SETTINGS.values())} settings kept (settings.json "
                  f"{'untouched' if same_bytes else 'rewritten'}, new top-level keys: {added or 'none'}); one launcher, "
                  f"{len(logins)} login entry, both start the installed file", flush=True)
        except Exception:
            log.seek(0)
            print(log.read().decode(errors="replace")[-6000:], file=sys.stderr)
            raise
        finally:
            if installed.exists():
                try:
                    subprocess.run([str(installed), "--quit"], env=env, capture_output=True, timeout=60)
                    time.sleep(1)
                except (subprocess.TimeoutExpired, OSError):
                    pass
            for pid in wheel_processes(root):
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            for process in launched:
                if process.poll() is None:
                    process.kill()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    pass
    print(f"PASS: {old_version} -> {new_version} keeps the user's wheel and settings and the integration")


if __name__ == "__main__":
    main()
