# Arcade Wheel

Arcade Wheel is a radial desktop launcher. Hold a key, move toward an action and release to run it. Organize actions into decks and use center gestures to launch routines.

## Install

Install or update Wheel from **Arcade Tools → Apps**, or install it by hand using the [getting-started guide](docs/user/getting-started.md). Tools can share Wheel's tray controls through the Arcade tray icon; Wheel keeps its own tray icon when Tools is closed.

## Works with other Arcade apps

Add compatible Box, Lens, Look, Clipboard, Shelf and Find actions to Wheel slots, and use Lens, Look or clipboard content as supported action input. See [how the Arcade apps connect](docs/user/with-other-apps.md).

Read the [Arcade Wheel user guide](docs/user/) for setup, features, shortcuts and help.

## Development

### Requirements

- CMake 3.25+ and a C++20 compiler
- Qt **6.8+**: Core, Gui, Qml, Quick, QuickControls2, Widgets, Network, Test
- **Linux:** Qt DBus, GLib/GIO (`gio-unix-2.0`), pkg-config, and KDE's [LayerShellQt 6.4+](https://github.com/KDE/layer-shell-qt) for the Wayland overlay. Global shortcuts need a compositor portal that implements `org.freedesktop.portal.GlobalShortcuts` (Hyprland's does).
- **Windows:** MSVC with a Qt 6 MSVC kit
- **macOS:** Xcode command-line tools and a Qt 6 macOS kit

On Arch-based systems:

```sh
sudo pacman -S cmake ninja qt6-base qt6-declarative layer-shell-qt xdg-desktop-portal-hyprland
```

### Build and install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

On Linux the build stops if LayerShellQt is missing. For a development-only build without it, pass `-DARCADE_REQUIRE_LAYERSHELLQT=OFF`; the wheel then opens as a normal window and may not stay above other windows.

On Windows, configure with an MSVC Qt 6 kit, build with CMake, then run `packaging/windows/package.ps1` from an x64 Visual Studio developer prompt to create the setup EXE and portable ZIP. Packaging also needs Inno Setup 6 and Python. CI builds and tests all platform packages for each push to `main` and each pull request targeting `main`.

From a source checkout, `./arcade-wheel` runs the binary in `build/`.

### Command line

```sh
arcade-wheel --settings     # start in the background and open Settings (the default)
arcade-wheel --background   # start in the background with no window
arcade-wheel --show         # open the wheel in click mode
arcade-wheel --preview      # display the overlay without running actions
arcade-wheel --cancel       # close the wheel
arcade-wheel --status       # print status as JSON
arcade-wheel --restart      # restart the running instance
arcade-wheel --quit         # quit the running instance
```

`--press` and `--release` simulate the trigger; `--release` can run the selected action. Closing Settings leaves Wheel running. A tray icon opens Settings on click and offers Open Wheel, Open Settings, Restart Arcade Wheel and Quit Arcade Wheel.

### Configuration

The configuration is a versioned JSON file. Its path is shown on the **General** settings page (on Linux, `~/.config/Arcade Wheel/config.json`). It is written atomically. Deck and action IDs stay the same when you reorder them, and unknown action types are kept rather than dropped. If the file cannot be parsed, it is backed up next to the original before defaults are loaded.

Commands run as a program plus arguments, not through a shell. Use `sh -c '…'` explicitly if you need shell syntax.

### Platform notes

- **Wayland:** global press and release events come from the XDG GlobalShortcuts portal. LayerShellQt provides the overlay layer.
- **Hyprland:** the pointer position is read over compositor IPC (generic Wayland clients cannot query it), and the pointer is moved to the wheel center when it opens. Other Wayland desktops can only provide a best-effort position and may not move the pointer.
- **Windows:** a narrowly scoped, passive keyboard hook watches only the configured trigger. It does not record keys or block normal input. Caps Lock tap passthrough and mouse-button triggers are not supported.
- **macOS:** a registered global keyboard shortcut supplies press/release events; Settings can record a replacement when F8 is reserved by the OS (some keyboards require Fn+F8). Application discovery, launch/focus, the overlay, and optional login startup are implemented. Screenshot and sleep actions are supported; other system/media/compositor actions and mouse/modifier-only triggers are not available yet.
- Fullscreen games or apps with exclusive input may block the overlay or the shortcut, depending on the compositor or OS.

### Project layout

```
src/core/        WheelController (interaction state machine), WheelLogic (selection math)
src/config/      ConfigStore: defaults, migration, validation, atomic save, import/export
src/actions/     ActionDispatcher: built-in action routing
src/providers/   ActionProvider interface and asynchronous Arcade Link discovery/jobs
src/platform/    Linux, Windows, and macOS desktop integration backends
qml/             Wheel overlay, settings UI, shared components
tests/           Qt Test suites (run with ctest)
docs/            Architecture, manual release checks, Arcade Link actions
```

See [architecture](docs/ARCHITECTURE.md), [manual release checks](docs/MANUAL_TESTING.md) and [current status](docs/STATUS.md) for more detail.

### Contributing

Issues and pull requests are welcome. Please:

1. Keep changes focused. Arcade Wheel is deliberately small.
2. Put platform-specific code behind `PlatformBackend`, and keep `WheelController` and QML free of platform code.
3. Add or update tests in `tests/` and make sure `ctest` passes. CI builds, tests, packages, and smoke-tests on Linux, Windows, and both macOS architectures.
4. For interaction changes, go through the relevant steps in [manual release checks](docs/MANUAL_TESTING.md) on a real desktop.

### License

MIT. See [LICENSE](LICENSE).
