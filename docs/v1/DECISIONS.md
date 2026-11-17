# Decisions

Newest last. Each entry: what was decided, why.

## D1. Scope of the 1.0 code pass
The brief (`/docs/v1/SPEC.md`, `audit/`) is the contract. Wheel keeps its own C++ implementations of install and
desktop integration, child launch, single instance and tray hosting (owner decision, catalog README). They must pass
the same rules and Link's contract tests, so Link's `runtime_contract.py` and its Qt test executables run in CI.

## D2. Link is vendored at the tag v1.0.6
`src/link` (Qt module, design kit, vectors, install strings), `scripts/arcade-release.py`,
`scripts/validate_shortcuts.py`, the Windows packaging includes, the app icons and the Arcade glyphs all come from
`v1.0.6` (commit `cd380d62`). `scripts/sync-design v1.0.6` does the same from the network; the first bump was done from a
local checkout with `tools/vendor_qt.py --revision v1.0.6`, which reads `git show`, so it is the same bytes.
Why: LINK-2 (pin by tag). The bump carries WH-5 (Escape cancels a destructive dialog), the same-user socket check and
`arcade_trim.h` (RT-5).

## D-W0. Trim after startup and after each hide; the renderer stays as it is for now (WH1)
`arcade_trim()` runs once the event loop starts and 2 s after the wheel hides (a reopen cancels it). Refaults are minor
page-cache faults; the audit measured no UX change. The software renderer (WH1, about -72 RSS / -24 PSS) changes how
the wheel draws; the spec requires equal open animation and frame time, measured. CI renders with llvmpipe either
way, so it cannot show the GPU comparison. Left for a measured run on a real session (owner's desktop harness), with
"release the scene graph on hide" as the fallback if the software renderer is slower.

## D3. Link moves to the tag v1.0.7
`scripts/sync-design v1.0.7` re-vendored `src/link` (commit `40ec0279`); the shared tools and Windows includes were
copied from the same tag and their `VENDORED` records and CI's `ARCADE_LINK_REVISION` updated. 1.0.7's Qt installer
retires entries under `legacyDesktopIds` even when they run the same AppImage, and carries Start at login over; Wheel
already lists `com.arcadewheel.ArcadeWheel` and `arcade-wheel` (fleet rule "One launcher per app").

## D4. Untangle (step 2): configuration no longer depends on the platform interface

`ConfigStore.h` included `platform/PlatformBackend.h` (a `QObject` interface with every OS hook) for one value type,
`DiscoveredApplication`, so the stored configuration format depended on the most volatile layer. The struct is now
`core/DiscoveredApplication.h`; the backends, which produce it, and `ConfigStore` and `ActionDispatcher`, which read
it, include that. Kept on purpose: the Linux and Windows backends use `AppInstaller` for start at login and desktop
repair once Wheel is installed, because the login item is part of the install integration.

## D5. Dead code (step 4)

Rule: remove with the language's own tools, keep only what is provably used, including by CI, packaging, Link entry
points and other apps; vendored files are never edited.

Evidence gathered: clang 18 with `-Wunused -Wunused-function -Wunused-member-function -Wunused-private-field
-Wunused-macros -Wunused-template` over every Wheel and test translation unit that builds on Linux (clang 18 cannot
parse `QPair` class-template deduction in `ArcadeLinkProvider.cpp`; g++ 13 with `-Wunused -Wunused-function
-Wunused-macros -Wunused-but-set-variable -Wunused-local-typedefs -Wunused-const-variable=2` covered that file), no
warning in Wheel's code; clang-tidy `misc-unused-using-decls`, `misc-unused-alias-decls`,
`readability-redundant-declaration`, no finding. The compilers do not see members of public classes, so every
function, slot, signal, `Q_PROPERTY`, `Q_INVOKABLE`, struct field and constant declared in the non-vendored headers was
searched by name across `src/`, `qml/`, `tests/`, `scripts/`, `packaging/`, `.github/` and `docs/`, and each file under
`qml/`, `assets/`, `scripts/`, `packaging/` and `tests/` was checked for a reference.

Removed (one commit each):

- `WheelController::refreshProviders()`: a `Q_INVOKABLE` that no QML, C++, test, script or CI step names.
- `WheelLogic::centreOf()`: only its own test called it; the controller centres from the overlay window's size. The
  test's centre column and assertion went with it, the `screenIndexFor` rows are unchanged.
- `AppMetadata::Shelf` and `AppMetadata::Find`: namespace constants nothing refers to.
- `WheelLink::lastError()` and `m_lastError`: no caller; the same value already goes to `diagnosticsChanged`.
- `LinuxBackend::DesktopEntry::terminal`: parsed from every `.desktop` file, never read (GIO handles `Terminal=`).
- `DiscoveredApplication::launchTarget`: set by the Linux and Windows backends, never read.
- `assets/arcade/tokens.json`: compiled into the executable as a resource, opened by nothing, not vendored, not
  refreshed by `scripts/sync-design`; the kit's `Theme.qml` carries the accents.
- `packaging/linux/dev.arcade.Wheel.png` and `packaging/linux/arcade-wheel-symbolic.svg`: no CMake, packaging
  script or CI step names them; the AppImage takes its icon from `assets/arcade-wheel.svg` (`packaging/linux/package.sh`).

Kept although it looks unused:

- `WheelController::shortcutPeerConflict` (a `Q_PROPERTY`) is read only by `tst_WheelController`, and
  `WheelLink::locations()` only by `tst_WheelLink`. Both expose state the app uses (the controller sets
  `m_shortcutPeerConflict` when a peer owns the shortcut; `WheelLink` serves from `m_locations`), so they stay as
  the tests' view of it.
- `shortcutFromKey` and `arcadeActionInfo` are `Q_INVOKABLE` but only called from C++ inside the controller; they are
  used, the attribute is just wider than needed.
- Windows, macOS and `ARCADE_HAVE_LAYERSHELLQT` code: not compiled on this Linux build, so only read by hand (every
  helper has a second reference); nothing was removed there except the `launchTarget` assignment.
- Everything under `src/link/qt`, `src/link/design`, `src/link/spec`, `src/link/fixtures`, `src/link/tools`, the shared
  scripts and the Windows includes is vendored (D2, D3). Reported, not edited: a `retired` variable set and never read
  (`-Wunused-but-set-variable`, `ArcadeInstall.cpp:1012`), and the kit components `HintBar`, `NumberField`, `OverlayRow`
  and `Skeleton`, which neither Wheel nor other kit files use.
- `docs/dev/history/release-notes-template.md` is unreferenced but is history, kept as such. `tests/cli-contract.py` and
  `tests/verify-wheel.py` are not run by CI (only `PROGRESS.md` and `docs/dev/link.md` mention them) and stay as
  documented manual checks.

## D6. Step 5: memory, unpacked runtime (U1)

Done: Link moves to v1.0.10 (the same D3 steps, see below), and Wheel runs from the unpacked copy of its AppImage
(U1, `AUDIT/memory.md`). `main()` calls `arcade_relaunch_unpacked("arcade.wheel", ARCADE_VERSION, 0, argv)` from
Link's `qt/arcade_unpacked.h` first, before `ArcadeLink::onStartup()` and the application object, and exits with the
code it returns when that is not -1. The launcher then exits 0 at once and the same command line runs from
`<XDG_DATA_HOME>/arcade/runtime/wheel-<version>-<16 hex digits>/`, with `$APPIMAGE` unchanged, so Install, the login
entry and Restart still name the installed file. The copy is made once per version and image.

Which starts relaunch (`ResidentStart::keepsResident`, unit-tested): no arguments, `--settings`, `--background` and
`--restarting`, the starts that become Wheel's resident. Not relaunched, as Box does for its one-shots:
- Commands that never keep a process: `--status`, `--version`, `--help`, `--menu`, `--arcade-manifest`,
  `--settings-export`, `--install`, `--uninstall`, `--repair`, `--integration-status`, and anything unknown (exit 2).
  A relaunch without wait would also drop their exit codes (`--status` exits 3 when Wheel is not running).
- `--smoke-test`: one-shot whose exit code is the result (`packaging/smoke-test.py`).
- Commands that drive a running Wheel: `--show`, `--preview`, `--hide`, `--cancel`, `--toggle`, `--press`, `--release`,
  `--restart`, `--quit`, `--settings-import`. The code cannot tell before the application object exists whether a
  resident is running; with one they only send a line and exit, and the trigger commands run on every hotkey press,
  where a relaunch is a second process for nothing. With none running they still start one, from the mount as before
  (no regression); `--background` at login and Settings are how Wheel normally starts. `--preview` is such a command
  (it asks the running Wheel to draw a preview), not a start of its own.

Restart: with Link 1.0.10 the copy had `ARCADE_UNPACKED=1`, the successor inherited it and started from the mount
(checked: the restarted resident ran from the AppImage's mount). The same happened in Clipboard, so it was fixed once
in Link 1.0.11: the copy's marker names its own folder, and `arcade_relaunch_unpacked` drops a marker that does not
contain the running executable, so the successor started from `$APPIMAGE` unpacks again. Wheel's own workaround
(`ResidentStart::runsFromUnpackedCopy`) was removed; the packaged lifecycle check still asserts the restarted resident
runs from the copy. `ARCADE_UNPACKED=1` (Link's isolated session) still means "never relaunch".

Uninstall: nothing added. On Linux `AppInstaller::command --uninstall` ends in `engine.uninstall(Ids::Wheel, ...)`,
Link's `ArcadeInstall::uninstall`, which since 1.0.10 also removes `<data>/arcade/runtime/wheel-*` (finished copies and
`.wheel-*` copies in progress) and leaves other apps' copies. `quitResident` runs before it, so the copy in use has
exited. `tst_AppInstaller` and the packaged lifecycle check assert it (Wheel's copy goes, another app's stays).

Link 1.0.8 to 1.0.10, what touches Wheel: 1.0.10 adds `arcade_unpacked.h` and the uninstall above (this step);
1.0.9 makes Link's isolated session start AppImages with `ARCADE_UNPACKED=1` (the packaged check removes it from
its own environment because it wants the real relaunch), the rest is app gates and the Dart kit; 1.0.8's key chip and
`HintBar` changes are in the kit, and Wheel uses neither component (D5). The shared tools and Windows includes did not
change between 1.0.7 and 1.0.10, so only their `VENDORED` records and `ARCADE_LINK_REVISION` moved. The tag `v1.0.10`
did not exist yet, so `tools/vendor_qt.py` and the icon and glyph copies of `scripts/sync-design` ran against a
local clone at the release commit `2748ebf4` with a scratch tag; the files are what the tag will give.

Cost: the copy is about 145 MB on disk per version (the audit measured 144 MB). Wheel does not remove an older
version's copy itself; uninstall removes all of them, and Tools' `runtime_gc` recognises the folder names.

Not done, and why:
- WH1 (software renderer) stays as D-W0 says: it needs a measured run on a real session.
- W1, W3b and OPEN1 are WebKit items (a WebView's renderer and network process, the page-out of its heap, and the
  trimmer in WebKit's helpers); Wheel has no WebKit.
- W0 was already done (D-W0).
- Not measured here: the memory effect of U1 for Wheel. The audit's numbers come from its lab; the change is checked
  for behaviour (the resident runs from the copy, also after Restart), not for megabytes.

Found, in Link, not changed here: with `ARCADE_HOME` set, `ArcadeInstall::Environment::discover` takes the data
folder from under it and ignores `XDG_DATA_HOME`, while `arcade_unpacked.h` and `runtime::unpacked_root` follow
`XDG_DATA_HOME`. They agree without `ARCADE_HOME` (every real install), so uninstall finds the copies; only isolated
test sessions differ, and the packaged check points `XDG_DATA_HOME` where Link looks.

## D7. 1.0.0: Link v1.0.13, conformance, desktop suites, release gates (steps 6 to 8)

Link moves to v1.0.13 (`a24146f`) with `scripts/sync-design v1.0.13`, the same D3 steps: `scripts/VENDORED`,
`packaging/windows/VENDORED`, `ARCADE_LINK_REVISION` in `ci.yml` and `docs/dev/vendored.md` name it, the reusable
workflows (nightly, promote, release) are `@v1.0.13`, and the vendor checks pass (114 bundled files, 5 assets). What
v1.0.11 to v1.0.13 change for Wheel: the inherited `ARCADE_UNPACKED` fix (D6), and the unpacked copy no longer
inheriting the launcher's mount environment (Look's Restart hang; Wheel's copy runs the same `AppRun`).

Conformance (`arcade-link conformance .`, Link 1.0.13 CLI): 24 checks, 0 failed after two fixes:
- **ID-3:** `packaging/linux/dev.arcade.Wheel-symbolic.svg` (Link's `arcade.wheel.svg` glyph, unchanged), installed to
  `share/icons/hicolor/symbolic/apps`; `scripts/sync-design` copies both desktop-id icons.
- **REPO-2:** a `dependency review` job on pull requests (the audit half; Dependabot was already in place).

Desktop suites in CI (`desktop`, `desktop-tier2`), on the CI AppImage, and in `gates.yml` on the published one:
- Tier 1 (`tests/desktop/wheel_desktop.py`, headless Hyprland): a plain start opens Settings with Wheel's desktop id;
  `--status`; `--show` maps the wheel as the `arcade-wheel` layer surface on the overlay layer, covering its monitor, and
  adds no window; `--hide`, `--toggle` twice; Ctrl+W closes Settings and Wheel stays running; `--quit` leaves nothing.
- Tier 2 (Link's `smoke.py` on sway, KWin, Mutter, X11) with the default `--background` start.

Release gates (`gates.yml`, Link `app-gates.yml@v1.0.13`, previous release v0.5.0):
- Linux: `tests/appimage-lifecycle.py` on the published AppImage in Link's isolated session, and
  `scripts/verify-upgrade.py` from 0.5.0. 0.5.0 was built on Ubuntu 22.04, so it starts in the gates' 22.04 Linux job
  (Shelf and Box need a separate 24.04 job for theirs).
- Windows: `packaging/windows/gate-installer.ps1`: silent per-user install of the published Setup.exe, the installed
  app's smoke test and native install checks, uninstall, nothing left.
- macOS: `packaging/macos/gate-dmg.sh` on the Apple Silicon DMG (the hosted runner's architecture): copied out of the
  image as a user drags it, smoke test, native install checks.

The upgrade test fills 0.5.0's profile through `--settings-import` on the running 0.5.0 (3 decks, 13 slots, 12
non-default settings), so nothing is written in ConfigStore's shape by hand, then installs the new file over it with
`--install --silent` while 0.5.0 runs. It checks the settings file is untouched, that the new copy replaced the old in
place, and that there is one launcher and at most one login entry, both starting the installed file.

Start at login across an update: a fresh install starts at login (as every Arcade app); an existing install keeps the
state of its entry (`loginEnabled` in `AppInstaller.cpp`). Checked by hand on 0.5.0 to the CI 1.0.0 without
`ARCADE_HOME`: install adds the entry, switching start at login off removes it, the update keeps it off. With
`ARCADE_HOME` set (isolated test sessions only) the installer's autostart folder is under it while the app reads
`XDG_CONFIG_HOME`, the same split as in D6, so that case cannot be checked there.
