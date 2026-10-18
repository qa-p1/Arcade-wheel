# Vendored dependencies

Wheel is MIT licensed. Arcade Link declares `MIT OR Apache-2.0` in its release
Cargo metadata and the Qt bundle's provenance. The released repository does
not carry standalone license-text files; the declared license and source pin
are recorded here and in the generated provenance.

| Files | Source and pin | Purpose |
|---|---|---|
| `src/link/qt/`, `spec/`, `fixtures/`, `assets/strings/`, bundled spec/Cargo metadata/tools | Arcade Link `85bdb0e9769cc724c0edd83c6f214b70fba33f42` (after v0.3.0); `src/link/VENDORED.json` owns every bundled SHA-256 | Qt protocol, install, accelerator, shortcuts, tray and conformance tests |
| `scripts/arcade-release.py`, `scripts/validate_shortcuts.py` | Same commit, hashes in `scripts/VENDORED` | Release metadata and canonical shortcut documentation |
| `packaging/windows/arcade.iss`, `arcade.nsh`, `arcade-integration.ps1` | Same commit, hashes in `packaging/windows/VENDORED` | Shared per-user installer integration; Wheel uses Inno |

Source: [Arcade Link 85bdb0e](https://github.com/qa-p1/Arcade-link/tree/85bdb0e9769cc724c0edd83c6f214b70fba33f42).
The compatibility header `src/link/ArcadeLink.h` and Wheel-specific adapter,
installer UI/CLI, tests and checks are maintained by Wheel. Vendored sources
are byte-for-byte release copies. `packaging/check-link-vendor.py --source`
checks the whole bundle against the pin, and `check-vendored-tools.py --source`
checks the shared copies. Qt and LayerShellQt are dynamically linked platform
dependencies, packaged by the existing deployment scripts.
