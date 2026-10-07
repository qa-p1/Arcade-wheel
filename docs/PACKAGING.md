# Packaging and releases

The [workflow](../.github/workflows/ci.yml) builds a Release configuration, runs CTest, bundles dependencies, launches the deployed Settings and overlay with build-time Qt paths removed, then uploads installers and SHA-256 checksums. Failed builds or package checks do not upload installable artifacts for that job. Windows additionally tests silent installation and uninstallation.

## Triggers and downloads

- Every successful push to `main` publishes a **stable GitHub Release** with the Windows setup EXE and portable ZIP, Linux AppImage, both macOS DMGs, one `SHA256SUMS.txt`, and `arcade-release.json`. CI artifacts are also retained for 30 days; release downloads do not have that artifact expiry.
- Pull requests targeting `main` run the same checks and packaging. Superseded PR runs are cancelled.
- **Run workflow** starts a manual build; successful runs on `main` also publish a stable release.
- Main releases use `vX.Y.Z+build.N`, where `X.Y.Z` is the version in `CMakeLists.txt` and `N` is the GitHub Actions run number. Build metadata gives each run a unique tag without modifying source files or creating a commit loop. These releases are explicitly marked stable, not prereleases. Installer filenames and the installed application's version retain `X.Y.Z`.
- Explicit `vX.Y.Z` tags still publish a release with that exact tag, after verifying the version matches `CMakeLists.txt` and all platform jobs succeed.
- Pull requests, failed jobs, and cancelled runs never publish releases. A rerun preserves an already published release. Interrupted uploads leave a draft and can be retried. A slower build of an older commit cannot replace a newer release as **Latest**.

Use [Releases](https://github.com/qa-p1/Arcade-wheel/releases) for direct installer downloads. Release notes link to the exact tested commit and CI run and include GitHub-generated change notes. All five packages are checksum-verified before upload, and the complete upload is checked before the draft is published. Alternatively, choose an Actions run and download its platform **Artifacts**; GitHub wraps these in ZIPs.

Each platform artifact includes its own checksum list and manifest. The release job downloads these into separate directories, verifies every original package, then combines the five packages and regenerates one manifest and checksum list. Corrupt, missing or duplicate packages stop publication. The manifest uses the full release version, including `+build.N`, and describes the four installable packages; the portable ZIP is also checksummed. Windows entries use Inno's `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER` flags.

The metadata generator is vendored unchanged from Arcade-link commit `539fa91`; see `scripts/VENDORED`. CI compares it, the Qt module and the conformance vectors with the pinned commit in `src/link/VENDORED.json`. The owner must publish that Arcade-link commit to `qa-p1/Arcade-link` before enabling this workflow on GitHub. A local source comparison needs no network:

```sh
python3 packaging/check-link-vendor.py --source ../../Rust/Arcade-link
python3 -m unittest discover -s tests -p 'test_release.py' -v
```

No repository secrets are needed for unsigned builds. Build jobs have read-only repository permissions; only the release job gets `contents: write`. The built-in `GITHUB_TOKEN` publishes releases and creates tags without triggering another CI run. GitHub Actions must be enabled. Actions and downloaded Linux packaging tools/dependency sources are pinned to revisions or checksums. Qt is fixed at 6.8.3; upgrade Qt and rebuild LayerShellQt together because the latter uses Qt's private API.

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
sha256sum --ignore-missing -c SHA256SUMS.txt
shasum -a 256 -c SHA256SUMS.txt
```

On Windows, use `Get-FileHash .\ArcadeWheel-0.2.0-Windows-x64-Setup.exe -Algorithm SHA256` and compare with its line in `SHA256SUMS.txt`. On macOS, entries for packages you did not download will report missing files; check the downloaded DMG's result.

The dependency deployment follows [Qt's Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html), [Qt's macOS deployment](https://doc.qt.io/qt-6/macos-deployment.html), and the [linuxdeploy Qt plugin](https://github.com/linuxdeploy/linuxdeploy-plugin-qt) documentation.
