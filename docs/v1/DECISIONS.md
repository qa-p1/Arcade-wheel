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
