#!/usr/bin/env bash
# Build LayerShellQt against the app's exact Qt kit (it uses Qt's private ABI).
set -euo pipefail
prefix=$(realpath -m "${1:?Pass a dependency installation prefix}")
work=$(realpath -m "${2:?Pass a dependency build directory}")
mkdir -p "$prefix" "$work"
fetch() {
    local name=$1 version=$2 checksum=$3
    local archive="$work/$name.tar.gz"
    curl --fail --location --retry 3 --silent --show-error \
        "https://github.com/KDE/$name/archive/refs/tags/v$version.tar.gz" -o "$archive"
    echo "$checksum  $archive" | sha256sum --check
    tar -xzf "$archive" -C "$work"
}
fetch extra-cmake-modules 6.14.0 02a9e6f37454ce1c4d5c51a9a4a14fbd28c79d0bda063231b8b979510202d0d8
cmake -S "$work/extra-cmake-modules-6.14.0" -B "$work/ecm-build" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_TESTING=OFF
cmake --install "$work/ecm-build"
fetch layer-shell-qt 6.4.6 fe7cc0f7117c4eb111e6f16a8f3e1bbebbbb8d12e7167ed566db6031f577f274
cmake -S "$work/layer-shell-qt-6.4.6" -B "$work/layershell-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix;${QT_ROOT_DIR:?Qt installation is required}" \
    -DKDE_INSTALL_LIBDIR=lib -DKDE_INSTALL_QTPLUGINDIR=lib/plugins -DBUILD_TESTING=OFF
cmake --build "$work/layershell-build" --parallel 2
cmake --install "$work/layershell-build"
