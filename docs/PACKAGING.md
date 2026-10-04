# Packaging and releases

The [workflow](../.github/workflows/ci.yml) builds a Release configuration, runs CTest, bundles dependencies, launches the deployed Settings and overlay with build-time Qt paths removed, then uploads installers and SHA-256 checksums. Failed builds or package checks do not upload installable artifacts for that job. Windows additionally tests silent installation and uninstallation.

## Triggers and downloads

- Every push to `main` produces Windows x64, Linux x86_64, macOS arm64, and macOS x86_64 artifacts, retained for 30 days.
- Pull requests targeting `main` run the same checks and packaging. Superseded PR runs are cancelled.
- **Run workflow** starts a manual build.
- A `vX.Y.Z` tag must match the version in `CMakeLists.txt`. When all platform jobs succeed, the workflow publishes a GitHub Release containing the packages and checksums. Ordinary main commits do not create public releases.

Choose an Actions run for the desired commit, then download `ArcadeWheel-Windows-x64`, `ArcadeWheel-Linux-x86_64`, `ArcadeWheel-macOS-arm64`, or `ArcadeWheel-macOS-x86_64` from **Artifacts**. GitHub wraps artifacts in a ZIP; extract it to obtain the installer. Public tagged releases contain the files directly.

No repository secrets are needed for unsigned builds. Build jobs have read-only repository permissions; only the tagged-release job gets `contents: write`. GitHub Actions must be enabled. Actions and downloaded Linux packaging tools/dependency sources are pinned to revisions or checksums. Qt is fixed at 6.8.3; upgrade Qt and rebuild LayerShellQt together because the latter uses Qt's private API.

## What is bundled

- **Windows:** the Qt runtime, discovered QML imports, Qt plugins, and app-local Microsoft Visual C++ runtime DLLs. Inno Setup installs under `%LOCALAPPDATA%\Programs\Arcade Wheel`, adds a Start menu shortcut and an optional desktop shortcut, and provides an uninstaller. No administrator rights or separate runtime setup are needed. The portable ZIP contains the same application files.
- **Linux:** Qt/QML, Wayland platform and integration plugins, LayerShellQt and its shell plugin, plus dependencies discovered by linuxdeploy. The official image is built on Ubuntu 22.04 to establish the compatibility baseline. System graphics drivers, glibc, the compositor, session D-Bus, and a suitable GlobalShortcuts portal remain host requirements.
- **macOS:** a relocatable `.app`, Qt frameworks, QML and native plugins deployed by `macdeployqt`, inside a compressed DMG with an Applications shortcut. Native builds are made separately for Intel and Apple Silicon. The minimum macOS version is 13.

The app generates defaults on first launch, discovers installed applications, and stores settings in the user's configuration directory. Installation and updates do not replace saved settings. Login startup is opt-in. AppImage login entries reference the permanent `.AppImage` file, never its temporary mount; macOS login registration requires the app to be installed in Applications.

## Local packaging

Build and run CTest first. Each script writes to `dist/` by default, creates checksums, and fails if the packaged UI cannot load. Python 3 is required for the smoke test.

### Windows

Use an x64 Visual Studio 2022 developer prompt with the matching Qt `bin` directory on PATH and Inno Setup 6 installed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\packaging\windows\package.ps1 -BuildDir build -Configuration Release
```

### Linux

Use Qt with Wayland support and LayerShellQt built against that same Qt. For the exact CI dependency setup, see `packaging/linux/build-dependencies.sh` and the workflow's apt packages. `LAYERSHELL_PLUGIN` may point to `liblayer-shell.so` if it is outside Qt's normal plugins directory. Set `QMAKE` to the matching qmake binary and, when needed, scope dependency `LD_LIBRARY_PATH` to the packaging command.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
bash packaging/linux/package.sh build dist
```

Packaging downloads checksum-verified linuxdeploy tools into `.cache/packaging-tools`. It refuses a build without LayerShellQt. On a newer development distribution whose ELF format the bundled strip tool cannot read, use `NO_STRIP=1 bash packaging/linux/package.sh build dist`. Such a local AppImage has the development machine's compatibility baseline; use the CI artifact for distribution.

### macOS

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
cmake --build build --parallel
ctest --test-dir build --output-on-failure
bash packaging/macos/package.sh build dist
```

The script uses `macdeployqt`, `sips`, `iconutil`, `codesign`, and `hdiutil`. The app bundle is ad-hoc signed and verified before packaging. Test shortcuts, app launch/focus, Spaces/fullscreen behavior, and login startup on a real Mac before a public release; offscreen CI only checks UI loading and automated tests.

## Signing and verification

Windows code signing and Apple Developer ID signing/notarization need the publisher's certificates and account credentials. They are not configured by this workflow. Current artifacts are unsigned on Windows and ad-hoc signed on macOS, so they are not equivalent to trusted, notarized store releases. Add a signing step before checksumming/uploading when those credentials are available.

Verify a downloaded file against its accompanying checksum:

```sh
sha256sum -c ArcadeWheel-0.2.0-Linux-x86_64.AppImage.sha256
shasum -a 256 -c ArcadeWheel-0.2.0-macOS-arm64.dmg.sha256
```

On Windows, use `Get-FileHash .\ArcadeWheel-0.2.0-Windows-x64-Setup.exe -Algorithm SHA256` and compare with the `.sha256` file.

The dependency deployment follows [Qt's Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html), [Qt's macOS deployment](https://doc.qt.io/qt-6/macos-deployment.html), and the [linuxdeploy Qt plugin](https://github.com/linuxdeploy/linuxdeploy-plugin-qt) documentation.
