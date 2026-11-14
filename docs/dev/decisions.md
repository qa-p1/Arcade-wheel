# Decisions

Short records of choices that are not obvious from the code. Newest first.

## 2026-11: overhaul to the Arcade standard (0.5.0)

- **Release flow:** `ci.yml` used to publish a stable `v<version>+build.<run>` release on
  every push to `main`. It now only checks and packages. Releases use the shared manual
  flow: `release.yml` with `beta` on `main` (a private draft prerelease), then `tag.yml`,
  then `release.yml` with `stable` on `v<version>`. See [packaging.md](packaging.md).
  Since 2026-11-14 (fleet step 6, rule zero): stable is never built. `tag.yml` makes only
  `v<version>-rc.<n>`, `release.yml` publishes it on Link's hidden `test` channel, and
  `promote.yml` republishes a verified rc's bytes as stable with a signed manifest.
- **Build number:** WHEEL.md asks for it in `arcade-release.json`, but the generator,
  `scripts/arcade-release.py`, is vendored byte-for-byte from Link and its manifest has no
  build field. The run number goes in the release notes instead; the manifest stays what
  Tools reads today. A beta draft's manifest says `nightly`, the only other channel the
  generator knows; it is never public.
- **Identifiers:** the OS id is `dev.arcade.Wheel` (desktop file, Wayland app id,
  `StartupWMClass`, macOS bundle id, login entry). Windows sets no AppUserModelID, like the
  other Arcade apps: the shared installer's shortcut carries none, and a process id that
  doesn't match its shortcut splits the taskbar button. The Link id stays
  `arcade.wheel`, so Tools, peers and saved slots keep working.
- **Folders:** settings, cache and logs move from `Arcade Wheel/Arcade Wheel` to Link's
  `arcade/wheel` folders on the first start of 0.5, before anything reads them, through
  Link's copy-verify-remove migration. `config.json` becomes `settings.json`. Anything that
  could not move is kept where it was and noted in the log. If `settings.json` is still missing,
  Wheel starts from a copy of the old `config.json` and leaves the original in place, so a
  failed move never loses settings.
- **Launcher rename:** Link's install engine writes the new launcher but leaves an entry
  recorded under an older id. Wheel's own `migrateIntegration` reinstalls in place once,
  keeps start at login as it was and removes the old `com.arcadewheel.ArcadeWheel` and
  `arcade-wheel` entries.
- **macOS login item:** start at login uses `SMAppService.mainAppService`, which is tied
  to the bundle id. After the change from the old id, a login item registered by 0.4 may
  still be listed in System Settings and Wheel reports start at login as off until it is
  turned on again. This could not be checked on a Mac before release; see STATUS.
- **Overlay:** kept as it was apart from kit tokens, spacing and icons (about 95%), per
  the owner's request. The Settings window and install prompt are rebuilt on the kit.
- **Action picker:** the System, Media and Desktop categories stay as they are; each got
  more actions (hibernate, restart, stop, volume, and Hyprland window and workspace
  actions). Actions a system can't run report why instead of being hidden.
- **Install prompt:** QML on the kit first; the Widgets dialog stays as a fallback when
  QML can't load, so a broken Qt Quick setup can still install or uninstall.
- **Branches:** STANDARD §10 and WHEEL.md ask for merged remote branches and
  `arcade/wheel-win-layout-diag` to be deleted. The owner asked that no remote branch be
  deleted during the overhaul, so they are listed in STATUS for them to remove.
