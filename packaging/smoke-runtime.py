#!/usr/bin/env python3
"""Exercise a real packaged editor, with fixtures and runtime tools kept separate.

Native-package CI installs only the package's declared dependencies in a fresh
container. AppImage CI supplies an empty runtime PATH and probes with the
extracted bundled ffprobe. Fixture creation uses an explicitly resolved host
ffmpeg; its directory is never silently added to the application's PATH.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile


def run(command, environment=None):
    """Bound every child; timeout cleanup targets only this helper's process group."""
    child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             env=environment, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=45)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            # The deadline and a clean exit can race; still reap our child.
            pass
        stdout, stderr = child.communicate()
        raise RuntimeError(f"Timed out: {command[0]}\n{stderr.decode(errors='replace')}")
    if child.returncode:
        raise RuntimeError(f"Command failed ({child.returncode}): {command}\n"
                           f"{stderr.decode(errors='replace')}")
    return stdout


def executable(value, label):
    candidate = value or shutil.which(label)
    if not candidate:
        raise RuntimeError(f"Missing {label}; supply its absolute path")
    path = Path(candidate).absolute()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise RuntimeError(f"Not an executable {label}: {path}")
    return str(path)


def sylia_path(path):
    # No shell is involved in the application's command argument. This is the
    # Sylia string-literal escaping used by its own command-line entry point.
    return str(path).replace("\\", "\\\\").replace('"', '\\"')


def smoke(args):
    app = executable(args.application, "VirtualDubQt")
    fixture_ffmpeg = executable(args.ffmpeg, "ffmpeg")
    fixture_ffprobe = executable(args.ffprobe, "ffprobe")
    runtime_ffprobe = executable(args.runtime_ffprobe or fixture_ffprobe, "ffprobe")
    with tempfile.TemporaryDirectory(prefix="vdqt-package-smoke-") as directory:
        root = Path(directory)
        empty_path = root / "no-host-executables"
        empty_path.mkdir()
        # Start with no host Qt/plugin/loader overrides. Only the explicit
        # runtime library directory may supplement a package's own RPATH/AppRun.
        environment = {
            "PATH": args.runtime_path if args.runtime_path is not None else str(empty_path),
            "LANG": "C.UTF-8", "QT_QPA_PLATFORM": "offscreen",
            "VD_DISABLE_AUDIO_OUTPUT": "1", "APPIMAGE_EXTRACT_AND_RUN": "1",
            "XDG_CONFIG_HOME": str(root / "config"),
            "XDG_DATA_HOME": str(root / "data"),
            "XDG_CACHE_HOME": str(root / "cache"),
            "XDG_RUNTIME_DIR": str(root / "runtime"),
        }
        (root / "runtime").mkdir(mode=0o700)
        if args.runtime_library_path:
            environment["LD_LIBRARY_PATH"] = args.runtime_library_path
        for diagnostic in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
            if diagnostic in os.environ:
                environment[diagnostic] = os.environ[diagnostic]

        source = root / "source.mkv"
        run([fixture_ffmpeg, "-nostdin", "-v", "error", "-y", "-f", "lavfi", "-i",
             "testsrc2=size=64x48:rate=24", "-frames:v", "8", "-c:v", "ffv1",
             "-threads", "2", "-an", str(source)])
        script = root / "source.avs"
        script.write_text('BlankClip(length=8, width=64, height=48, fps=24, '
                          'pixel_type="RGB24", audio_rate=0, color=$204080)\n',
                          encoding="utf-8")
        for index, input_path in enumerate((source, script)):
            raw = root / f"export-{index}.bgra"
            encoded = root / f"export-{index}.mkv"
            command = ('VirtualDub.audio.SetSource(0);VirtualDub.video.SetMode(3);'
                       'VirtualDub.video.SetCompression(0x31564646);'
                       f'VirtualDub.SaveRawVideo("{sylia_path(raw)}",8,4,0,0);'
                       f'VirtualDub.SaveAVI("{sylia_path(encoded)}");')
            run([app, str(input_path), "--command", command, "--exit"], environment)
            if not raw.is_file() or raw.stat().st_size != 8 * 64 * 48 * 4:
                raise RuntimeError(f"Wrong raw frame count/size for {input_path.name}")
            if index == 1 and raw.read_bytes() != bytes.fromhex("804020ff") * (8 * 64 * 48):
                raise RuntimeError("Native AviSynth export changed its known BGRA pixels")
            # The app must produce an actual encoded file, not just return zero.
            # Inspect with the runtime ffprobe, not an accidental PATH fallback.
            metadata = json.loads(run([runtime_ffprobe, "-v", "error", "-count_frames",
                                       "-select_streams", "v:0", "-show_entries",
                                       "stream=codec_name,width,height,nb_read_frames",
                                       "-of", "json", str(encoded)], environment))
            streams = metadata.get("streams", [])
            if len(streams) != 1 or streams[0] != {
                "codec_name": "ffv1", "width": 64, "height": 48, "nb_read_frames": "8"
            }:
                raise RuntimeError(f"Unexpected packaged export: {metadata}")
            # Independently decode the complete output; a valid header alone
            # does not establish that every exported frame is usable.
            run([fixture_ffmpeg, "-nostdin", "-v", "error", "-i", str(encoded),
                 "-map", "0:v:0", "-f", "null", "-"])
        print("Packaged application decoded FFV1/native AVS and exported 8-frame raw/FFV1 outputs")
        print(f"Application runtime PATH: {environment['PATH']}")
        print(f"Runtime ffprobe: {runtime_ffprobe}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", required=True)
    parser.add_argument("--ffmpeg", help="Absolute fixture-generation/validation tool")
    parser.add_argument("--ffprobe", help="Absolute host probe (native runtime default)")
    parser.add_argument("--runtime-ffprobe", help="Packaged probe; mandatory in AppImage CI")
    parser.add_argument("--runtime-path", help="Application-only PATH; default is empty directory")
    parser.add_argument("--runtime-library-path", help="Explicit packaged library search path")
    args = parser.parse_args()
    try:
        smoke(args)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Package smoke failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
