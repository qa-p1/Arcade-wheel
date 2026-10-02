<p align="center">
  <img src="assets/arcade-wheel.svg" width="96" alt="Arcade Wheel icon">
</p>

<h1 align="center">Arcade Wheel</h1>

<p align="center">
  A radial launcher for the desktop. Hold a key, flick toward an action, release.
</p>

---

Arcade Wheel is a small background launcher that works like a game weapon wheel. Hold a global shortcut and a wheel of actions appears. Move the pointer toward one and release the key to run it. Release in the center and the wheel closes without doing anything. Scroll while holding to switch decks. Click, double-click, triple-click, or long-press the center to run whole groups of apps at once.

It targets **Linux Wayland (especially Hyprland)** and **Windows**. It is written in C++20 with Qt 6 and Qt Quick. macOS is not supported.

## Features

- **Hold → move → release.** You don't have to aim at an icon; each action owns a whole slice of the circle.
- **Decks.** Up to 8 actions per deck, any number of decks. Scroll while holding the trigger to switch.
- **Center gestures.** Single-click, double-click, triple-click, and long press in the hub can each run a group of up to 16 apps or actions.
- **Actions:** launch or focus applications, run commands, open URLs, files, or folders, system actions (lock, suspend, log out, power off, screenshot), media keys, compositor actions, and external providers such as [Arcade Box](docs/ARCADE_BOX_BRIDGE.md).
- **Live settings.** Changes apply to the running wheel immediately, and the Wheel page has a live preview.
- **Plain JSON config**, saved atomically, with versioned migrations and import/export.
- **Quiet UI.** The wheel shows action icons, a center label, and deck dots. Nothing else.

## Requirements

- CMake 3.25+ and a C++20 compiler
- Qt **6.8+**: Core, Gui, Qml, Quick, QuickControls2, Widgets, Network, Test
- **Linux:** Qt DBus, GLib/GIO (`gio-unix-2.0`), pkg-config, and KDE's [LayerShellQt](https://github.com/KDE/layer-shell-qt) for the Wayland overlay. Global shortcuts need a compositor portal that implements `org.freedesktop.portal.GlobalShortcuts` (Hyprland's does).
- **Windows:** MSVC with a Qt 6 MSVC kit

On Arch-based systems:

```sh
sudo pacman -S cmake ninja qt6-base qt6-declarative layer-shell-qt xdg-desktop-portal-hyprland
```

## Build and install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

On Linux the build stops if LayerShellQt is missing. For a development-only build without it, pass `-DARCADE_REQUIRE_LAYERSHELLQT=OFF`; the wheel then opens as a normal window and may not stay above other windows.

On Windows, configure with an MSVC Qt 6 kit, build with CMake, then run `packaging/windows/package.ps1` to bundle the Qt runtime into a ZIP. CI builds this ZIP for every push.

From a source checkout, `./arcade-wheel` runs the binary in `build/`.

## Usage

```sh
arcade-wheel --settings     # start in the background and open Settings (the default)
arcade-wheel --background   # start in the background with no window
```

Closing Settings leaves the wheel running. A tray menu appears when the desktop provides a system tray. The default shortcut is **F8**, and the desktop may ask you to approve it the first time.

- **Hold** the shortcut. The wheel opens in the middle of the screen the pointer is on, and the pointer jumps to its center.
- **Move** toward an action. It highlights and its name appears in the center.
- **Release** to run it. Releasing in the center closes the wheel without doing anything.
- **Scroll** while holding to change decks.
- **Right-click** to cancel.

The first deck starts with up to six of your installed graphical apps; any remaining slots are left empty for you to fill. If a saved app or provider goes missing, its slot stays in Settings with the reason it is unavailable.

### Center gestures

Open **Settings → Center gestures** to assign a group of apps or actions to **Single-click**, **Double-click**, **Triple-click**, or **Long press**. Each group has a name, an enabled switch, and up to 16 actions that run in order. All gestures start unassigned.

