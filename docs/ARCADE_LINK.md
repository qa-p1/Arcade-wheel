# Arcade Link

Arcade Wheel discovers Box, Lens, Look and Clipboard through Arcade Link v1.
No manager or broker needs to run. Without peers the normal launcher, editor,
commands and center gestures behave as before; the Arcade category is hidden.

## Actions and saved slots

The ActionPicker searches available actions grouped by their owning app. Its
monochrome badges and the Connected apps resources are copied from the Link
assets into `assets/arcade/`. Unavailable saved slots remain in the deck,
dimmed with the standard reason. They cannot be invoked until repaired or the
peer becomes available. An incompatible action version needs an app update.

Schema 4 stores `type: "arcade"` and a payload:

```json
{
  "app": "arcade.box",
  "action": "box.pipeline.run",
  "version": 1,
  "input": "none",
  "options": {"pipeline": "p-optimized-screenshot"}
}
```

`preset` is optional. `options` passes unchanged to the peer. Old `arcade_box`
slots, including center gestures, migrate to `app: "arcade.box"`,
`action: "box:<toolId>"`, version 1. Every old field survives, and a complete
copy remains in `payload.legacyPayload`. Legacy `clipboard-*` input becomes
`clipboard`; other unrecognized input strings remain in the preserved payload
and need review in the editor. Existing shortcuts are never reset.

Input modes:

- `none`: actions that take no input (also actions accepting `*`).
- `clipboard`: snapshots text, rich text, URLs, images, local files or batches
  when invoked. File paths travel by reference; images and text over 256 KiB
  use Wheel-owned private handoff files, removed on success, error or cancel.
- `lens-selection`: Lens's `lens.capture` runs first; only output types the
  target accepts are passed along. Wheel unmaps before requesting a capture.
- `file-selection`: only offered off Linux when Look advertises selection
  outputs accepted by the target. The current Look `look.preview_selection`
  opens a preview without returning files, so this mode stays hidden. The
  direct Preview selection action is available on supported platforms.

The owner applies its normal grants, confirmations, Private mode and secret
guard. Outbound actions show ↗ and a payload preview. The Activity window
shows progress, results, standard errors and Cancel. Jobs have a two-minute
deadline; cancelling disconnects after sending `job.cancel`, so a peer cannot
keep the caller waiting indefinitely. Clipboard content is never changed by
a result.

## What Wheel exposes

| Action | Accepts | Behavior |
|---|---|---|
| `wheel.show` | — | Opens the wheel. |
| `wheel.add_action` | `text/url`, `text/plain;hint=command`, `file/*`, `folder/reference`, `structured/arcade-action` | Opens Settings with a draft. The user chooses a slot and saves it; cancellation, closing the picker or Settings, or a disconnected caller removes the draft. Only one request waits at once. |

`structured/arcade-action` data is `{app, action, version, title, preset?,
input?, options?}`. The four input modes above are accepted; omitted input
means `none`. Pipeline references use `action: "box.pipeline.run"` and
`options.pipeline`. Unknown peer actions can be saved for later but remain
unavailable, with their reason visible. Nothing arriving over Link silently
changes a deck. Commands become direct executable invocations, never shell
strings. Failed saves leave the caller's request pending.

## Discovery and execution

Registry reads, endpoint authentication, subscriptions, manifest writes and
invokes run off the UI thread. `QFileSystemWatcher` discovers peers installed
or started later. `app.changed` refreshes their live descriptions. Opening an
editor or recording a shortcut reads the cache only. There is no idle timer
or polling. Bounded waits occur only during a user-requested invocation,
startup authentication or cancellation of a pending add request.

The Qt v1 module is unchanged in `src/link/ArcadeLink.{h,cpp}` and pinned by
`VENDORED.json`; `packaging/check-link-vendor.py` checks its checksums.
`WheelInvoke` supplies bounded jobs and one-shot deadlines around that pinned
protocol. `ARCADE_HOME` redirects registry, endpoints and handoff storage.
Smoke-test/offscreen instances never publish to the real registry unless
given an explicit `ARCADE_HOME`.

## Settings and CLI

`link.enabled` is the master switch. Off publishes an installed manifest with
no actions, closes Wheel's listener and hides all peer entries.
`link.disabledPeers` hides one peer's actions in Wheel only.

```sh
arcade-wheel --arcade-manifest  # print only; never writes defaults
arcade-wheel --version
```

## Verification and platforms

Use the isolated runner for every app or GUI test:

```sh
cmake --build build
python3 ../../Rust/Arcade-link/tools/e2e.py run -- env QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
python3 ../../Rust/Arcade-link/tools/e2e.py --only wheel
```

The real Xvfb test checks wheel.show and Settings confirm/cancel/close. The
provider suite runs `arcade-link mock` using copied Box/Lens fixtures; the CLI
must be on PATH (otherwise those cases explicitly skip). It covers standalone
absence, late peers, toggles, migration, pipeline options, selection chaining,
progress, UI responsiveness, cancellation, size limits, peer crash, timeout,
Private mode and secret errors.

| Feature | Linux X11 | Linux Wayland | Windows | macOS |
|---|---|---|---|---|
| Registry / resident and one-shot calls | Real isolated runs | Not run | Not run | Not run |
| Settings placement | Real Xvfb run | Not run | Not run | Not run |
| Lens selection | Mock run; real capture owned by Lens | Not run | Not run | Not run |
| File selection input | Hidden | Hidden | Hidden until Look returns selections | Hidden until Look returns selections |

Only Linux has been built on this machine. Platform-neutral Qt paths and
Windows/macOS branches are code-reviewed; cross-platform compilation and real
runs remain required before release.
