#!/usr/bin/env bash
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build=$(cd "${1:-$repo/build}" && pwd)
mkdir -p "${2:-$repo/dist}"
output=$(cd "${2:-$repo/dist}" && pwd)
architecture=${3:-$(uname -m)}
case "$architecture" in arm64|x86_64) ;; *) echo 'Expected arm64 or x86_64' >&2; exit 1 ;; esac
version=$(sed -nE 's/^project\(ArcadeWheel VERSION ([^ ]+).*/\1/p' "$repo/CMakeLists.txt")
stage=$(mktemp -d "$output/.macos-stage-XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
app="$stage/Arcade Wheel.app"
ditto "$build/arcade-wheel.app" "$app"
mkdir -p "$app/Contents/Resources"
cp "$repo/LICENSE" "$repo/README.md" "$app/Contents/Resources/"

iconset="$stage/arcade-wheel.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$repo/assets/arcade-wheel.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    doubled=$((size * 2))
    sips -z "$doubled" "$doubled" "$repo/assets/arcade-wheel.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/arcade-wheel.icns"
rm -r "$iconset"
# macdeployqt normally deploys only Cocoa. Include the offscreen plugin and
# relocate its framework references too, so the packaged UI can be tested.
qt_plugins=$(qmake -query QT_INSTALL_PLUGINS)
mkdir -p "$app/Contents/PlugIns/platforms"
offscreen="$app/Contents/PlugIns/platforms/libqoffscreen.dylib"
cp "$qt_plugins/platforms/libqoffscreen.dylib" "$offscreen"
macdeployqt "$app" "-qmldir=$repo/qml" "-executable=$offscreen" -always-overwrite -verbose=1
# Ad-hoc signatures allow native Apple Silicon execution. Public distribution
# still needs a Developer ID certificate and notarization (documented in README).
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
python3 "$repo/packaging/smoke-test.py" "$app/Contents/MacOS/arcade-wheel"
ln -s /Applications "$stage/Applications"
filename="ArcadeWheel-$version-macOS-$architecture.dmg"
hdiutil create -volname 'Arcade Wheel' -srcfolder "$stage" -ov -format UDZO "$output/$filename"
python3 "$repo/scripts/arcade-release.py" --id arcade.wheel --version "$version" \
    --channel stable --windows-installer inno \
    --notes "https://github.com/qa-p1/Arcade-wheel/releases/tag/v$version" "$output"
printf 'Created %s\n' "$output/$filename"
