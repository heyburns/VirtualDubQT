"""Release helper guardrails; no network, package installation or GUI needed."""
import importlib.util
from pathlib import Path
import re
import subprocess
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release_sources", ROOT / "packaging/collect-release-sources.py")
SOURCES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SOURCES)


class ReleasePackagingTests(unittest.TestCase):
    def test_unique_binary_owner(self):
        with patch.object(SOURCES, "output", return_value="libaom3:amd64: /usr/lib/x86_64-linux-gnu/libaom.so.3.8.2"):
            self.assertEqual(SOURCES.package_owner("libaom.so.3.8.2"), "libaom3:amd64")

    def test_ambiguous_binary_owner_fails(self):
        with patch.object(SOURCES, "output", return_value="one: /usr/lib/a.so\ntwo: /opt/lib/a.so"):
            with self.assertRaises(RuntimeError):
                SOURCES.package_owner("a.so")

    def test_missing_binary_owner_fails(self):
        with patch.object(SOURCES, "output", return_value=""):
            with self.assertRaises(RuntimeError):
                SOURCES.package_owner("unknown.so")

    def test_version_has_release_notes(self):
        version = re.search(r"project\(VirtualDubQt VERSION ([0-9.]+)",
                            (ROOT / "CMakeLists.txt").read_text()).group(1)
        self.assertTrue((ROOT / f"packaging/release-notes-v{version}.md").is_file())

    def test_shell_helpers_parse(self):
        for name in ("build-appimage.sh", "build-ffmpeg-runtime.sh", "release-shell.sh", "AppRun", "ffmpeg-runtime-launcher"):
            with self.subTest(name=name):
                subprocess.run(["bash", "-n", str(ROOT / "packaging" / name)], check=True)


if __name__ == "__main__":
    unittest.main()
