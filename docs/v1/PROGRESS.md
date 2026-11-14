# Wheel 1.0.0 progress

Newest first. "tested locally" gives the command; "not run" says why.

Qt 6.8 is not installed in the authoring sandbox (Qt 6.4.2 only, no network install allowed), so the full CMake build,
the QML and the QtTest suite could not run there. Code that does not need Qt Quick was built and run against Qt 6.4
in a throw-away harness (ConfigStore, WheelPaths and the Link path code); everything else is **not run** until CI.

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
