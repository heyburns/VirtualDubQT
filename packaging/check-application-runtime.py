#!/usr/bin/env python3
"""Prove bundled-only startup with hostile PATH and private broken-bundle fixtures.

Copy the application and runtime before fault injection; never rename/delete
anything in the real bundle or a user's application. No system processes stop.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def check(application, runtime):
    with tempfile.TemporaryDirectory(prefix="vdqt locked runtime ") as temporary:
        root = Path(temporary)
        appdir = root / "application with spaces"
        appdir.mkdir()
        executable = appdir / "VirtualDubQt"
        shutil.copy2(application, executable)
        bundle = appdir / "ffmpeg-runtime"
        shutil.copytree(runtime, bundle, symlinks=True)
        traps = root / "fake system tools"
        traps.mkdir()
        marker = root / "system-tool-was-used"
        for name in ("ffmpeg", "ffprobe"):
            tool = traps / name
            tool.write_text('#!/bin/sh\nprintf "%s\\n" "SYSTEM TOOL USED" > "$VDQT_RUNTIME_TRAP"\nexit 99\n')
            tool.chmod(0o755)
        environment = os.environ.copy()
        environment.update(PATH=str(traps), QT_QPA_PLATFORM="offscreen",
                           VD_DISABLE_AUDIO_OUTPUT="1", VDQT_RUNTIME_TRAP=str(marker),
                           XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                           XDG_CACHE_HOME=str(root / "cache"), APPDIR="/wrong/inherited/appdir")
        # Do not mask an inherited library override: it must be rejected, not
        # silently accommodated. The harness itself normally starts clean.

        def launch(expected_success, label, env=environment, expected_error=None):
            child = subprocess.run([str(executable), "--runtime-info"], cwd=traps,
                                   env=env, text=True, capture_output=True, timeout=20)
            if marker.exists():
                raise RuntimeError(f"{label}: a system FFmpeg/ffprobe trap ran")
            if expected_success:
                if child.returncode or "system fallback disabled" not in child.stdout:
                    raise RuntimeError(f"{label}: {child.returncode}, {child.stderr}")
                if str(bundle) not in child.stdout:
                    raise RuntimeError(f"{label}: the wrong bundle was reported")
            else:
                if child.returncode == 0:
                    raise RuntimeError(f"{label}: an invalid runtime was accepted")
                if expected_error and expected_error not in child.stderr:
                    raise RuntimeError(f"{label}: wrong failure: {child.stderr}")
            print(f"PASS {label}", flush=True)

        launch(True, "fixed private runtime with fake system tools first in PATH")
        no_path = environment.copy()
        no_path["PATH"] = ""
        launch(True, "fixed private runtime with empty PATH", no_path)
        metadata_file = bundle / "runtime-build-info.json"
        saved_metadata = metadata_file.read_bytes()
        metadata = json.loads(saved_metadata)
        metadata.pop("platform_library_policy", None)
        metadata_file.write_text(json.dumps(metadata))
        launch(False, "old desktop/plugin-unsafe assembly is rejected", expected_error="compatibility policy")
        metadata_file.write_bytes(saved_metadata)
        # Qt already uses host fontconfig. An unused stale bundle copy must
        # still be rejected before a CLI AVS reader can load it in another order.
        stale_platform_library = bundle / "lib" / "deps" / "libfontconfig.so.1"
        stale_platform_library.write_bytes(b"owned stale-platform-library fixture")
        launch(False, "leftover host platform library is rejected", expected_error="unsafe bundled platform library")
        stale_platform_library.unlink()
        for name in ("ffmpeg", "ffprobe"):
            target = bundle / "bin" / name
            saved = target.read_bytes()
            target.unlink()
            launch(False, f"missing {name} cannot fall back", expected_error="System FFmpeg will not be used")
            target.write_bytes(saved)
            target.chmod(0o755)
            target.write_text(f'#!/bin/sh\nprintf "%s\\n" "{name} version wrong-build"\n')
            launch(False, f"wrong {name} build is rejected", expected_error="does not match")
            target.write_bytes(saved)
            target.chmod(0o755)

        # Simulate LD_LIBRARY_PATH substituting a same-ABI library from outside
        # the chosen bundle. Even equal version numbers must not make it valid.
        external = root / "foreign libraries"
        external.mkdir()
        for source in (bundle / "lib").glob("*.so*"):
            if source.is_symlink():
                (external / source.name).symlink_to(source.readlink())
            else:
                shutil.copy2(source, external / source.name)
        (external / "deps").symlink_to(bundle / "lib" / "deps", target_is_directory=True)
        override = environment.copy()
        override["LD_LIBRARY_PATH"] = str(external) + (":" + environment["LD_LIBRARY_PATH"] if environment.get("LD_LIBRARY_PATH") else "")
        launch(False, "foreign same-ABI FFmpeg libraries are rejected", override, "not the private bundle")

        missing_library = bundle / "lib" / "libavcodec.so.60"
        saved_link = missing_library.readlink()
        missing_library.unlink()
        launch(False, "missing private FFmpeg library cannot fall back")
        missing_library.symlink_to(saved_link)
        missing_directory = appdir / "parked-owned-test-bundle"
        bundle.rename(missing_directory)
        launch(False, "missing private bundle cannot fall back")
        missing_directory.rename(bundle)
        launch(True, "restored private bundle remains usable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    args = parser.parse_args()
    try:
        check(args.application.resolve(strict=True), args.runtime.resolve(strict=True))
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Application runtime contract failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
