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
fetch extra-cmake-modules 6.10.0 96970136cf38c810f4ef90a33ad4ef9c8977956e1a6a02a179b7abf3a8967b34
cmake -S "$work/extra-cmake-modules-6.10.0" -B "$work/ecm-build" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_TESTING=OFF
cmake --install "$work/ecm-build"
fetch layer-shell-qt 6.2.5 a9b1aae2f4a945b13f8bf399f6c41dad4c94b9471d07b1523e0a2e878e2bcf32
cmake -S "$work/layer-shell-qt-6.2.5" -B "$work/layershell-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix;${QT_ROOT_DIR:?Qt installation is required}" \
    -DKDE_INSTALL_LIBDIR=lib -DKDE_INSTALL_QTPLUGINDIR=lib/plugins -DBUILD_TESTING=OFF
cmake --build "$work/layershell-build" --parallel 2
cmake --install "$work/layershell-build"
