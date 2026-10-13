# Arcade Wheel: status

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
