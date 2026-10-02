# Changelog

All notable changes to Arcade Wheel are documented here.

## Unreleased

### Added

- **Single-click center gesture.** A single left click in the wheel's hub can now run its own group of up to 16 apps or actions, configured under **Settings → Center gestures** like double-click, triple-click, and long press. When double- or triple-click is also enabled, a single click waits for the click interval before running; releasing the trigger runs it immediately.

### Unchanged

- With no single-click group, a single center click still closes the wheel. Holding the trigger and releasing it in the center without clicking still just closes the wheel.
- Existing configurations get a disabled single-click group automatically; no migration is needed.

### Fixed

- **Windows:** the pointer now moves to the wheel center when the wheel opens, matching Hyprland. Before, the wheel was drawn at the screen center while the pointer stayed put, so the first small movement could select a slot and releasing would launch it instead of cancelling.

### Documentation

- Rewrote the README for the open-source release: features, requirements (Qt 6.8+), build, usage, CLI reference, configuration, platform notes, layout, and contributing.
- Updated the architecture notes for schema version 3, center gesture recognition, and how the wheel is positioned.
- Added single-click checks to the manual release checklist.
