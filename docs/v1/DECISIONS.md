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
