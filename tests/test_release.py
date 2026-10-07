"""Release publication must never expose partial, unverified packages."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "publish_release", Path(__file__).resolve().parents[1] / "packaging/publish-release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        for suffix in ("Windows-x64-Setup.exe", "Windows-x64.zip", "Linux-x86_64.AppImage",
                       "macOS-arm64.dmg", "macOS-x86_64.dmg"):
            name = f"ArcadeWheel-0.2.0-{suffix}"
            data = name.encode()
            (self.directory / name).write_bytes(data)
        release.write_metadata(self.directory, "0.2.0", "https://example.com/release")
        self.commit = "a" * 40
        self.tag = "v0.2.0+build.15"

    def publish(self):
        assets = release.verified_assets(self.directory, "0.2.0")
        return release.publish("owner/repo", "0.2.0", self.tag, self.commit,
                               assets, "https://example.com/ci/15", 15)

    def uploaded(self):
        return {"draft": True, "prerelease": False, "assets": [
            {"name": path.name, "size": path.stat().st_size} for path in self.directory.iterdir()]}

    def test_tags_are_unique_and_reruns_are_stable(self):
        self.assertEqual(release.release_tag("0.2.0", "refs/heads/main", 15), self.tag)
        self.assertNotEqual(release.release_tag("0.2.0", "refs/heads/main", 16), self.tag)
        self.assertEqual(release.release_tag("0.2.0", "refs/tags/v0.2.0", 15), "v0.2.0")
        for ref in ("refs/pull/3/merge", "refs/heads/feature", "refs/tags/v9.9.9"):
            with self.assertRaises(ValueError):
                release.release_tag("0.2.0", ref, 15)

    def test_all_five_packages_and_checksums_are_required(self):
        self.assertEqual(len(release.verified_assets(self.directory, "0.2.0")), 7)
        (self.directory / "ArcadeWheel-0.2.0-macOS-arm64.dmg").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            release.verified_assets(self.directory, "0.2.0")

    def test_corrupt_packages_are_rejected(self):
        (self.directory / "ArcadeWheel-0.2.0-Windows-x64-Setup.exe").write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            release.verified_assets(self.directory, "0.2.0")

    def bundles(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        for platform in ("Windows-x64", "Linux-x86_64", "macOS-arm64", "macOS-x86_64"):
            bundle = root / platform
            bundle.mkdir()
            for path in self.directory.glob(f"ArcadeWheel-0.2.0-{platform}*"):
                shutil.copyfile(path, bundle / path.name)
            release.write_metadata(bundle, "0.2.0", "https://example.com/release")
        return root

    def test_manifest_and_checksums_cover_installer_and_portable_downloads(self):
        release.write_metadata(self.directory, self.tag[1:], "https://example.com/release")
        release.verified_assets(self.directory, "0.2.0", self.tag[1:])
        manifest = json.loads((self.directory / "arcade-release.json").read_text())
        self.assertEqual(manifest["version"], "0.2.0+build.15")
        self.assertEqual(manifest["id"], "arcade.wheel")
        self.assertEqual(manifest["linkProtocol"], [1])
        self.assertEqual(len(manifest["assets"]), 4)
        windows = next(asset for asset in manifest["assets"] if asset["os"] == "windows")
        self.assertEqual(windows["kind"], "inno")
        self.assertEqual(windows["silent"], ["/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER"])
        self.assertEqual({(a["os"], a["arch"]) for a in manifest["assets"]},
                         {("windows", "x64"), ("linux", "x64"), ("macos", "arm64"), ("macos", "x64")})
        lines = (self.directory / "SHA256SUMS.txt").read_text().splitlines()
        self.assertEqual(len(lines), 5)
        for line in lines:
            digest, name = line.split("  ", 1)
            self.assertEqual(digest, hashlib.sha256((self.directory / name).read_bytes()).hexdigest())
        self.assertTrue(any(line.endswith("Windows-x64.zip") for line in lines))
        self.assertFalse(any(asset["file"].endswith(".zip") for asset in manifest["assets"]))

    def test_incorrect_identity_and_installer_flags_are_rejected(self):
        path = self.directory / "arcade-release.json"
        original = json.loads(path.read_text())
        for change in ("identity", "installer"):
            manifest = json.loads(json.dumps(original))
            if change == "identity":
                manifest["id"] = "arcade.look"
            else:
                windows = next(asset for asset in manifest["assets"] if asset["os"] == "windows")
                windows["silent"] = ["/S"]
            path.write_text(json.dumps(manifest))
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, "manifest mismatch"):
                release.verified_assets(self.directory, "0.2.0")

    def test_legacy_sidecars_are_rejected(self):
        (self.directory / "old.AppImage.sha256").write_text("obsolete")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            release.verified_assets(self.directory, "0.2.0")

    def test_bundle_merge_verifies_then_generates_one_complete_manifest(self):
        bundles = self.bundles()
        output = bundles.parent / (bundles.name + "-combined")
        self.addCleanup(shutil.rmtree, output, ignore_errors=True)
        release.collect_assets(bundles, output, "0.2.0")
        release.write_metadata(output, self.tag[1:], "https://example.com/release")
        self.assertEqual(len(release.verified_assets(output, "0.2.0", self.tag[1:])), 7)

    def test_corrupt_bundle_is_rejected_before_rehashing_or_copying(self):
        bundles = self.bundles()
        next((bundles / "Windows-x64").glob("*.exe")).write_bytes(b"corrupt")
        output = bundles / "combined"
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            release.collect_assets(bundles, output, "0.2.0")
        self.assertFalse(output.exists())

    def test_missing_and_duplicate_bundles_are_rejected(self):
        bundles = self.bundles()
        arm = bundles / "macOS-arm64"
        parked = bundles.parent / (bundles.name + "-parked")
        self.addCleanup(shutil.rmtree, parked, ignore_errors=True)
        arm.rename(parked)
        with self.assertRaisesRegex(ValueError, "all five packages"):
            release.collect_assets(bundles, bundles / "combined", "0.2.0")
        parked.rename(arm)
        shutil.copytree(bundles / "Windows-x64", bundles / "duplicate")
        with self.assertRaisesRegex(ValueError, "Duplicate package"):
            release.collect_assets(bundles, bundles / "combined", "0.2.0")

    def test_upload_finishes_before_stable_publication(self):
        published = {"draft": False, "prerelease": False, "html_url": "https://example.com/release"}
        with patch.object(release, "api", side_effect=[None, [], None, self.uploaded(), None, published]), \
                patch.object(release, "gh") as gh:
            self.publish()
        commands = [call.args for call in gh.call_args_list]
        self.assertEqual([args[1] for args in commands], ["create", "upload", "edit"])
        self.assertIn("--draft", commands[0])
        self.assertIn("--target", commands[0])
        self.assertIn(self.commit, commands[0])
        self.assertIn("--draft=false", commands[2])
        self.assertIn("--prerelease=false", commands[2])
        self.assertIn("--latest", commands[2])

    def test_partial_upload_stays_a_draft(self):
        uploaded = self.uploaded()
        uploaded["assets"].pop()
        with patch.object(release, "api", side_effect=[None, [], None, uploaded]), \
                patch.object(release, "gh") as gh:
            with self.assertRaisesRegex(ValueError, "keeping the release as a draft"):
                self.publish()
        self.assertNotIn("edit", [call.args[1] for call in gh.call_args_list])

    def test_published_release_is_not_overwritten_on_rerun(self):
        existing = self.uploaded() | {"draft": False, "html_url": "https://example.com/release"}
        with patch.object(release, "api", side_effect=[existing, {"base_commit": {"sha": self.commit}}]), \
                patch.object(release, "gh") as gh:
            self.publish()
        gh.assert_not_called()

    def test_tag_pointing_to_another_commit_is_rejected(self):
        with patch.object(release, "api", side_effect=[None, [], {"base_commit": {"sha": "b" * 40}}]), \
                patch.object(release, "gh") as gh:
            with self.assertRaisesRegex(ValueError, "different commit"):
                self.publish()
        gh.assert_not_called()

    def test_older_build_does_not_replace_latest(self):
        published = {"draft": False, "prerelease": False, "html_url": "https://example.com/release"}
        with patch.object(release, "api", side_effect=[None, [], None, self.uploaded(),
                {"tag_name": "v0.2.0+build.16"}, {"status": "behind"}, published]), \
                patch.object(release, "gh") as gh:
            self.publish()
        self.assertIn("--latest=false", gh.call_args_list[-1].args)

    def test_draft_is_resumed_without_creating_a_duplicate(self):
        existing = self.uploaded() | {"target_commitish": self.commit, "tag_name": self.tag}
        published = {"draft": False, "prerelease": False, "html_url": "https://example.com/release"}
        with patch.object(release, "api", side_effect=[None, [existing], None, None, [existing], None, published]), \
                patch.object(release, "gh") as gh:
            self.publish()
        self.assertEqual([call.args[1] for call in gh.call_args_list], ["upload", "edit"])

    def test_draft_lookup_checks_subsequent_release_pages(self):
        draft = {"tag_name": self.tag, "draft": True}
        older = [{"tag_name": f"other-{number}"} for number in range(100)]
        with patch.object(release, "api", side_effect=[None, older, [draft]]) as api:
            self.assertEqual(release.release_by_tag("owner/repo", self.tag), draft)
        self.assertEqual(api.call_args.args[0], "repos/owner/repo/releases?per_page=100&page=2")


if __name__ == "__main__":
    unittest.main()