While holding the trigger, use the left mouse button on the wheel's center:

- The group runs as soon as the gesture is recognized, and the wheel closes. Releasing the trigger afterward does not run anything a second time.
- With **no** single-click group, a single center click still closes the wheel.
- A click waits for the **time between clicks** if a longer gesture could still follow. For example, a single click waits while double- or triple-click is enabled. Releasing the trigger runs whatever is pending right away.
- Moving out of the center, right-clicking, or scrolling to another deck cancels a pending gesture.
- If you don't click at all, releasing the trigger in the center just closes the wheel, as before.
- **Long press** duration is adjustable (250–1500 ms). Click timing is adjustable from 160 to 500 ms.

Groups are saved with the rest of the configuration and included in import/export. If one action fails, the rest of the group still runs and the error is reported.

### Command-line reference

When Arcade Wheel is already running, each command is sent to the running instance.

| Command | Effect |
| --- | --- |
| `--settings` | Open Settings (default when no argument is given) |
| `--background` | Start without opening any window |
| `--show` | Open the wheel in click mode; click an action, right-click to cancel |
| `--preview` | Show the real overlay in a safe mode that never runs actions |
| `--cancel` | Close the wheel |
| `--press` / `--release` | Simulate the trigger (this **does** run the selected action) |
| `--status` | Print the running instance's status as JSON |
| `--restart` / `--quit` | Restart or quit the running instance |

## Configuration

The configuration is a versioned JSON file. Its path is shown on the **General** settings page (on Linux, `~/.config/Arcade Wheel/config.json`). It is written atomically. Deck and action IDs stay the same when you reorder them, and unknown action types are kept rather than dropped. If the file can't be parsed, it is backed up next to the original before defaults are loaded.

Commands run as a program plus arguments, not through a shell. Use `sh -c '…'` explicitly if you need shell syntax.

Arcade Box integration is optional and goes through a small CLI. Arcade Wheel calls `arcade-box tools --json` to list tools and `arcade-box run --tool ID [--input INPUT] [--preset ID]` to run one. See the [bridge contract](docs/ARCADE_BOX_BRIDGE.md).

## Platform notes

- **Wayland:** global press and release events come from the XDG GlobalShortcuts portal. LayerShellQt provides the overlay layer.
- **Hyprland:** the pointer position is read over compositor IPC (generic Wayland clients can't query it), and the pointer is moved to the wheel center when it opens. Other Wayland desktops can only provide a best-effort position and may not move the pointer.
- **Windows:** a narrowly scoped, passive keyboard hook watches only the configured trigger. It doesn't record keys or block normal input. Caps Lock tap passthrough and mouse-button triggers are not supported.
- Fullscreen games or apps with exclusive input may block the overlay or the shortcut, depending on the compositor or OS.

## Project layout

```
src/core/        WheelController (interaction state machine), WheelLogic (selection math)
src/config/      ConfigStore: defaults, migration, validation, atomic save, import/export
src/actions/     ActionDispatcher: built-in action routing
src/providers/   ActionProvider interface and the Arcade Box provider
src/platform/    Linux (portal, Hyprland IPC, LayerShellQt) and Windows backends
qml/             Wheel overlay, settings UI, shared components
tests/           Qt Test suites (run with ctest)
docs/            Architecture, manual release checks, Arcade Box bridge
```

See [architecture](docs/ARCHITECTURE.md) and [manual release checks](docs/MANUAL_TESTING.md) for more detail.

## Contributing

Issues and pull requests are welcome. Please:

1. Keep changes focused. Arcade Wheel is deliberately small.
2. Put platform-specific code behind `PlatformBackend`, and keep `WheelController` and QML free of platform code.
3. Add or update tests in `tests/` and make sure `ctest` passes. CI builds and tests on Linux and Windows.
4. For interaction changes, go through the relevant steps in [manual release checks](docs/MANUAL_TESTING.md) on a real desktop.

## License

MIT. See [LICENSE](LICENSE).
