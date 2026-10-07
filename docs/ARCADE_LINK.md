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

Saved Box pipelines are fetched with `box.pipelines` on a worker and cached.
Each picker row stores `box.pipeline.run` and `options.pipeline`, and uses the
pipeline's own input types and effects. `app.changed` or a new endpoint
refreshes the cache; opening or searching the picker performs no IPC. A removed
pipeline keeps its saved slot and shows an unavailable reason. The optimized
screenshot flow uses input `none` and carries the pipeline's ↗ badge.
The Input selector uses readable labels and lists only modes the action and
selection resolver support on this platform. The detail area shows the app,
action or pipeline name, and effects rather than protocol IDs.

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
- `file-selection`: Look's `look.preview_selection` is called first with
  `options.resolveOnly: true`, which returns selected files without opening a
  preview. It is offered only when Look advertises the resolver and file
  outputs on this platform. Actual file kinds are checked before the target
  runs; no selection returns Look's unavailable reason. Linux's resolver is
  currently absent, so this mode is hidden there outside mock tests.

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

For example, these calls use the debug CLI against a running Wheel:

```sh
arcade-link invoke wheel wheel.show
arcade-link invoke wheel wheel.add_action --text '/usr/bin/printf "Hello"' --hint command
```

Wheel advertises both actions at version 1. Its `app.status` callback includes
`status.mode: "background"` when started with `--background`, and
`status.mode: "foreground"` for Settings/default launches. Arcade Tools uses
the initial launch mode when relaunching after an update. Opening Settings
later does not change that mode.

## Discovery and execution

Registry reads, endpoint authentication, subscriptions, manifest writes and
invokes run off the UI thread. `QFileSystemWatcher` discovers peers installed
or started later. `app.changed` refreshes their live descriptions. Opening an
editor or recording a shortcut reads the cache only. There is no idle timer
or polling. Bounded waits occur only during a user-requested invocation,
startup authentication or cancellation of a pending add request.

The Qt v1 module is unchanged in `src/link/ArcadeLink.{h,cpp}` and pinned to
Arcade-link commit `539fa91` by `VENDORED.json`. The vendor check compares its
checksums with that commit, alongside the vectors and release generator.
`WheelInvoke` supplies bounded jobs and one-shot deadlines around that pinned
protocol. `ARCADE_HOME` redirects registry, endpoints and handoff storage.
Smoke-test/offscreen instances never publish to the real registry unless
given an explicit `ARCADE_HOME`.

## Settings and CLI

`link.enabled` is the master switch. Off publishes an installed manifest with
no actions, closes Wheel's listener and hides all peer entries.
`link.disabledPeers` hides one peer's actions in Wheel only. Settings →
Connected apps shows every peer with its monochrome glyph,
state and “Use with Arcade Wheel” toggle for installed action peers. Arcade
Tools appears only when its Get option is useful, with no peer toggle.
Missing apps have one short pitch
and Get. Get invokes `tools.install` with `options.app` in an available Arcade
Tools, launching it if needed; otherwise it opens that app's releases URL
through `QDesktopServices`. Nothing is opened
automatically. A local peer switch reports “Arcade Box is turned off in
Connected apps.”; the local master switch reports “Connections to other
Arcade apps are off in Connected apps.” Diagnostics expands the registry path, Wheel's listener state,
peer endpoint states and the last errors.

The shortcut recorder compares recorded keys with effective shortcuts in the
cached registry and warns “Used by Arcade Box” (or the relevant app). It keeps
the current shortcut until the user chooses another key or confirms Use anyway.

```sh
arcade-wheel --arcade-manifest  # print only; never writes defaults
arcade-wheel --version
```

## Verification and platforms

Use the isolated runner for every app or GUI test:

```sh
cmake --build build -j3
python3 ../../Rust/Arcade-link/tools/e2e.py run -- env QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
python3 ../../Rust/Arcade-link/tools/e2e.py --only wheel
python3 ../../Rust/Arcade-link/tools/e2e.py --only failure
```

The real Xvfb tests check wheel.show, Settings confirm/cancel/close, a saved
Box text pipeline with a clipboard slot, and Look's ordinary file preview. The
provider suite runs `arcade-link mock` using copied Box/Lens fixtures; the CLI
must be on PATH (otherwise those cases explicitly skip). It covers standalone
absence, late peers, toggles, migration, pipeline options, selection chaining,
progress, UI responsiveness, cancellation, size limits, peer crash, timeout,
Private mode and secret errors.
The failure group verifies crash, cancel, busy/forced quit, broken registry
entries and stale endpoint recovery, including killing and restarting real
Wheel. Windows/macOS packaging and runtime checks have not been run here.

| Feature | Linux X11 | Linux Wayland | Windows | macOS |
|---|---|---|---|---|
| Registry / resident and one-shot calls | Real isolated runs | Not run | Not run | Not run |
| Settings placement | Real Xvfb run | Not run | Not run | Not run |
| Lens selection | Mock run; real capture owned by Lens | Not run | Not run | Not run |
| File selection input | Mock run; hidden for real Look | Hidden | Not run | Not run |

Only Linux has been built on this machine. Platform-neutral Qt paths and
Windows/macOS branches are code-reviewed; cross-platform compilation and real
runs remain required before release.
