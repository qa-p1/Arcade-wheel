# Arcade Wheel: status

## Release status (2026-10-18)

- Latest release: [0.4.5](https://github.com/qa-p1/Arcade-wheel/releases/latest) (tag `v0.4.5+build.<CI run>`), published 2026-10-25 (rendering as in 0.4.3: 0.4.4's Vulkan renderer crashed on start with the bundled Qt 6.8, and its idle graphics release could leave the wheel transparent; see CHANGELOG.md).
- Previously released: [0.4.4](https://github.com/qa-p1/Arcade-wheel/releases/tag/v0.4.4%2Bbuild.70) (tag `v0.4.4+build.70`), published 2026-10-25 (the Hyprland trigger works the first time after a click in the wheel; crashed on Wayland with Vulkan and could reopen transparent; replaced by 0.4.5). The CI and e2e notes below were recorded for the earlier v0.3.0+build.44.
- Previously released: [0.4.3](https://github.com/qa-p1/Arcade-wheel/releases/tag/v0.4.3%2Bbuild.67) (tag `v0.4.3+build.67`), published 2026-10-19 (only programs from another image drop the AppImage's descriptors).
- Previously released: [0.4.2](https://github.com/qa-p1/Arcade-wheel/releases/tag/v0.4.2%2Bbuild.65) (tag `v0.4.2+build.65`), published 2026-10-19 (released every inherited descriptor at start, which also cut off helpers from the app's own image).
- Previously released: [0.4.1](https://github.com/qa-p1/Arcade-wheel/releases/tag/v0.4.1%2Bbuild.63) (tag `v0.4.1+build.63`), published 2026-10-18 (fixes for what verifying the release before it found).
- Previously released: [0.4.0](https://github.com/qa-p1/Arcade-wheel/releases/tag/v0.4.0%2Bbuild.61) (tag `v0.4.0+build.61`), published 2026-10-18 (fixes from the October audit and stress test).
- Previously released: [v0.3.1+build.56](https://github.com/qa-p1/Arcade-wheel/releases/tag/v0.3.1%2Bbuild.56), published 2026-10-16.
- Previously released: [v0.3.0+build.44](https://github.com/qa-p1/Arcade-wheel/releases/tag/v0.3.0%2Bbuild.44) with a Linux self-installing AppImage, a per-user Windows installer, a macOS dmg, `arcade-release.json` and `SHA256SUMS.txt`.
- Merged to `main` from PR #6 (deaf79c) after CI passed on Linux, Windows and macOS: [run 38599266119](https://github.com/qa-p1/Arcade-wheel/actions/runs/38599266119). That covers the build, tests, clippy or analyzers, installer smoke tests on each OS, and the Linux AppImage install, repair and uninstall check in a temporary profile.
- Ecosystem e2e (Arcade Link `tools/e2e.py`, real CI builds of all eight apps): install, tray hosting (hosted by Tools, own icon back within 1 s after Tools is killed, no flash on restart) and shortcuts pass. Tools 0.2.0 backs up and restores Wheel's settings (the backup incompatibility found by the e2e matrix was fixed in Tools, cf8f0c5).
- Not yet verified: a real Windows or macOS desktop (CI runners only), and the owner's real-desktop checks. Builds are unsigned, so SmartScreen and Gatekeeper ask for approval.

Updated 2026-10-13 (Wheel 0.3.0, Arcade Link v0.3.0, protocol 1). This records implementation and verification separately.

## Implemented

- Hold-move-release wheel with decks, center gestures (single, double,
  triple click and long press), live Settings, JSON config (schema 4) with
  migration, import and export, and the standard tray menu.
- Triggers: XDG GlobalShortcuts portal with LayerShellQt overlay on Wayland
  (pointer moved over Hyprland IPC), a passive hook on Windows, a Carbon hot
  key on macOS 13+.
- Arcade Link: peer actions from Box, Lens, Look and Clipboard in the action
  picker (tools, presets, saved Box pipelines, with clipboard, Lens selection
  or file-manager selection as input), `wheel.show` and `wheel.add_action`
  (always confirmed in Settings), the Connected apps page.
- Portable JSON settings import/export with secret and device-identity fields
  omitted, a private backup before import, and a CLI `--menu` listing for the
  Wheel-specific tray action. The manifest carries the configured effective
  trigger in canonical form under the `trigger` shortcut id.
- Windows per-user Inno setup using the shared Link integration include for
  the receipt, PATH shim and optional login shortcut. Linux AppImages use the
  shared first-run self-installer and macOS bundles offer move-to-Applications.

- All seven hub methods with feature detection, restart busy/force policy,
  explicit shortcut registration results and live configuration updates.
- Tools tray hosting with startup gating, exclusions, Link off, immediate
  socket-close recovery, and a five-second restart grace; tray status and
  menu/docs/log/install manifest metadata.
- Install/update/open dialog with shared strings, exact-file dismissal,
  background/dev suppression, Tools handoff, lifecycle CLI and PATH hints.

## Verification

| Check | Result |
|---|---|
| Qt build and complete CTest suite | Linux 16/16 (offscreen), Windows 15/15, macOS arm64 and x86_64 15/15 each, [CI run 38433936789](https://github.com/qa-p1/Arcade-wheel/actions/runs/38433936789) at `36f2e13` |
| Python release and shortcut tests | 16/16 pass (Linux CI and local) |
| Link vendor, release assets, shortcuts and docs checks | pass in the same run |
| Isolated hub lifecycle through Link tools/e2e.py (Linux CI) | pass: self-install from a simulated AppImage, update with `previous`, repair after a move, uninstall keeping data, settings export/import, `app.restart`, tray hosted/own including the 1 s hand-back and restart grace |
| Windows per-user Inno setup (CI only) | pass: silent install, manifest, docs, settings, login toggle, repair, successor installer, old uninstaller exit 10, uninstall removes receipt, shim, login shortcut, Link manifest and PATH entry |
| macOS DMG, both architectures (CI only) | pass: install, manifest, docs, settings, moved-bundle repair and uninstall |
| Linux AppImage packaging and smoke (CI only) | pass |

This branch has not been installed into the owner's real profile. Native
Windows/macOS behavior is exercised by CI, not a real desktop. Rust interop
checks require `ARCADE_LINK_CLI`; without it those cases report skips.

## Limits

- Windows and macOS are built, tested and packaged in CI, but have not been
  run interactively.
- macOS: mouse, media and modifier-only triggers and most system actions are
  not available.
- Wayland needs a GlobalShortcuts portal; pointer positioning is exact only on
  Hyprland.
- Builds are unsigned (Windows) or ad-hoc signed without notarization (macOS).
- Lens and file-manager selection inputs are covered by mock peers in the
  isolated runner; file-manager selection is hidden on Linux, where no file
  manager can report its selection.

## Documents

| Document | Contents |
|---|---|
| [README](../README.md) | Install, features, usage, CLI, configuration, platform notes |
| [ARCHITECTURE](ARCHITECTURE.md) | Core classes, config schema, trigger path, gestures, coordinates |
| [ARCADE_LINK](ARCADE_LINK.md) | Peer actions, `wheel.*` actions, Settings, verification, platform table |
| [PACKAGING](PACKAGING.md) | Packages, release publishing, vendored files |
| [MANUAL_TESTING](MANUAL_TESTING.md) | Release checks to run on a real desktop |
| [CHANGELOG](../CHANGELOG.md) | Changes by version |
