#!/usr/bin/env bash
# Release gate (gates.yml): a published DMG of a release candidate on a clean macOS runner. Copies the app out of the
# read-only image the way a user drags it, runs its smoke test and the native install checks. CI only.
#   packaging/macos/gate-dmg.sh rc/ArcadeWheel-1.0.0-macOS-arm64.dmg
set -euo pipefail
[[ "${GITHUB_ACTIONS:-}" == true && "$(uname -s)" == Darwin ]] || { echo 'Run this only on the macOS GitHub Actions runner.' >&2; exit 1; }
dmg=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
stage=$(mktemp -d "${RUNNER_TEMP:?}/wheel-gate.XXXXXX")
mount="$stage/mounted"
mkdir -p "$mount" "$stage/copy"
trap 'hdiutil detach "$mount" >/dev/null 2>&1 || true; rm -rf -- "$stage"' EXIT
hdiutil attach -readonly -nobrowse -mountpoint "$mount" "$dmg"
ditto "$mount/Arcade Wheel.app" "$stage/copy/Arcade Wheel.app"
hdiutil detach "$mount"
python3 packaging/smoke-test.py "$stage/copy/Arcade Wheel.app/Contents/MacOS/arcade-wheel"
CI=true python3 packaging/check-native-install.py macos "$stage/copy/Arcade Wheel.app"
echo "Published DMG $(basename "$dmg"): copy, smoke test and native checks passed."
