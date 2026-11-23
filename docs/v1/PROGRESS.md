# Wheel 1.0.0 progress

Newest first. "tested locally" gives the command; "not run" says why.

Qt 6.8 is not installed in the authoring sandbox (Qt 6.4.2 only, no network install allowed), so the full CMake build,
the QML and the QtTest suite could not run there. Code that does not need Qt Quick was built and run against Qt 6.4
in a throw-away harness (ConfigStore, WheelPaths and the Link path code); everything else is **not run** until CI.

## 2026-11-23 - 1.1.0 promoted (cloud session)

`v1.1.0-rc.1` (5e4f055, PR #29: Link 1.1.1, the shared action rules, any app's action) passed
[Gates](https://github.com/qa-p1/Arcade-wheel/actions/runs/45174864217) 12/12 on its published files: published files,
delta from v1.0.0, memory and performance budgets, Linux conformance, Windows and macOS installer smoke, N-1, and both
desktop suites (Tier 1 Hyprland; Tier 2 sway, KWin, Mutter, X11).
[Promote](https://github.com/qa-p1/Arcade-wheel/actions/runs/45176537134) made it
[1.1.0](https://github.com/qa-p1/Arcade-wheel/releases/tag/v1.1.0), now the latest release. The AppImage, both dmgs,
the setup.exe, the Windows zip and SHA256SUMS.txt have the same SHA-256 digests as rc.1; `manifest.json` and
`arcade-release.json` differ only in the channel (`test` to `stable`), the tag and the notes link, and
`manifest.json.sig` is new.

PR #29's CI found three things, each fixed before the merge:
- ARM Linux `scripts/check` hit the 45-minute limit: `cmake --build --parallel` with no number runs an unbounded
  `make -j` and ignores `CMAKE_BUILD_PARALLEL_LEVEL`, which swapped the 7 GB runner. `check.py` now passes the job count.
- One `hub-lifecycle.py` run timed out on a status wait after `app.restart` and could not be reproduced (13 local runs,
  restart takes 0.1-0.2 s). The timeout now prints the exit code, output and Wheel's logs.
- macOS: `hdiutil attach` can fail with "Resource temporarily unavailable" right after `hdiutil create`.
  `packaging/macos/attach.sh` tries again up to five times, for that error only.

## 2026-11-18 - 1.0.0 promoted (cloud session)

`v1.0.0-rc.4` (5481c63, Link 1.0.13, Tier 2 from Link 1.0.14) passed
[Gates](https://github.com/qa-p1/Arcade-wheel/actions/runs/44226061986) 12/12 on its published files: published files,
delta from v0.5.0, memory and performance budgets, Linux conformance with the lifecycle and upgrade checks, Windows and
macOS installer smoke, N-1, and both desktop suites (Tier 1 Hyprland; Tier 2 sway, KWin, Mutter, X11).
[Promote](https://github.com/qa-p1/Arcade-wheel/actions/runs/44228311950) made it
[1.0.0](https://github.com/qa-p1/Arcade-wheel/releases/tag/v1.0.0), now the latest release. The AppImage, both dmgs,
the setup.exe, the Windows zip and SHA256SUMS.txt have the same SHA-256 digests as rc.4; `manifest.json` and
`arcade-release.json` differ only in the channel (`test` to `stable`), the tag and the notes link, and
`manifest.json.sig` is new.

What stopped the earlier rcs, each fixed before the next:
- rc.1 (9ae74f4): Gates' Linux job lacked libraries the AppImage takes from the desktop (`libEGL.so.1`), so CLI-1 to
  CLI-4 exited 127. #25 installs the full list.
- rc.2 (a29a65c): its own Release failed on macOS arm64: `tst_WheelTrayHost` expected "hosted" first, but on a loaded
  runner the test's fake Tools answered after the 300 ms startup cap, so Wheel rightly showed its own icon first. #26
  checks the contract either way: in time, "hosted" with no own icon before it; late, "own" no earlier than the cap,
  then "hosted".
- rc.3 (60c58a1): its own Release failed in Tier 2 (sway, X11): the desktop packages timed out on a slow Ubuntu
  mirror. #27 moves Tier 2 to Link 1.0.14 (package cache, parallel prefetch, mirror fallback). Nothing was published.

## 2026-11-17 - steps 6 to 8: 1.0.0, conformance, desktop suites, gates (cloud session, PR #24)

- D7. Link v1.0.13, conformance 24/24 (ID-3 symbolic icon, REPO-2 dependency review), Tier 1 and Tier 2 desktop suites
  in CI and in `gates.yml`, gates on the published rc (Linux lifecycle and upgrade from 0.5.0, Windows installer,
  macOS DMG), version 1.0.0 and its changelog section.
- CI on `ade3b02`: every job green, including Tier 1 (Hyprland) and Tier 2 (sway, KWin, Mutter, X11).
- Tested locally: `dbus-run-session -- xvfb-run -a python3 scripts/verify-upgrade.py <published 0.5.0 AppImage>
  <CI 1.0.0 AppImage from ade3b02>`: "PASS: 0.5.0 -> 1.0.0 keeps the user's wheel and settings and the integration"
  (3 decks, 13 slots and 12 settings kept, one launcher, one login entry). Start at login switched off on 0.5.0 stays
  off after the update (D7).
- Not run: `gate-installer.ps1` and `gate-dmg.sh` need the hosted Windows and macOS runners and a published rc; they
  first run in the rc.1 gates.

## 2026-11-15 - step 5: memory, unpacked runtime (cloud session, branch `local/wheel-step5`)

- Link v1.0.10 (`2748ebf4`), U1 (`ResidentStart`, `main()`), the Restart successor fix, tests and D6 are in the
  commits on top of `origin/main`; `git log origin/main..HEAD` lists them. WH1 stays as D-W0 says; W1, W3b and OPEN1 are
  WebKit only.
- Tested locally with Qt 6.8.3 (aqt), Linux x86_64, no real desktop session:
  - `cmake -S . -B build-ls -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DWHEEL_WARNINGS_AS_ERRORS=ON
    -DARCADE_REQUIRE_LAYERSHELLQT=ON` (LayerShellQt 6.4.6 and ECM 6.14.0 built from KDE's git tags, as
    `packaging/linux/build-dependencies.sh` does from tarballs, which this sandbox cannot download, so their checksums
    were not checked), then `scripts/check.py --build build-ls --config Release --link-source <Link at 2748ebf4>`:
    build with warnings as errors, clang-format 18.1.3, qmllint gate, `check-link-vendor.py` and
    `check-vendored-tools.py` against Link, shortcut data, doc links, release tests and 23/23 ctest (offscreen, software
    renderer): pass. The same run on the build without LayerShellQt also passes.
  - New tests fail without the change: `ResidentStart` with `keepsResident` returning false fails its four
    resident rows; the packaged check with the relaunch disabled in `main()` times out waiting for the launcher to exit
    (it stays the resident); without the Restart fix the successor's `/proc/<pid>/exe` is the AppImage mount, not the copy.
  - `scripts/package` (`packaging/linux/package.sh build-ls dist`): AppImage built, bundle check and `--smoke-test` pass.
    `tests/appimage-lifecycle.py` on that AppImage inside Link's `tools/e2e.py run --` (Link CLI built from the same
    commit): passes, covering the unpacked resident, Restart, `--quit` and uninstall. The SVG engine files are in the image.
  - `tests/hub-lifecycle.py` on `build/arcade-wheel` in `e2e.py run --`, and `tests/cli-contract.py`: pass (they run the
    binary directly, not an AppImage, so they see no relaunch).
- Not run: Windows and macOS (compile only in CI; the header is a no-op there and `main()` calls it on every
  platform); `tests/verify-wheel.py` (needs the other apps' builds under fixed paths); the WH1 comparison and any
  memory measurement (RSS and PSS were not measured, only where the resident runs from); the real AppImage
  runtime with FUSE (the sandbox runs AppImages with `APPIMAGE_EXTRACT_AND_RUN=1`, as CI does).
- The tag `v1.0.10` did not exist yet: the vendoring ran from the commit (D6). Re-running `scripts/sync-design v1.0.10`
  once the tag exists should change nothing.

## 2026-11-15 - step 4 (cloud session, branch `claude/sharp-cori-29w5eg`)

- Step 4, dead code (D5): eight removals, one commit each (a402dfc, 1009cd3, 815e65c, 7dba3a6, 36b88f1, c48678a,
  3b92c29, 86707f6): `WheelController::refreshProviders`, `WheelLogic::centreOf` (and its test column), `AppMetadata::Shelf` and
  `Find`, `WheelLink::lastError`, `DesktopEntry::terminal`, `DiscoveredApplication::launchTarget`, the unread
  `assets/arcade/tokens.json`, two unreferenced Linux packaging icons. Evidence per item is in D5. Vendored files untouched; findings in them are only reported.
- Checks run: clang 18 `-Wunused*` (incl. member-function, private-field, macros, template) on every Wheel and test
  translation unit, g++ 13 `-Wunused*` on `ArcadeLinkProvider.cpp` (clang 18 cannot parse it), clang-tidy
  `misc-unused-using-decls`/`misc-unused-alias-decls`: no warning in Wheel's code. Name search of every non-vendored
  header symbol across `src/ qml/ tests/ scripts/ packaging/ .github/ docs/`, and a reference check of every QML, asset,
  script, packaging and fixture file.
- Tested locally with Qt 6.8.3: clean CMake build with `-DWHEEL_WARNINGS_AS_ERRORS=ON`
  `-DARCADE_REQUIRE_LAYERSHELLQT=OFF`, 21/21 ctest (offscreen; baseline before the changes was also 21/21), clang-format 18
  on every non-vendored C++ file, `qmllint_gate`, `packaging/check-docs.py`, `packaging/check-link-vendor.py`. The Windows
  and macOS code compiles only in CI. On Windows, removing `app.launchTarget = path;` leaves `path` in use. On macOS the
  first push missed the field's slot in the app list initializer, and the DMG builds failed until it was removed.

## 2026-11-14 - steps 2 and 6 (cloud session, branch `claude/sharp-cori-29w5eg`)

- Step 6 (ba1b238): rc-only releases through Link `app-publish` (Inno), `promote.yml`, `nightly.yml`; the stage job
  collects assets with `publish-release.collect_assets`.
- Step 2 (0b1b126, D4): an `#include` graph of `src/` (the vendored `link/qt` excluded) showed configuration including
  the platform interface for one value type; `DiscoveredApplication` has its own header now.
- Tested locally with Qt 6.8.3 (aqt): CMake build with `-DWHEEL_WARNINGS_AS_ERRORS=ON` and
  `-DARCADE_REQUIRE_LAYERSHELLQT=OFF` (no LayerShellQt here; CI builds it), 21/21 ctest (offscreen),
  clang-format 18 on the changed headers. Windows and macOS compile only in CI.

## 2026-11-14 - Link v1.0.7

- PR #17 CI green on 12f3869 (Linux AppImage, Windows setup, macOS arm64 and Intel), after the format and qmllint fixes.
- Re-vendored Arcade Link v1.0.7 (D3). Build, qmllint gate, vendor checks, docs, release tests and 21/21 ctest pass
  locally on Qt 6.12. Removing `arcade.wheel` from Link's `LEGACY_PENDING` is Link's change, once this is on main.

## 2026-11-13 - local build and test pass, W0, CI1 (PR #17)

- The whole branch now **builds and passes locally** against Qt 6.12 (owner's PC): `cmake -DBUILD_TESTING=ON
  -DWHEEL_WARNINGS_AS_ERRORS=ON -DARCADE_REQUIRE_LAYERSHELLQT=OFF`, then `ctest` with `QT_QPA_PLATFORM=offscreen
  QT_QUICK_BACKEND=software` in a clean environment (`env -i`, temporary HOME and runtime dir): 21 of 21 pass,
  including the controller and QML tests that were "not run" above. Run inside a Hyprland session's environment,
  five WheelController tests fail (the Hyprland variables leak into them); CI and a clean environment are unaffected,
  noted as a test-isolation item for the untangle step.
- [PR #17](https://github.com/qa-p1/Arcade-wheel/pull/17) opened for CI on Linux, Windows and macOS.
- cf4109c ci: timeouts and retries on every network step (CI1; the Qt install cache was already on).
- 4e08f1e perf: `arcade_trim()` (vendored Link C twin of `runtime::trim`) after startup and 2 s after each hide (W0).
  The old `malloc_trim` call sites (Settings and Activity release) use it too.
- WH1 (software renderer) is not switched: the spec allows it only if the open animation and frame time stay equal,
  and CI's llvmpipe can't show that for a real GPU. Needs a measured comparison on a real session first (D-entry).
- Waiting on Link v1.0.7: re-vendor `qt/` and leave `LEGACY_PENDING` (fleet rule "One launcher per app").
- First PR CI run (2725f33): Linux and Windows `scripts/check` failed on two things the authoring sandbox could not
  run: clang-format 18 (ConfigStore.cpp, main.cpp, two tests) and the zero-warning qmllint gate
  (`SettingsWindow.qml` read `cursorPosition` on a `QQuickItem`). Fixed in 2156d37 and 12f3869 (the Escape handler
  now checks `instanceof TextInput || TextEdit`); format, qmllint gate, vendor, docs, shortcut and release checks and
  all 21 tests pass locally (clang-format 18.1.8 from pip, Qt 6.12).

## Step 1-2 - behaviour lock and P0/P1 fixes
- ab7f123 build: vendor Link v1.0.6 (carries WH-5). **tested locally**: `packaging/check-link-vendor.py --source`, `check-vendored-tools.py --source` pass.
- 9890aff test: every P1 finding as QEXPECT_FAIL. ConfigStore/WheelPaths/WheelLogic tests **tested locally** in a Qt 6.4 harness (xfail confirmed against the old code). Controller/QML tests **not run** (need Qt 6.8).
- 9d53727 fix WH-3/WH-4/WH-6 (import validation, limits, backups). **tested locally** (tst_ConfigStore, 40 pass, Qt 6.4 harness). Controller part (error cleared) **not run**.
- 9961587 fix WH-2/W1/RD-WHEEL-1 (one cache folder). **tested locally** (tst_WheelPaths, 9 pass).
- 3068085 fix WH-1/WH-5 (Escape). **not run** (QML; needs Qt 6.8). WH-5 test cannot be shown failing: the kit fix was already in the vendor commit.
- 437e2e9 fix RD-WHEEL-3 (pointer restore). **not run** (controller test needs Qt 6.8).
- b2160fd monitor placement function + test. **tested locally** (tst_WheelLogic, 42 pass).

- CLI-1/CLI-2: unknown arguments exit 2 with usage, `--status` exits 3 when not running; `tests/cli-contract.py`. **not run** (needs a Qt 6.8 build; CI).

- Step 3 repo files: SECURITY.md, THIRD_PARTY_NOTICES.md (Qt LGPL relinking), Dependabot, dev.arcade.Wheel icons and symbolic tray svg in packaging/linux, CHANGELOG Unreleased. Documents; not run.

- RT-4 crash reports (`src/core/CrashReport.*`, `arcade/wheel/crashes`, newest 20). **tested locally** (tst_CrashReport, Qt 6.4 harness, a child SIGSEGV writes a report). Wiring in main.cpp **not run**.

Open: unpacked relaunch (RT-7), WH1, desktop harness test, CI, notices, SECURITY, CHANGELOG, untangle.

## Step 0 - brief
- docs: SPEC.md, audit/, DECISIONS.md, PROGRESS.md - not applicable (documents).
