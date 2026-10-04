"""Release publication must never expose partial, unverified packages."""
import hashlib
import importlib.util
from pathlib import Path
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
            (self.directory / (name + ".sha256")).write_text(
                f"{hashlib.sha256(data).hexdigest()}  {name}\n")
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
        self.assertEqual(len(release.verified_assets(self.directory, "0.2.0")), 10)
        (self.directory / "ArcadeWheel-0.2.0-macOS-arm64.dmg").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            release.verified_assets(self.directory, "0.2.0")

    def test_corrupt_packages_are_rejected(self):
        (self.directory / "ArcadeWheel-0.2.0-Windows-x64-Setup.exe").write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            release.verified_assets(self.directory, "0.2.0")

    def test_upload_finishes_before_stable_publication(self):
        published = {"draft": False, "prerelease": False, "html_url": "https://example.com/release"}
        with patch.object(release, "api", side_effect=[None, None, self.uploaded(), None, published]), \
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
        with patch.object(release, "api", side_effect=[None, None, uploaded]), \
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
        with patch.object(release, "api", side_effect=[None, {"base_commit": {"sha": "b" * 40}}]), \
                patch.object(release, "gh") as gh:
            with self.assertRaisesRegex(ValueError, "different commit"):
                self.publish()
        gh.assert_not_called()

    def test_older_build_does_not_replace_latest(self):
        published = {"draft": False, "prerelease": False, "html_url": "https://example.com/release"}
        with patch.object(release, "api", side_effect=[None, None, self.uploaded(),
                {"tag_name": "v0.2.0+build.16"}, {"status": "behind"}, published]), \
                patch.object(release, "gh") as gh:
            self.publish()
        self.assertIn("--latest=false", gh.call_args_list[-1].args)

    def test_draft_is_resumed_without_creating_a_duplicate(self):
        existing = self.uploaded() | {"target_commitish": self.commit}
        published = {"draft": False, "prerelease": False, "html_url": "https://example.com/release"}
        with patch.object(release, "api", side_effect=[existing, None, self.uploaded(), None, published]), \
                patch.object(release, "gh") as gh:
            self.publish()
        self.assertEqual([call.args[1] for call in gh.call_args_list], ["upload", "edit"])


if __name__ == "__main__":
    unittest.main()
