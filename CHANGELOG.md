# Changelog

All notable changes to Arcade Wheel are documented here.

## Unreleased (branch `arcade/link`)

### Added

- **Arcade Link.** Wheel joins the other Arcade apps through the vendored Qt module (Arcade Link `v0.1.0`): it publishes a manifest and listens for `wheel.show` and `wheel.add_action`. An added action always opens a draft in Settings and needs a chosen slot and confirmation.
- **Arcade actions in the picker.** Box tools, presets and saved pipelines, and Lens, Look and Clipboard actions can be bound to slots, with the clipboard, a Lens selection or a file-manager selection as input. The picker shows each action's effects, with ↗ for actions that send content out. Missing or disabled peers leave saved slots in place with a reason.
- **Connected apps** settings: a master switch, per-app toggles, Get links (through Arcade Tools when installed) and diagnostics.
- **Release metadata.** Releases include `arcade-release.json` and one combined `SHA256SUMS.txt` for Arcade Tools.

### Fixed

- The tray icon now appears when the tray host starts after Wheel (common at login).
- Manifest updates after the first one failed on Windows (fixed in the vendored module).
- The Settings hint wraps beside long saved shortcuts.

## 0.2.0 — 2026-10-04

Builds of this version are published automatically from `main` as `v0.2.0+build.N`. See [Packaging and releases](docs/PACKAGING.md).

### Added

- **macOS support (13+, Apple Silicon and Intel).** A registered global keyboard shortcut supplies press and release events. Application discovery, launch/focus, the overlay across Spaces and fullscreen apps, screenshot and sleep actions, and start on login (from Applications) are implemented. Mouse, media, and modifier-only triggers and other system actions are not available on macOS yet.
- **Installers.** CI builds a Windows setup EXE and portable ZIP, a Linux AppImage, and macOS DMGs for both architectures, each with a SHA-256 checksum. Every package is smoke-tested before upload.
- **Automatic releases.** Every successful build on `main` publishes a stable GitHub Release. Explicit `vX.Y.Z` tags still publish that exact version. Interrupted uploads leave a draft that the next run finds and completes.
- **Single-click center gesture.** A single left click in the wheel's hub can now run its own group of up to 16 apps or actions, configured under **Settings → Center gestures** like double-click, triple-click, and long press. When double- or triple-click is also enabled, a single click waits for the click interval before running; releasing the trigger runs it immediately.

### Unchanged

- With no single-click group, a single center click still closes the wheel. Holding the trigger and releasing it in the center without clicking still just closes the wheel.
- Existing configurations get a disabled single-click group automatically; no migration is needed.

### Fixed

- **Windows:** the pointer now moves to the wheel center when the wheel opens, matching Hyprland. Before, the wheel was drawn at the screen center while the pointer stayed put, so the first small movement could select a slot and releasing would launch it instead of cancelling.
- **Windows:** Settings no longer shows "Shortcut needs attention" while the trigger works. The backend now reports a "Ready" status like Linux does.
- **Windows:** shortcuts recorded in Settings for Page Down, Insert, Print Screen, Num Lock, Scroll Lock, Menu, and the volume and media keys are accepted. Unsupported shortcuts are now rejected before they are saved, instead of being saved and leaving the trigger disabled.
- **Windows:** changing the trigger while it is held cancels the wheel instead of running the current selection.
- **Windows:** the keyboard trigger runs on its own thread. Windows silently removes a low-level keyboard hook whose thread is busy for too long (for example while refreshing the application list), which disabled the trigger until restart.
- **Linux:** "Media Stop" and "Toggle Media Play/Pause" can be used as triggers.
- **Linux:** turning start on login off and on no longer leaves a backup copy in `~/.config/autostart` each time, and a quick toggle can no longer fail with "Could not back up the Arcade Wheel autostart entry". Hand-edited entries are still backed up before being replaced.
- **Hyprland:** if the compositor event socket is lost, it reconnects, and the pointer still moves to the wheel center. Before, the pointer could stay where it was for the rest of the session.
- **Start on login** now matches the system. Settings shows whether the login entry actually exists, and importing a configuration or resetting to defaults turns the entry on or off to match.
- **Screenshot** actions wait for the wheel to leave the screen so it isn't captured.
- Older configuration files are upgraded on disk when loaded. Before, the upgrade was redone on every start, and entries without IDs got new IDs each time.
- **Restart** keeps the running instance if a new one can't be started, instead of quitting.
- Appearance sliders no longer extend past the values the wheel can display (icon size, segment depth, selected scale).
- **Linux AppImage:** start on login and **Restart** use the `.AppImage` file itself instead of its temporary mount, which disappears when the app exits.

### Changed

- Dragging a slider in Settings still updates the wheel immediately, but the configuration is written once the slider settles (about 250 ms) and on exit, rather than on every step.
- On Linux, the command socket used by `--status`, `--show`, and other commands moved from the shared `/tmp` directory to the per-user runtime directory, so another local user can't take its name. When upgrading, quit the old instance before starting the new one.
- `--help` lists every command.
- Removed unused code (`WheelLogic::clampCenter` and its tests, `LinuxBackend::updateOverlayScreen`); the wheel is always centered on the screen.
- The Linux trigger tests skip with an explanation, instead of failing, when the build directory is under `/tmp`.
- The command action help text says that `~` and environment variables aren't expanded.
- Linux builds require LayerShellQt 6.4 or later.
- The Windows package is named `ArcadeWheel-<version>-Windows-x64` and bundles the Visual C++ runtime, so the per-user installer runs without administrator rights.

### Documentation

- Rewrote the README for the open-source release: features, requirements (Qt 6.8+), build, usage, CLI reference, configuration, platform notes, layout, and contributing.
- Updated the architecture notes for schema version 3, center gesture recognition, and how the wheel is positioned.
- Added single-click checks to the manual release checklist.
- Added [packaging and release documentation](docs/PACKAGING.md) and download instructions to the README.
- Added macOS to the architecture notes and the manual release checklist.
