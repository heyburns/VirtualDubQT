"""Bundle contracts: no network, real codecs, framework installs, or GUI needed."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "packaging"))


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "packaging" / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


ASSEMBLER = module("runtime_assembler", "assemble-ffmpeg-runtime.py")
VERIFIER = module("runtime_verifier", "verify-ffmpeg-runtime.py")


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((ROOT / "packaging" / "ffmpeg-runtime.json").read_text())

    def test_framework_support_is_not_framework_distribution(self):
        self.assertFalse(self.manifest["deployment"]["frameworks_bundled"])
        self.assertFalse(self.manifest["deployment"]["python_bundled"])
        for flag in ("--enable-avisynth", "--enable-vapoursynth", "--enable-gpl", "--disable-nonfree",
                     "--enable-shared", "--disable-static", "--enable-runtime-cpudetect"):
            self.assertIn(flag, self.manifest["configure"])
        self.assertNotIn("--enable-nonfree", self.manifest["configure"])

    def test_all_30_previously_missing_encoders_are_required(self):
        software = {"hap", "libcodec2", "libgsm", "libgsm_ms", "libjxl", "libopenjpeg", "librav1e",
                    "libshine", "libspeex", "libtheora", "libtwolame", "libwebp_anim", "libwebp", "libxvid"}
        hardware = set(self.manifest["hardware_encoders"])
        self.assertEqual(len(software | hardware), 30)
        self.assertTrue((software | hardware) <= set(self.manifest["encoders"]))
        self.assertEqual(len(self.manifest["encoders"]), len(set(self.manifest["encoders"])))
        self.assertNotIn("libx264_10bit", self.manifest["encoders"])
        self.assertNotIn("libx265_lossless", self.manifest["encoders"])

    def test_source_revisions_are_pinned(self):
        for source in self.manifest["sources"].values():
            self.assertRegex(source["commit"], r"^[a-f0-9]{40}$")


class InventoryTests(unittest.TestCase):
    def test_encoder_legend_is_not_an_encoder(self):
        self.assertEqual(VERIFIER.inventory(" V..... = Video\n V....D libx264 test\n A..... aac test\n", "encoders"),
                         {"libx264", "aac"})

    def test_format_aliases_are_split(self):
        self.assertEqual(VERIFIER.inventory(" D  video4linux2,v4l2 capture\n DE alsa ALSA\n", "devices"),
                         {"video4linux2", "v4l2", "alsa"})

    def test_filter_legend_and_protocol_headers_are_ignored(self):
        self.assertEqual(VERIFIER.inventory(" T.. = Timeline\n .S. scale V->V\n ... abuffer |->A\n", "filters"),
                         {"scale", "abuffer"})
        self.assertEqual(VERIFIER.inventory("Input:\n  file\n  pipe\nOutput:\n  https\n", "protocols"),
                         {"file", "pipe", "https"})


class AssemblyTests(unittest.TestCase):
    def test_host_platform_stack_is_never_copied(self):
        for name in ("libva.so.2", "libva-x11.so.2", "libva-drm.so.2.2400.0", "libdrm.so.2",
                     "libfontconfig.so.1", "libfreetype.so.6", "libcairo.so.2", "libpangoft2-1.0.so.0",
                     "libX11.so.6", "libxcb-render.so.0", "libwayland-client.so.0",
                     "libstdc++.so.6.0.34", "libgcc_s.so.1", "libasound.so.2"):
            with self.subTest(name=name):
                self.assertEqual(ASSEMBLER.dependencies(f"{name} => /lib/{name} (0x123)\n"), [])

    def test_codec_and_intel_dispatcher_dependencies_remain_private(self):
        for name in ("libx264.so.164", "libjxl.so.0.7", "libcodec2.so.1.2", "libvpl.so.2"):
            with self.subTest(name=name):
                self.assertEqual(ASSEMBLER.dependencies(f"{name} => /lib/{name} (0x123)\n"),
                                 [(name, Path("/lib") / name)])

    def test_only_direct_edges_are_followed_not_a_host_librarys_closure(self):
        needed = ASSEMBLER.needed_libraries(
            ' 0x1 (NEEDED) Shared library: [libva.so.2]\n'
            ' 0x1 (NEEDED) Shared library: [libx264.so.164]\n')
        self.assertEqual(needed, {"libva.so.2", "libx264.so.164"})
        closure = ("libva.so.2 => /lib/libva.so.2 (0x123)\n"
                   "libfontconfig.so.1 => /lib/libfontconfig.so.1 (0x123)\n"
                   "libexpat.so.1 => /lib/libexpat.so.1 (0x123)\n"
                   "libx264.so.164 => /lib/libx264.so.164 (0x123)\n")
        self.assertEqual(ASSEMBLER.dependencies(closure, needed),
                         [("libx264.so.164", Path("/lib/libx264.so.164"))])

    def test_missing_direct_resolution_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "Missing direct"):
            ASSEMBLER.dependencies("", {"libx264.so.164"})

    def test_glibc_stays_on_host_but_codec_dependencies_are_copied(self):
        result = ASSEMBLER.dependencies("libc.so.6 => /lib/libc.so.6 (0x123)\n"
                                       "libx264.so.164 => /lib/libx264.so.164 (0x456)\n")
        self.assertEqual(result, [("libx264.so.164", Path("/lib/libx264.so.164"))])

    def test_unresolved_dependencies_are_not_silently_ignored(self):
        with self.assertRaisesRegex(RuntimeError, "Unresolved"):
            ASSEMBLER.dependencies("libx264.so.164 => not found\n")

    def test_hard_linked_frameworks_are_rejected_not_copied(self):
        for name in ("libavisynth.so.11", "libvapoursynth.so.0", "libvapoursynth-script.so.0", "libpython3.12.so.1.0"):
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "Framework must be optional"):
                ASSEMBLER.dependencies(f"{name} => /lib/{name} (0x123)\n")

    def test_library_aliases_cannot_replace_existing_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "libx.so.1").write_bytes(b"existing")
            with self.assertRaisesRegex(RuntimeError, "replace a file"):
                ASSEMBLER.alias(directory, "libx.so.1", directory / "libx.so.1.2")
            self.assertEqual((directory / "libx.so.1").read_bytes(), b"existing")

    def test_symlink_alias_is_relative_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            target = directory / "libx.so.1.2"
            target.write_bytes(b"library")
            ASSEMBLER.alias(directory, "libx.so.1", target)
            ASSEMBLER.alias(directory, "libx.so.1", target)
            self.assertEqual((directory / "libx.so.1").readlink(), Path("libx.so.1.2"))
            with self.assertRaisesRegex(RuntimeError, "Conflicting"):
                ASSEMBLER.alias(directory, "libx.so.1", directory / "other")


class LauncherTests(unittest.TestCase):
    def test_relocated_tools_preserve_external_framework_configuration(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "runtime with spaces"
            (root / "bin").mkdir(parents=True)
            (root / "libexec").mkdir()
            (root / "lib").mkdir()
            for name in ("libavcodec.so.60", "libavformat.so.60", "libavutil.so.58", "libavfilter.so.9",
                         "libswscale.so.7", "libswresample.so.4", "libavdevice.so.60", "libpostproc.so.57"):
                (root / "lib" / name).write_bytes(b"test fixture")
            for tool in ("ffmpeg", "ffprobe"):
                launch = root / "bin" / tool
                shutil.copy2(ROOT / "packaging" / "ffmpeg-runtime-launcher", launch)
                launch.chmod(0o755)
                child = root / "libexec" / tool
                child.write_text('#!/bin/sh\nprintf "%s\\n" "$LD_LIBRARY_PATH" "$PYTHONHOME" "$PYTHONPATH" "$@"\n')
                child.chmod(0o755)
                environment = {"PATH": "", "LD_LIBRARY_PATH": "/external/framework/lib",
                               "PYTHONHOME": "/external/python", "PYTHONPATH": "/external/modules"}
                result = subprocess.run([str(launch), "argument with spaces"], env=environment,
                                        text=True, capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(), [f"{root}/lib:{root}/lib/deps:/external/framework/lib",
                                 "/external/python", "/external/modules", "argument with spaces"])


if __name__ == "__main__":
    unittest.main()
