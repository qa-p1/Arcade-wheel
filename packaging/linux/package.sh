#!/usr/bin/env bash
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build=$(realpath "${1:-$repo/build}")
output=$(realpath -m "${2:-$repo/dist}")
version=$(sed -nE 's/^project\(ArcadeWheel VERSION ([^ ]+).*/\1/p' "$repo/CMakeLists.txt")
tools_dir="$repo/.cache/packaging-tools"
mkdir -p "$output" "$tools_dir"
stage=$(mktemp -d "$output/.linux-stage-XXXXXX")
trap 'rm -rf -- "$stage"' EXIT

download() {
    local name=$1 url=$2 checksum=$3
    if ! echo "$checksum  $tools_dir/$name" | sha256sum --check --status; then
        curl --fail --location --retry 3 --silent --show-error "$url" -o "$tools_dir/$name"
        echo "$checksum  $tools_dir/$name" | sha256sum --check
    fi
    chmod +x "$tools_dir/$name"
}
download linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
download linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
    15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724

export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
qt_plugins=$("$QMAKE" -query QT_INSTALL_PLUGINS)
layer_plugin=${LAYERSHELL_PLUGIN:-$qt_plugins/wayland-shell-integration/liblayer-shell.so}
if [[ ! -f "$layer_plugin" ]]; then
    echo "LayerShellQt's liblayer-shell.so is required. Set LAYERSHELL_PLUGIN to its path." >&2
    exit 1
fi
if ! ldd "$build/arcade-wheel" | grep -q libLayerShellQtInterface; then
    echo 'Refusing to package a development build without LayerShellQt.' >&2
    exit 1
fi

appdir="$stage/AppDir"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr
mkdir -p "$appdir/usr/plugins/wayland-shell-integration"
cp "$layer_plugin" "$appdir/usr/plugins/wayland-shell-integration/"
# These are dlopened by Qt, so ELF dependency scanning alone cannot find them.
for category in wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client; do
    if [[ -d "$qt_plugins/$category" ]]; then
        mkdir -p "$appdir/usr/plugins/$category"
        cp -a "$qt_plugins/$category/." "$appdir/usr/plugins/$category/"
    fi
done
extra_platforms=libqoffscreen.so
for plugin in "$qt_plugins"/platforms/libqwayland*.so; do
    [[ -f "$plugin" ]] || continue
    extra_platforms+=";$(basename "$plugin")"
done
if [[ "$extra_platforms" != *wayland* ]]; then
    echo 'Qt Wayland platform plugins are missing.' >&2
    exit 1
fi
export EXTRA_PLATFORM_PLUGINS="$extra_platforms"
export QML_SOURCES_PATHS="$repo/qml"
export APPIMAGE_EXTRACT_AND_RUN=1
export OUTPUT="$output/ArcadeWheel-$version-Linux-x86_64.AppImage"
# Libraries/plugins copied above are scanned before the Qt and AppImage plugins.
"$tools_dir/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
    --desktop-file "$repo/packaging/linux/com.arcadewheel.ArcadeWheel.desktop" \
    --icon-file "$repo/assets/arcade-wheel.svg" --plugin qt --output appimage
test -s "$OUTPUT"
python3 "$repo/packaging/smoke-test.py" "$OUTPUT"
python3 "$repo/scripts/arcade-release.py" --id arcade.wheel --version "$version" \
    --channel stable --windows-installer inno \
    --notes "https://github.com/qa-p1/Arcade-wheel/releases/tag/v$version" "$output"
printf 'Created %s\n' "$OUTPUT"
