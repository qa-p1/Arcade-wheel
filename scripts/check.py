#!/usr/bin/env python3
"""What CI checks, in one command (STANDARD §10).

    scripts/check [--build DIR] [--config Release] [--link-source DIR] [--no-tests]

Builds DIR (default build/; configured with compiler warnings as errors when
it is not configured yet), then checks the C++ format (clang-format 18, on
Linux), the QML (qmllint at zero warnings), the vendored Link files (against a
Link checkout at the pinned commit with --link-source), and on Linux the
shortcut data, the documentation links and the release tools; then runs every
CTest test. Point CMAKE_PREFIX_PATH at Qt 6.8 (and LayerShellQt on Linux) first.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(*command, env=None, stdout=None):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True, env=env, stdout=stdout)


def check_format():
    tool = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format-18") or shutil.which("clang-format")
    if not tool:
        raise SystemExit("clang-format 18 is required for the format check (or set CLANG_FORMAT).")
    version = subprocess.run([tool, "--version"], capture_output=True, text=True, check=True).stdout
    if " 18." not in version:
        raise SystemExit(f"The format check needs clang-format 18, as CI uses; {tool} is: {version.strip()}")
    # The vendored Link module keeps Link's own style.
    files = [name for name in subprocess.run(
        ["git", "ls-files", "src/*.cpp", "src/*.h", "src/*.mm", "tests/*.cpp", "tests/*.h"],
        cwd=ROOT, capture_output=True, text=True, check=True).stdout.split() if not name.startswith("src/link/qt/")]
    run(tool, "--dry-run", "--Werror", *files)


def check_shortcuts():
    data, manifest = "docs/user/shortcuts.json", "tests/fixtures/wheel-shortcut-manifest.json"
    run(sys.executable, "scripts/validate_shortcuts.py", data, "--manifest", manifest)
    with tempfile.NamedTemporaryFile("w+", suffix=".md", delete=False) as generated:
        run(sys.executable, "scripts/validate_shortcuts.py", data, "--manifest", manifest, "--markdown", stdout=generated)
    try:
        if Path(generated.name).read_bytes() != (ROOT / "docs/user/shortcuts.md").read_bytes():
            raise SystemExit("docs/user/shortcuts.md is out of date; regenerate it with validate_shortcuts.py --markdown.")
    finally:
        os.unlink(generated.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="build", help="the CMake build directory (default: build)")
    parser.add_argument("--config", default="Release", help="the configuration of a multi-config build")
    parser.add_argument("--link-source", help="Arcade Link checkout at the pinned commit, to compare vendored files")
    parser.add_argument("--no-tests", action="store_true", help="build and check, but skip CTest")
    args = parser.parse_args()
    build = (ROOT / args.build).resolve()
    if not (build / "CMakeCache.txt").exists():
        run("cmake", "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_TESTING=ON", "-DWHEEL_WARNINGS_AS_ERRORS=ON")
    run("cmake", "--build", build, "--config", args.config, "--parallel")
    linux = sys.platform.startswith("linux")
    if linux:
        check_format()
    run("cmake", "--build", build, "--config", args.config, "--target", "qmllint_gate")
    source = ["--source", args.link_source] if args.link_source else []
    run(sys.executable, "packaging/check-link-vendor.py", *source)
    run(sys.executable, "packaging/check-vendored-tools.py", *source)
    if linux:
        check_shortcuts()
        run(sys.executable, "packaging/check-docs.py")
        run(sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_release.py")
    if not args.no_tests:
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
        run("ctest", "--test-dir", build, "-C", args.config, "--output-on-failure", "--timeout", "120", env=env)
    print("All checks passed.")


if __name__ == "__main__":
    main()
