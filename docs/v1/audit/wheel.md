# Arcade Wheel 0.5.0 audit

Resume in progress, 2026-11-06. Installed AppImage; private D-Bus, Xvfb :93, fresh HOME/XDG under the requested scratch root (`resume/`). A short `/tmp/cswt-c7347b9d` symlink resolves into that root and is used for Unix socket addresses. No real compositor or owner process is controlled. Screenshot coordinates are private-display coordinates.

## Coverage
| Feature | Tested? | Method / inputs | Result |
|---|---|---|---|
| CLI enumeration, version | yes | --help / --version | pass; wheel/resume-cli.txt |
| All 10 settings pages | yes | GUI clicks, 1080×720 | render; r-page-*.png |
| Fresh install offer | yes | GUI Don't ask again | closes and opens editor |
| New deck / selector / options | yes | GUI | new deck persists in exported JSON |
| Settings portable export | yes | CLI, live instance | pass |
| Action editor | yes | GUI, empty slot | opens; Escape dismisses one layer; pointer leave stays open |
| Wheel preview | yes | --preview, pointer leave, explicit focus to Settings | stays open; r-preview*.png and --status |
| Top-level Settings Escape | yes | GUI Escape after editor closed | WH-1 |
| Six slices | yes | imported real command actions writing to private recorder; click each clockwise | pass exact slot0..5; r-slot-results.json |
| File / URL / application / missing command | yes | private file, `.invalid` URL → xdg-open recorder, private .desktop recorder, missing executable | first three dispatch correctly; missing executable returns explicit status error; r-type-results2.json |
| Idle after use | yes | /proc 30 s | 0.067% CPU, 36 threads, 39 fds; r-metrics-heavy.json |
| Center gestures | yes | imported real command actions on single/double/triple/long press; events recorded (r-gestures.json) | pass |
| Import validation | yes | CLI `--settings-import` against a live resident: 20 inputs (malformed, empty, array, `{}`, null, schema 99/-1/"two"/absent, unknown action type, decks not a list, no decks, 500 actions, 50 duplicate decks, out-of-range appearance, 200 KB / 5 MB names, 53 MB junk, missing file) | no crash; findings WH-3, WH-4, WH-6; wheel/r2-import-results.txt |
| UI scale | yes | QT_SCALE_FACTOR 1.25 / 1.5 / 2 on 1920x1080 | layout scales cleanly; at 2 the default window (2160x1440) exceeds the screen, no WM to clamp it (Xvfb artefact, not a finding); r2-scale-*.png |
| `--restart` / `--quit` | yes | resident in background and with Settings open | pid changes, comes back as background resident (Settings is not reopened), `--quit` rc 0, `--status` then `{"running":false}` rc 1, no coredumps (`coredumpctl --since -10min` shows only another agent's wlrctl-bin) |
| Backend idle | yes | /proc 30 s samples | background: 0.000% CPU, 32 threads, 30 fds, 125 MiB RSS; Settings open: 225 MiB / 39 threads; 12 s after closing: 165 MiB / 32 threads (settings engine released as designed); all 0.000% CPU |
| Cache folders (W1) | yes | fresh profile, two starts | reproduced, WH-2 |
| Escape in each window | yes | Settings top (WH-1), action editor (pass), deck menu (pass), Delete-deck confirm (WH-5), Connected apps details (nothing to close), install prompt (Escape ends the whole process; acceptable) | WH-1, WH-5 |
| Global F8 / Wayland / physical media & desktop actions | no | isolation forbids real session controls | needs real desktop |

## Findings

### WH-1 (UX) Escape does nothing at the top of Settings
Method: GUI in Xvfb. Open Settings, edit an empty action, press Escape: the action editor closes correctly. Press Escape again: Settings stays open. Expected under the owner's rule: close only when back at this top view. Evidence: `wheel/r-editor.png`, `r-editor-escape.png`, `r-esc-settings.png`. Suspected source: `qml/settings/SettingsWindow.qml:59` implements only `StandardKey.Close` (Ctrl+W), with no Escape handler at the root.

### WH-2 (wrong result) Qt's pipeline cache re-creates `~/.cache/Arcade Wheel/Arcade Wheel`, and the next start "migrates" it, so both cache folders exist (owner issue W1)
Method: fresh profile (HOME and all XDG dirs private), installed AppImage 0.5.0 on xcb, `--settings`, dismiss the install prompt, then restart the resident; `find` on `XDG_CACHE_HOME` between steps.
Steps: with no `~/.cache/Arcade Wheel`, the first window draw creates `cache/Arcade Wheel/Arcade Wheel/qtpipelinecache-x86_64-little_endian-lp64`. Quit and start again (`--background`): that folder is moved to `cache/arcade/wheel/qtpipelinecache-...` and the old one disappears. Open Settings and Qt creates `Arcade Wheel/Arcade Wheel` again. Where both exist together (owner's machine) the migration finds the target present and keeps both.
Expected: one cache folder, `~/.cache/arcade/wheel`. Actual: Qt Quick writes its RHI pipeline cache to the default `CacheLocation` (organization "Arcade Wheel" + application "Arcade Wheel"), and Wheel's legacy-folder check then treats that very folder as an old 0.4 install on each real start.
Evidence: wheel/r2-cache-folders.txt. The owner's real tree (read-only `ls`) matches: `~/.cache/arcade/wheel/{logs,qmlcache,qtpipelinecache}` plus `~/.cache/Arcade Wheel/Arcade Wheel/qtpipelinecache`.
Suspected source: `src/main.cpp:120-122` (org/app names drive Qt's cache path); `src/main.cpp:235-252` (legacy check plus `WheelPaths::migrate`); `src/link/qt/ArcadeAppDirsTable.inc:17` lists `Arcade Wheel/Arcade Wheel` as a legacy cache folder. Direction: redirect Qt's cache or drop the cache kind from legacy detection.

### WH-3 (broken) Importing a settings file with a multi-megabyte name freezes the resident for minutes
Method: CLI against a live resident. `--settings-export` a good file, set `decks[0].name` to 200 KB, 1 MB, 5 MB of `A`, `--settings-import` it.
Expected: reject oversize strings, or accept quickly. Actual: 200 KB takes about 2 s and is accepted; the 5 MB name is accepted and written (settings.json 5 MB) and the resident's main thread then spins at 55-75% CPU, 900 MB RSS, for over 2 minutes (90 CPU seconds) without answering `--status` ("Arcade Wheel did not answer"); later imports print "did not confirm". Looks quadratic in length. I killed my own resident afterwards; no coredump. A 53 MB junk file is rejected cleanly ("Invalid JSON: illegal value"). Not tried through the GUI name field.
Evidence: wheel/r2-import-results.txt (hugename onwards). Suspected source: no length limits in `ConfigStore::normalize`/`portable` (`src/config/ConfigStore.cpp:199-305, 430-470`); the slow GUI-thread consumer was not located (could not attach gdb, ptrace_scope 1).

### WH-4 (wrong result) Import error text for valid non-object JSON; `--status` keeps a stale error
Method: `--settings-import` with `[1,2,3]` and `{}` prints "ERROR: Invalid JSON: no error occurred" although the JSON is valid. Expected: "not a settings file". Separately, after a failed import of a missing file `--status` reports `"error":"No such file or directory"` and keeps it through later successful imports until the resident restarts. Expected: cleared on success.
Evidence: wheel/r2-import-results.txt. Suspected source: `ConfigStore.cpp:397-403` and the controller's last-error field.

### WH-5 (UX) Escape does not dismiss the "Delete this deck?" confirmation
Method: GUI. Your wheel, Deck options, Duplicate deck, Deck options, Delete deck, Escape. Expected: Escape cancels (Keep). Actual: dialog stays; only the Keep button works. Evidence: wheel/r2-delete-deck-dialog.png, r2-delete-deck-after-escape.png (identical).
Suspected source: `src/link/design/qml/Arcade/Dialog.qml:21` sets `NoAutoClose` for every destructive dialog, disabling Escape and outside click. Kit-wide property, so every Arcade QML app (Shelf too) inherits it.

### WH-6 (UX) Import validation is lenient; every import leaves an unpruned backup
Method: same 20-file run, inspecting `settings.json` after each.
Actual: schemaVersion absent or the string "two" is accepted as legacy v0; `decks` as a string or `[]` is accepted, the user's decks are silently replaced by one default "Applications" deck and the CLI prints "Imported settings" (a backup is kept); an action of type `frobnicate` is stored (dispatch later says "Unknown action type", so safe but invisible until used); 50 decks sharing one id are all kept; `radius -5`, `opacity 99`, `iconSize 1e9` are saved unchanged (the wheel still draws normally, runtime clamps: r2-badnums-preview.png). Each successful import writes `settings.json.backup-<timestamp>-<uuid>.json`; nothing prunes them (12 after this run).
Expected: reject or repair structurally wrong input with a message; bound the backups. Evidence: wheel/r2-import-results.txt. Suspected source: `ConfigStore.cpp:204-305` normalize, `ConfigStore.cpp:470-495` backupCurrent.

## Backend
Idle background resident: 0.000% CPU over 30 s, 32 threads, 30 fds, 125 MiB RSS. Settings open adds ~100 MiB and 7 threads; the settings engine is released ~10 s after Ctrl+W (165 MiB left, heap not returned). No polling threads. Resident publishes a Link socket; the tray becomes hosted when private Tools starts. Logs directory `cache/arcade/wheel/logs` is never created in an uneventful session. See WH-2 for the stray Qt cache folder.

## Unconfirmed / needs real desktop
- Owner-reported double cache folders: reproduced, see WH-2.
- Physical hold F8, multi-monitor/Wayland surface positioning, global registration, focus-follow-pointer on a real compositor.
- No real media, power, workspace or window-management actions will be invoked by this isolated audit.
