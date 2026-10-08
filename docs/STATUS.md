# Arcade Wheel: status

Verified 2026-10-08 on branch `arcade/link` (version 0.2.0, Qt Link module
vendored from Arcade Link `v0.1.0`). This page records what is implemented
and how it was checked; the other documents describe how it works.

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

## Verification

| Check | Result |
|---|---|
| `ctest` | 9 of 9 suites pass |
| `packaging/check-link-vendor.py --source …` | the 8 vendored files match `v0.1.0` |
| CI (Linux AppImage, Windows setup, macOS arm64 and x86_64 DMGs, each built, tested and smoke-tested) | passing at `3333434` |
| Arcade Link e2e, `wheel` group and cross-app flows | all passing (74/74 ecosystem checks) |
| Benchmark against the 2026-10-05 baseline | startup 146.9 → 136.6 ms, warm invoke 38.6 → 35.2 ms, idle RSS 114 → 115 MiB, idle CPU 0 |

Wheel runs daily on the owner's Hyprland desktop.

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
