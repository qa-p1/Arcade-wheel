# Arcade Wheel: status

## 2026-11-01: 0.5.0 (WHEEL.md overhaul)

- Kit: Arcade Link v0.4.15's QML kit as a static module (`arcade_kit`), Inter, Wheel's amber.
  `WheelController` is a typed QML type; the `qmllint_gate` target runs at 0 warnings. The overlay
  keeps its geometry, motion and gestures; only colours, type, the label pill and the selected
  slot's 2 px accent changed.
- Settings is Template A with the ten standard pages. `tst_SettingsLayout` opens every page and the
  action picker from 720×520 to 1920×1080, in light and dark, and fails when anything is clipped. The action picker
  keeps its System, Media and Desktop categories with 7, 7 and 14 actions.
- STANDARD §10 identity: `dev.arcade.Wheel` for the desktop file, Wayland app id, X11 class and
  macOS bundle id. The folder move was checked with a real 0.4-style profile on Xvfb: settings,
  the deck and the F9 trigger came across, the old folders were removed, and `--toggle`, `--hide`
  and `--quit` worked. A failed move is unit-tested (settings start from a copy of the old file).
- Performance against a 0.4.5 build from `main`, same machine and Qt, Xvfb, three runs each:
  the wheel appears in 69 ms in both; idle CPU is 0 in both. Memory (PSS) is 114 MB idle against
  110, 181 MB with the wheel shown against 173, 246 MB with Settings against 236. Most of it is the
  bundled Inter (the standard's font; 0.4.5 used whatever font the desktop had) and the kit's
  theme bindings. No timers or polling were added.
- Screenshots in `docs/user/images` were taken on Xvfb with xcompmgr and an isolated profile; the
  sidebar's "Shortcut needs attention" is real there, because Xvfb has no shortcut portal.
- Open, for the owner:
  - Remote branches are never deleted from these sessions. Already merged into `main` and safe to
    delete: `arcade/docs-polish`, `arcade/icons-layout`, `arcade/link`, `arcade/readme-standard`,
    `arcade/tools-hub`, `claude/epic-ramanujan-afo4yy`, `claude/hotfix-0.4.5-no-vulkan`,
    `claude/trigger-release-and-idle-memory`, `feature/shelf-action-onboarding`. Not merged:
    `arcade/wheel-win-layout-diag`, two scratch commits that only change `ci.yml` (no logs worth
    keeping); WHEEL.md asks for it to be deleted.
  - macOS: the bundle id changed from `com.arcadewheel.ArcadeWheel`. A login item registered by
    0.4 may stay listed in System Settings, and Settings shows start at login as off until it is
    turned on again. Not checked on a real Mac.
  - Tools 0.4.2 writes Wheel's login entry under the old name; Tools needs `dev.arcade.Wheel` for
    Wheel 0.5 and later (Tools 0.4.3).
  - Not checked on a real desktop: Windows and macOS (CI runners only), the new macOS volume and
    power actions (System Events asks for permission once), and the new Hyprland desktop actions.

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
| [architecture](dev/architecture.md) | Core classes, config schema, trigger path, gestures, coordinates |
| [link](dev/link.md) | Peer actions, `wheel.*` actions, Settings, verification, platform table |
| [packaging](dev/packaging.md) | Packages, release publishing, vendored files |
| [testing](dev/testing.md) | `scripts/check` and the release checks to run on a real desktop |
| [decisions](dev/decisions.md) | Choices that aren't obvious from the code |
| [user guide](user/index.md) | Install, use and troubleshoot Wheel |
| [CHANGELOG](../CHANGELOG.md) | Changes by version |
