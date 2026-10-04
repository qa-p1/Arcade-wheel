# Architecture

`arcade-wheel` is one resident process. Its two Qt Quick views are the transient wheel overlay and the optional Settings window. A local socket lets another invocation request Settings, press/release for diagnostics, restart, or quit. Closing Settings never stops the core.

The shared core consists of:

- `ConfigStore`: schema migration, first-run defaults, validation, atomic JSON saves, import/export.
- `WheelLogic`: clockwise angular selection, center dead zone, and deck scrolling in logical screen coordinates.
- `WheelController`: trigger lifecycle, active deck, pointer selection, settings mutations, and immediate hide before action execution.
- `ActionDispatcher`: built-in action routing and a small `ActionProvider` registry. `ArcadeBoxProvider` is the first external provider.
- `PlatformBackend`: release-aware trigger signals, cursor position, overlay preparation, app discovery/launch/focus, system actions, and start-on-login.

The QML wheel consumes deck data and selection state but does not execute actions. Settings reuses the same `WheelView` for live preview. Editing actions or appearance updates the resident controller immediately.

## Configuration schema

The current schema version is 3. Older files are migrated on load. The top level has `general`, `trigger`, `appearance`, `behaviour`, `centerGestures`, and ordered `decks`. Each deck has a stable ID, name, and 4–8 ordered actions. Each action has a stable ID, type, name, icon key, and `payload` object. Six slots are the default. Unknown action types and unavailable providers are preserved so an import cannot silently discard a user's wheel. A future contextual deck can add matching metadata to a deck and a resolver can select it before fixed decks; fixed decks remain the default.

`centerGestures` holds `clickIntervalMs` (160–500), `longPressMs` (250–1500), and one group each for `singleClick`, `doubleClick`, `tripleClick`, and `longPress`. A group has a `name`, an `enabled` flag, and up to 16 actions using the same action shape as deck slots. Missing groups are filled in from defaults during normalization, so older configurations gain new gestures disabled.

## Trigger path

On Hyprland/Wayland the backend registers one shortcut with the XDG GlobalShortcuts portal and receives `Activated` and `Deactivated` signals. On Windows a passive native hook watches only the configured key and required modifiers and emits the same two signals. On macOS the backend registers the shortcut with the Carbon hot key API and maps its pressed and released events to the same signals; only single keyboard shortcuts are accepted. `WheelController` applies the optional hold threshold, opens the preloaded overlay, and handles release. A release first hides the overlay and then queues action execution on the next event-loop turn.

The settings UI does not promise tap passthrough because no platform backend can provide it reliably for all keys without taking over normal input.

## Center gestures

The overlay forwards left-button presses and releases inside the hub to `WheelController`, which recognizes the gesture itself. A long-press timer starts on press when that gesture is enabled. Each release in the hub counts a click. The controller resolves the click count immediately when no longer gesture is enabled; otherwise it waits for `clickIntervalMs`. Releasing the trigger resolves a pending count at once. One click runs `singleClick` if enabled and otherwise cancels. Two clicks run `doubleClick` and three run `tripleClick`. A count with no enabled group cancels. Running a group goes through the same hide-then-execute path as a slot, so a later trigger release finds the wheel closed and does nothing. Leaving the hub, right-clicking, changing decks, editing gestures, or closing the wheel clears any pending gesture. Preview mode never executes groups.

## Coordinates

The platform backend supplies a global logical cursor position. The controller finds the `QScreen` that contains it, covers that screen with the overlay, and draws the wheel at the screen center. That center is also the selection origin. After the first frame is presented, the controller asks the backend to move the pointer to the origin (Hyprland IPC on Linux, `QCursor::setPos` on Windows and macOS), so selection always starts in the cancel zone. Pointer events from before the move lands are ignored briefly. The overlay reports pointer positions in screen-local logical coordinates.

## Extension points

New built-in actions belong in `ActionDispatcher` only when they are small, common desktop functions. External tools implement `ActionProvider` and are registered with the dispatcher. Provider invocations use argument arrays, never shell-concatenated command lines. Platform-specific behaviour belongs behind `PlatformBackend`.
