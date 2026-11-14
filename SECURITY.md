# Security policy

## Reporting

Report a vulnerability privately through GitHub's "Report a vulnerability" (Security tab of this repository), not in a
public issue. Expect an answer within a week. A fix ships as a patch release through the normal release-candidate and
promote path.

## What Arcade Wheel guards, and how

| Surface | Protection | Tested by |
|---|---|---|
| Settings import | A file over 2 MiB is refused unread; shape, text length, ids, action types and counts are checked before anything is stored, and a refused file changes nothing. Exports never contain secrets or device identity. | `tests/tst_ConfigStore.cpp` |
| Command socket | A per-user socket in the runtime directory (owner only); a command line is bounded to 16 KiB. | `tests/tst_WheelController.cpp` |
| Link | Arcade Link's sockets are 0600 in a 0700 folder and now also check the peer's user id. | Link's vendored tests (`ReleasedLink*`) |
| Actions | A command action runs the program you configured, with no shell; other Arcade apps' actions are shown with what they send out before you save them. | `tests/tst_ActionDispatcher.cpp` |
| Install | Integration files are written from a receipt; foreign files are never overwritten or removed. | `tests/tst_AppInstaller.cpp` |
| Crash reports | Written locally under `arcade/wheel/crashes`, never sent. | `tests/tst_CrashReport.cpp` |

## Dependencies

Qt 6 is the only large dependency (LGPL, dynamically linked; see `THIRD_PARTY_NOTICES.md`). GitHub's dependency review
runs on every pull request and Dependabot watches the GitHub Actions used by CI.
