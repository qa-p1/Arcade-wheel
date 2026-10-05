# Arcade Link

Arcade Wheel works with the other Arcade apps (Box, Lens, Look, Clipboard)
through [Arcade Link](https://github.com/qa-p1/Arcade-link): a file-based
registry and one local socket per running app. The Qt implementation is
vendored in `src/link/` (pinned in `src/link/VENDORED.json`, checked by
`packaging/check-link-vendor.py`). Wheel works exactly the same when no other
Arcade app is installed.

## What Wheel exposes

| Action | Accepts | Notes |
|---|---|---|
| `wheel.add_action` | `text/url`, `text/plain` with the `command` hint, `file/*`, `folder/reference` | Opens Settings with the action pre-filled. **Nothing is saved until the user selects a slot and saves it**; the caller's job ends with success, or with `denied`/`user_cancelled` when the user cancels or closes Settings. One request waits at a time (`busy` otherwise). Commands become Wheel `command` actions, which run directly, never through a shell; the picker says so. |
| `wheel.show` | — | Opens the wheel. |

## Settings

The `link` section of `config.json`: `enabled` ("Connect with other Arcade
apps") and `disabledPeers` (the per-app toggles). With the switch off,
Wheel's manifest lists no actions and nothing listens.

## Command line

```sh
arcade-wheel --arcade-manifest    # Wheel's manifest (no side effects; never writes defaults)
arcade-wheel --version
```

Smoke-test and offscreen instances never register in the real registry
unless they are given their own `ARCADE_HOME`.

## Isolated verification

Run `python3 ../../Rust/Arcade-link/tools/e2e.py --only wheel` from the
Wheel checkout. The Wheel check starts
the real executable under a private D-Bus session and Xvfb with temporary
HOME/XDG/Arcade directories. It checks `wheel.show`, confirms a URL into a
selected slot through Settings, and checks both Cancel and closing Settings
without changing the saved deck. The Qt test checks the wire-level
`denied`/`user_cancelled` response; the CLI displays `Cancelled.`.

## Platforms

| | Linux X11 | Linux Wayland | Windows | macOS |
|---|---|---|---|---|
| Exposed actions | tested (offscreen/Xvfb) | build only | build only | build only |
