#!/usr/bin/env python3
"""Relocate a fault-injection test so its fixed tool paths are privately owned."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    args = parser.parse_args()
    runtime = args.runtime.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="vdqt-private-runtime-test-") as temporary:
        root = Path(temporary)
        executable = root / args.test.name
        shutil.copy2(args.test, executable)
        bundle = root / "ffmpeg-runtime"
        bundle.mkdir()
        shutil.copytree(runtime / "bin", bundle / "bin")
        # Only copied tool launchers are mutable. Encoder/library files stay
        # references to the real bundle and are never replaced by these tests.
        (bundle / "lib").symlink_to(runtime / "lib", target_is_directory=True)
        (bundle / "libexec").symlink_to(runtime / "libexec", target_is_directory=True)
        environment = os.environ.copy()
        environment["VDQT_RUNTIME_TEST_ROOT"] = str(root)
        environment["PATH"] = str(bundle / "bin") + os.pathsep + environment.get("PATH", "")
        return subprocess.run([str(executable)], env=environment, timeout=100).returncode


if __name__ == "__main__":
    raise SystemExit(main())
