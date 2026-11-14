# Spec 9: Arcade Wheel 1.0.0

**Outcome:** Wheel 1.0.0. It delivers:
- one cache folder;
- safe imports;
- Escape everywhere;
- the pointer restored after use;
- the memory budget met with the software renderer if it costs no UX;
- C++ contract tests from Link passing.

Starting point: `qa-p1/Arcade-wheel`, Wheel 0.5.0 (C++/Qt6 QML). Read `AUDIT/wheel.md` and
`AUDIT/real-desktop.md` §Wheel.

## Findings to fix

| ID | P | What | Where |
|---|---|---|---|
| WH-3 | P0 | Importing a settings file with a 5 MB deck name freezes the resident (55–75% CPU, 900 MB, no `--status` for minutes). Add size and length limits before parsing. | config import |
| W1 / WH-2 / RD-WHEEL-1 | P1 | Qt writes `~/.cache/Arcade Wheel/Arcade Wheel/` because the organization and application are both "Arcade Wheel". The next start then "migrates" it, and Qt recreates it. Set the org and app names (or the cache paths) so everything lives under `~/.cache/arcade/wheel`. Retire the legacy folder once. | `src/main.cpp:120-122,235-252` |
| WH-1 / RD-WHEEL-2 | P1 | Escape does nothing at the top of Settings. | `qml/settings/SettingsWindow.qml:59` |
| WH-5 | P1 | Escape can't cancel "Delete this deck?". Fixed in Link's QML kit (`specs/LINK.md` §2). | kit `Dialog.qml:21` |
| WH-4 / WH-6 | P1 | Import accepts `decks` as a string or `[]` and replaces the user's decks; unknown action types, duplicate ids and out-of-range values are stored; valid non-object JSON says "no error occurred"; `--status` keeps a stale error. Validate against the schema, never replace data on an invalid import, and prune import backups. | import / status |
| RD-WHEEL-3 | P1 | The wheel warps the pointer to the screen centre and never restores it. Restore the pointer on close, unless an action moved it on purpose. | show / hide |

## Memory

Item WH1: `QQuickWindow::setGraphicsApi(QSGRendererInterface::Software)`, as Shelf does at `src/main.cpp:292`. Also items U1 and W0. Budget: background 30 PSS / 50 RSS.
Wheel costs about 77 PSS / 135 RSS for an invisible wheel. Measured: Qt's software renderer (which Shelf already
uses) takes it to 70 RSS / 59 PSS. Switch only if the open animation and frame time stay equal, measured in CI.
Otherwise release the scene graph on hide. Also apply the Wheel items from `AUDIT/memory.md`.

## Also

- Add a LICENSE file and `THIRD_PARTY_NOTICES` (Qt LGPL).
- Pass the Link C++ contract suite.
- Automated physical hold and release of the shortcut (a portal press and release pair) under the harness.
- A multi-monitor placement test.

## CI

Measured: main CI takes 8–37 minutes. Use a Qt install cache, and put a timeout and retry on apt (CI1).

## Acceptance

All `PLAN.md` exit gates, plus: the import fuzz set (malformed, huge, wrong schema) never freezes and never loses
decks, a single-cache-folder test after upgrade, an Escape test, and a pointer-restore test.
