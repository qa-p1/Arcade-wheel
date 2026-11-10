# Wheel 1.0.0 progress

Newest first. "tested locally" gives the command; "not run" says why.

Qt 6.8 is not installed in the authoring sandbox (Qt 6.4.2 only, no network install allowed), so the full CMake build,
the QML and the QtTest suite could not run there. Code that does not need Qt Quick was built and run against Qt 6.4
in a throw-away harness (ConfigStore, WheelPaths and the Link path code); everything else is **not run** until CI.

## Step 1-2 - behaviour lock and P0/P1 fixes
- ab7f123 build: vendor Link v1.0.6 (carries WH-5). **tested locally**: `packaging/check-link-vendor.py --source`, `check-vendored-tools.py --source` pass.
- 9890aff test: every P1 finding as QEXPECT_FAIL. ConfigStore/WheelPaths/WheelLogic tests **tested locally** in a Qt 6.4 harness (xfail confirmed against the old code). Controller/QML tests **not run** (need Qt 6.8).
- 9d53727 fix WH-3/WH-4/WH-6 (import validation, limits, backups). **tested locally** (tst_ConfigStore, 40 pass, Qt 6.4 harness). Controller part (error cleared) **not run**.
- 9961587 fix WH-2/W1/RD-WHEEL-1 (one cache folder). **tested locally** (tst_WheelPaths, 9 pass).
- 3068085 fix WH-1/WH-5 (Escape). **not run** (QML; needs Qt 6.8). WH-5 test cannot be shown failing: the kit fix was already in the vendor commit.
- 437e2e9 fix RD-WHEEL-3 (pointer restore). **not run** (controller test needs Qt 6.8).
- b2160fd monitor placement function + test. **tested locally** (tst_WheelLogic, 42 pass).

- CLI-1/CLI-2: unknown arguments exit 2 with usage, `--status` exits 3 when not running; `tests/cli-contract.py`. **not run** (needs a Qt 6.8 build; CI).

- Step 3 repo files: SECURITY.md, THIRD_PARTY_NOTICES.md (Qt LGPL relinking), Dependabot, dev.arcade.Wheel icons and symbolic tray svg in packaging/linux, CHANGELOG Unreleased. Documents; not run. SECURITY.md cites `tests/crash-report.py` (to be written with crash reports).

Open: CLI-2 (unknown argument), --status exit code 3, crash reports, unpacked relaunch, WH1, desktop harness test, CI, notices, SECURITY, CHANGELOG, untangle.

## Step 0 - brief
- docs: SPEC.md, audit/, DECISIONS.md, PROGRESS.md - not applicable (documents).
