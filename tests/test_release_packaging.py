"""Release helper guardrails; no network, package installation or GUI needed."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile
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

    def check_glibc(self, version):
        with tempfile.TemporaryDirectory(prefix="vdqt-release-glibc-") as directory:
            root = Path(directory)
            payload = root / "payload"
            payload.mkdir()
            (payload / "ffmpeg").write_bytes(b"\x7fELF")
            args = SOURCES.argparse.Namespace(payload=payload, notices=root / "notices",
                sources=root / "sources", skip_private_runtime=False, max_glibc="2.38",
                ffmpeg_source=None)
            with patch.object(SOURCES, "output", return_value="Name: GLIBC_" + version), patch("builtins.print"):
                SOURCES.collect(args)
            return (args.notices / "glibc-requirement.txt").read_text().strip()

    def test_glibc_baseline_compares_numerically(self):
        self.assertEqual(self.check_glibc("2.9"), "2.9")

    def test_newer_glibc_requirement_fails_the_release(self):
        with self.assertRaises(RuntimeError):
            self.check_glibc("2.39")

    def test_shell_helpers_parse(self):
        for name in ("build-appimage.sh", "build-ffmpeg-runtime.sh", "release-shell.sh", "AppRun", "ffmpeg-runtime-launcher"):
            with self.subTest(name=name):
                subprocess.run(["bash", "-n", str(ROOT / "packaging" / name)], check=True)

    def test_failure_annotation_keeps_original_exit_status(self):
        with tempfile.TemporaryDirectory(prefix="vdqt-release-shell-") as directory:
            env = dict(os.environ, BASH_ENV=str(ROOT / "packaging/release-shell.sh"), RUNNER_TEMP=directory)
            env.pop("VDQT_RELEASE_SHELL_ACTIVE", None)
            child = subprocess.run(["bash", "-e", "-c", "printf 'expected test failure\\n'; (exit 7)"],
                                   env=env, text=True, capture_output=True, timeout=10)
            self.assertEqual(child.returncode, 7, child.stderr)
            self.assertIn("::error title=Release build diagnostics::", child.stdout)

    def test_success_is_not_annotated_as_failure(self):
        with tempfile.TemporaryDirectory(prefix="vdqt-release-shell-") as directory:
            env = dict(os.environ, BASH_ENV=str(ROOT / "packaging/release-shell.sh"), RUNNER_TEMP=directory)
            env.pop("VDQT_RELEASE_SHELL_ACTIVE", None)
            child = subprocess.run(["bash", "-e", "-c", "printf 'success\\n'"],
                                   env=env, text=True, capture_output=True, timeout=10)
            self.assertEqual(child.returncode, 0, child.stderr)
            self.assertEqual(child.stdout, "success\n")


if __name__ == "__main__":
    unittest.main()
