# Manual release checks

Automated tests (`ctest`, 9 suites) cover config migration and round-trips, angular selection, the dead zone, deck scrolling, invalid action handling, the trigger lifecycle, center gesture recognition, the Arcade Link conformance vectors, the Link provider and `wheel.*` actions, shortcut validation for the Windows and macOS backends and login entries on Linux. Arcade Link's isolated runner adds the cross-app checks (see [Arcade Link](ARCADE_LINK.md)). Before a release, exercise the actual desktop path on the target OS:

1. Set a shortcut, approve the desktop portal if prompted, then press, hold, move, and release. Verify execution starts as the overlay disappears.
2. Return to the center before release and verify nothing executes. Try rapid repeated invocations and a configured hold threshold.
3. Scroll forward and back through at least three decks, including wrap disabled and reverse scroll. Verify one physical wheel notch moves one deck.
4. Open near all screen edges and corners. Repeat with two monitors, a negative-coordinate monitor, different resolutions and scaling, and moving the pointer between screens.
5. Test over normal, maximized, and fullscreen windows. Verify the prior app regains pointer/keyboard input after release. Check suspend/resume and login startup.
6. Launch an installed app, then focus an existing instance. Remove an app or provider and verify its saved slot remains visible but unavailable in Settings.
7. Edit a slot and appearance while the core remains running. Verify the next invocation uses the change. Import/export and restart, then verify IDs and order persist.
8. Exercise a command that fails, a missing file, and an unavailable provider. Verify the overlay always closes and a useful message appears in Settings or the tray.
9. In Center gestures, assign different app groups to single-click, double-click, triple-click, and long press. Hold the trigger, perform each gesture in the hub, and verify only its group runs, and only once. Release the trigger after execution and confirm nothing launches again. Test releasing after one or two clicks before the click interval ends, moving out of the center during a long press, right-click cancellation, disabling a group, and restarting/importing/exporting the configuration. A missing action must not prevent the rest of the group from running.
10. Disable the single-click group. Verify a single center click closes the wheel without running anything. Holding the trigger and releasing in the center without clicking must close the wheel whether or not any gesture is configured.

On Windows, also check Start Menu shortcut discovery, DPI scaling, UAC/elevated windows, and media keys. On Hyprland, verify the GlobalShortcuts portal and LayerShellQt are present and that no X11 session is needed. On macOS, install from the DMG into Applications, then check shortcut registration (including Fn+F8 on keyboards where F8 is reserved), the overlay over fullscreen apps and on other Spaces, app discovery in `/Applications` and `~/Applications`, screenshot and sleep actions, and login startup approval in **System Settings → General → Login Items**.
