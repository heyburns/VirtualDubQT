"""Launcher contracts do not need a GUI, system package install, or network."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


LAUNCHER = Path(__file__).resolve().parents[1] / "packaging" / "AppRun"


class PackageLauncherTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="vdqt-launcher-")
        self.root = Path(self.directory.name) / "AppDir with spaces"
        self.bin = self.root / "usr" / "bin"
        self.bin.mkdir(parents=True)
        self.launcher = self.root / "AppRun"
        shutil.copy2(LAUNCHER, self.launcher)
        self.host = Path(self.directory.name) / "host"
        self.host.mkdir()
        self.empty = Path(self.directory.name) / "empty"
        self.empty.mkdir()
        self.script(self.bin / "VirtualDubQt", '#!/bin/sh\n'
                    'printf "%s\\n" "$APPDIR" "$LD_LIBRARY_PATH" "$VDQT_TEST_HOOK" "$@"\n'
                    'ffmpeg -version\nffprobe -version\n')
        for tool in ("ffmpeg", "ffprobe"):
            self.script(self.bin / tool, f'#!/bin/sh\nprintf "%s\\n" "bundled-{tool}"\n')
            self.script(self.host / tool, f'#!/bin/sh\nprintf "%s\\n" "host-{tool}"\n')

    def tearDown(self):
        self.directory.cleanup()

    @staticmethod
    def script(path, contents):
        path.write_text(contents, encoding="utf-8")
        path.chmod(0o755)

    def launch(self, path):
        environment = {"PATH": str(path), "APPDIR": "/wrong-inherited-appdir",
                       "LD_LIBRARY_PATH": "/explicit-user-library", "VDQT_TEST_HOOK": "unset"}
        return subprocess.run([str(self.launcher), "--command", "argument with spaces"],
                              env=environment, capture_output=True, text=True, timeout=5)

    def test_no_host_executables_are_needed_to_start(self):
        child = self.launch(self.empty)
        self.assertEqual(child.returncode, 0, child.stderr)
        self.assertEqual(child.stdout.splitlines(), [
            str(self.root), f"{self.root}/usr/lib:{self.root}/usr/lib/virtualdubqt:/explicit-user-library",
            "unset", "--command", "argument with spaces", "bundled-ffmpeg", "bundled-ffprobe"
        ])

    def test_bundled_tools_precede_host_tools(self):
        child = self.launch(self.host)
        self.assertEqual(child.returncode, 0, child.stderr)
        self.assertNotIn("host-ffmpeg", child.stdout)
        self.assertNotIn("host-ffprobe", child.stdout)
        self.assertIn("bundled-ffmpeg\nbundled-ffprobe", child.stdout)

    def test_missing_bundle_tool_cannot_fall_back_to_host(self):
        for tool in ("ffmpeg", "ffprobe"):
            with self.subTest(tool=tool):
                bundled = self.bin / tool
                contents = bundled.read_text(encoding="utf-8")
                bundled.unlink()
                child = self.launch(self.host)
                self.assertEqual(child.returncode, 127)
                self.assertIn(f"missing {tool}", child.stderr)
                self.assertEqual(child.stdout, "")
                self.script(bundled, contents)

    def test_deployment_hooks_are_sourced_before_application(self):
        for directory in ("apprun-hooks", "vdqt-apprun-hooks"):
            with self.subTest(directory=directory):
                hooks = self.root / directory
                hooks.mkdir()
                self.script(hooks / "test.sh", 'export VDQT_TEST_HOOK="hook-applied"\n')
                child = self.launch(self.empty)
                self.assertEqual(child.returncode, 0, child.stderr)
                self.assertEqual(child.stdout.splitlines()[2], "hook-applied")
                shutil.rmtree(hooks)


if __name__ == "__main__":
    unittest.main()
