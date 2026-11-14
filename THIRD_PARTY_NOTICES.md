# Third-party notices

Arcade Wheel is MIT-licensed (`LICENSE`). It bundles the following; Settings → About opens this file.

## Qt 6 (LGPL-3.0)

Wheel uses Qt 6.8 (Core, Gui, Qml, Quick, QuickControls2, Svg, Widgets, Network, DBus on Linux). Qt is licensed under
the GNU Lesser General Public License version 3 and is linked **dynamically**: the AppImage and the Windows and macOS
packages carry Qt as separate shared libraries that you may replace.

Relinking and replacing Qt:

- Qt's source and licence texts: https://www.qt.io/download-open-source and https://code.qt.io/.
- Linux AppImage: run `Arcade-Wheel.AppImage --appimage-extract`; the libraries are in `squashfs-root/usr/lib` and the
  plugins in `squashfs-root/usr/plugins`. Replace them with a build of Qt 6.8 from the same source, then repack with
  `appimagetool`, or run `squashfs-root/AppRun` directly.
- Wheel's own source (this repository) builds against any Qt 6.8: `cmake -S . -B build -DCMAKE_PREFIX_PATH=<your Qt>`.

The LGPL-3.0 text is at https://www.gnu.org/licenses/lgpl-3.0.txt and in the Qt distribution.

## KDE LayerShellQt (LGPL-2.1-or-later)

Linux only: Wayland layer-shell support for the overlay, built from source in CI (`packaging/linux/build-dependencies.sh`)
and shipped as a shared plugin that you may replace in the same way.

## Inter (SIL Open Font License 1.1)

The Arcade design kit's typeface, `src/link/design/fonts/InterVariable.ttf`, licence in `src/link/design/fonts/OFL.txt`.

## Arcade Link (MIT OR Apache-2.0)

The Qt module and design kit vendored in `src/link`, from Arcade Link v1.0.7 (see `src/link/VENDORED.json`).

## Libraries pulled in by the Linux AppImage

Glib/GIO (LGPL-2.1+), libxkbcommon (MIT), libwayland-client (MIT), Mesa/libEGL (MIT) and their dependencies from the
Ubuntu build image. Their licences are in `/usr/share/doc/<package>/copyright` of that image.
