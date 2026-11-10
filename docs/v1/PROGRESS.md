# Wheel 1.0.0 progress

Newest first. "tested locally" gives the command; "not run" says why.

Qt 6.8 is not installed in the authoring sandbox (Qt 6.4.2 only, no network install allowed), so the full CMake build,
the QML and the QtTest suite could not run there. Code that does not need Qt Quick was built and run against Qt 6.4
in a throw-away harness (ConfigStore, WheelPaths and the Link path code); everything else is **not run** until CI.

## Step 0 - brief
- docs: SPEC.md, audit/, DECISIONS.md, PROGRESS.md - not applicable (documents).
