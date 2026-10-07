#!/usr/bin/env python3
"""Publish the complete, verified package set from a successful CI run."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "scripts/arcade-release.py"
spec = importlib.util.spec_from_file_location("arcade_release", GENERATOR)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def project_version(path):
    match = re.search(r"project\(ArcadeWheel VERSION (\d+\.\d+\.\d+)\s", path.read_text())
    if not match:
        raise ValueError("Cannot read the application version from CMakeLists.txt")
    return match.group(1)


def release_tag(version, ref, run_number):
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Expected a major.minor.patch application version")
    if ref == "refs/heads/main":
        if run_number < 1:
            raise ValueError("The CI run number must be positive")
        # SemVer build metadata distinguishes builds without changing the app's
        # base version or creating a prerelease version.
        return f"v{version}+build.{run_number}"
    if ref == f"refs/tags/v{version}":
        return f"v{version}"
    raise ValueError("Only main and a version tag matching CMakeLists.txt may publish")


def package_names(version):
    prefix = f"ArcadeWheel-{version}-"
    return [prefix + suffix for suffix in (
        "Windows-x64-Setup.exe", "Windows-x64.zip", "Linux-x86_64.AppImage",
        "macOS-arm64.dmg", "macOS-x86_64.dmg")]


def verified_bundle(directory, version, release_version=None, complete=False):
    required = set(package_names(version))
    actual = {path.name for path in directory.iterdir()}
    filenames = sorted(required if complete else actual & required)
    expected = set(filenames) | {"SHA256SUMS.txt", "arcade-release.json"}
    if actual != expected:
        raise ValueError(f"Incomplete or unexpected release assets: missing={sorted(expected - actual)}, "
                         f"unexpected={sorted(actual - expected)}")
    if not filenames:
        raise ValueError("No packages in release bundle")
    for name in filenames:
        package = directory / name
        if not package.is_file() or package.stat().st_size == 0:
            raise ValueError(f"Empty or invalid package: {name}")
    manifest = json.loads((directory / "arcade-release.json").read_text(encoding="utf-8-sig"))
    wanted, sums = generator.build("arcade.wheel", release_version or version, "stable",
                                   manifest.get("notes", ""), directory, "inno")
    if (directory / "SHA256SUMS.txt").read_text(encoding="utf-8-sig") != sums:
        raise ValueError("Checksum mismatch in SHA256SUMS.txt")
    if manifest != wanted:
        raise ValueError("Release manifest mismatch (identity, version, platforms, hashes or installer flags)")
    return [directory / name for name in sorted(expected)]


def verified_assets(directory, version, release_version=None):
    return verified_bundle(directory, version, release_version, complete=True)


def collect_assets(bundles, directory, version):
    """Verify each CI artifact before copying its packages into one release set."""
    packages = {}
    for bundle in sorted(bundles.iterdir()):
        if not bundle.is_dir():
            raise ValueError(f"Unexpected artifact: {bundle.name}")
        for path in verified_bundle(bundle, version):
            if path.name in generator.OUTPUTS:
                continue
            if path.name in packages:
                raise ValueError(f"Duplicate package: {path.name}")
            packages[path.name] = path
    if set(packages) != set(package_names(version)):
        raise ValueError("Incomplete platform artifacts; all five packages are required")
    if directory.exists() and any(directory.iterdir()):
        raise ValueError("The combined release directory must be empty")
    directory.mkdir(parents=True, exist_ok=True)
    for name, path in packages.items():
        shutil.copyfile(path, directory / name)


def write_metadata(directory, version, notes):
    subprocess.run([sys.executable, str(GENERATOR), "--id", "arcade.wheel", "--version", version,
                    "--channel", "stable", "--notes", notes, "--windows-installer", "inno",
                    str(directory)], check=True)


def gh(*arguments):
    return subprocess.run(["gh", *arguments], check=True, capture_output=True, text=True).stdout.strip()


def api(path, allow_missing=False):
    result = subprocess.run(["gh", "api", path], capture_output=True, text=True)
    if result.returncode:
        if allow_missing and "HTTP 404" in result.stderr:
            return None
        raise RuntimeError(result.stderr.strip() or "GitHub API request failed")
    return json.loads(result.stdout)


def release_by_tag(repository, tag):
    result = api(f"repos/{repository}/releases/tags/{tag}", allow_missing=True)
    if result:
        return result
    # GitHub's tag endpoint returns only published releases. Authenticated
    # release listings include drafts, whose tags may not exist yet.
    page = 1
    while True:
        releases = api(f"repos/{repository}/releases?per_page=100&page={page}")
        for candidate in releases:
            if candidate["tag_name"] == tag:
                return candidate
        if len(releases) < 100:
            return None
        page += 1


def publish(repository, version, tag, commit, assets, run_url, run_number):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Release target must be the full tested commit SHA")
    existing = release_by_tag(repository, tag)
    # Also reject a pre-existing tag pointing elsewhere, even without a release.
    comparison = api(f"repos/{repository}/compare/{tag}...{commit}", allow_missing=True)
    if comparison and comparison["base_commit"]["sha"] != commit:
        raise ValueError(f"Tag {tag} already points to a different commit")
    if existing and not comparison and existing["target_commitish"] != commit:
        raise ValueError(f"Draft {tag} targets a different commit")
    if existing and not existing["draft"]:
        expected = {asset.name for asset in assets}
        if existing["prerelease"] or {asset["name"] for asset in existing["assets"]} != expected:
            raise ValueError(f"Published release {tag} does not match this package set; refusing to overwrite it")
        print(f"Already published; preserving its assets: {existing['html_url']}")
        return existing["html_url"]

    title = f"Arcade Wheel {version}"
    if "+build." in tag:
        title += f" (build {run_number})"
    notes = (
        f"Built and tested from commit `{commit}`. [Successful CI run]({run_url}).\n\n"
        "Download the package for your system below:\n\n"
        "- **Windows x64:** run `Windows-x64-Setup.exe`, or extract the entire portable ZIP.\n"
        "- **Linux x86_64:** make the AppImage executable and launch it.\n"
        "- **macOS 13+:** open the DMG matching your processor and drag the app to Applications.\n\n"
        "Qt and the application runtime are bundled. Verify downloads with `SHA256SUMS.txt`; "
        "`arcade-release.json` supplies Arcade Tools with package hashes and installer details.\n\n"
        "Windows packages are unsigned; macOS packages are ad-hoc signed, without notarization.\n"
    )
    with tempfile.TemporaryDirectory(prefix="arcade-release-") as temporary:
        notes_file = Path(temporary) / "notes.md"
        notes_file.write_text(notes)
        if not existing:
            gh("release", "create", tag, "--repo", repository, "--target", commit,
               "--title", title, "--notes-file", str(notes_file), "--generate-notes", "--draft")
        # A failed upload leaves an unpublished draft which a rerun can resume.
        gh("release", "upload", tag, *map(str, assets), "--repo", repository, "--clobber")
    uploaded = release_by_tag(repository, tag)
    if uploaded is None:
        raise ValueError("Could not find the uploaded draft release")
    sizes = {asset.name: asset.stat().st_size for asset in assets}
    if {asset["name"]: asset["size"] for asset in uploaded["assets"]} != sizes:
        raise ValueError("Uploaded assets are incomplete; keeping the release as a draft")

    # A slower, older CI run must not replace a newer release as Latest.
    latest = api(f"repos/{repository}/releases/latest", allow_missing=True)
    make_latest = True
    if latest:
        ordering = api(f"repos/{repository}/compare/{latest['tag_name']}...{commit}")
        make_latest = ordering["status"] in ("ahead", "identical")
    gh("release", "edit", tag, "--repo", repository, "--draft=false", "--prerelease=false",
       "--latest" if make_latest else "--latest=false")
    published = api(f"repos/{repository}/releases/tags/{tag}")
    if published["draft"] or published["prerelease"]:
        raise ValueError("GitHub did not publish the release as stable")
    print(published["html_url"])
    return published["html_url"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--bundles", type=Path, help="Separate downloaded CI artifact directories")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--run-number", type=int, required=True)
    parser.add_argument("--run-url", required=True)
    args = parser.parse_args()
    version = project_version(Path(__file__).resolve().parents[1] / "CMakeLists.txt")
    tag = release_tag(version, args.ref, args.run_number)
    repository = os.environ["GH_REPO"]
    if args.bundles:
        collect_assets(args.bundles, args.artifacts, version)
        write_metadata(args.artifacts, tag[1:], f"https://github.com/{repository}/releases/tag/{tag}")
    assets = verified_assets(args.artifacts, version, tag[1:])
    url = publish(repository, version, tag, args.commit,
                  assets, args.run_url, args.run_number)
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(summary, "a") as stream:
            stream.write(f"Published [{tag}]({url}) with all five packages and checksums.\n")


if __name__ == "__main__":
    main()
