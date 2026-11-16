#!/usr/bin/env python3
"""Exercise a private backend with the host desktop and a real L-SMASH plugin.

Fixtures, indexes, exports and configuration belong to a temporary directory.
Only a copied application is launched; no running editor or installed plugin is
changed. Missing optional L-SMASH returns CTest's explicit skip code, not success.
"""

import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile

from ffmpeg_runtime_policy import HOST_PLATFORM_LIBRARY


def run(command, environment):
    child = subprocess.Popen([str(value) for value in command], env=environment,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=45)
    except subprocess.TimeoutExpired:
        # Only this test's process group (including an export subprocess), never
        # an editor/export the user has running, may be stopped on timeout.
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.communicate()
        raise RuntimeError(f"Timed out: {command}")
    if child.returncode:
        raise RuntimeError(f"Failed ({child.returncode}): {command}\n{stderr}")
    return stdout


def platform_check(runtime):
    # Check the payload, not just paths that this particular load order happens
    # to select. Qt can hide a bad bundled fontconfig by loading the host first.
    for path in (runtime / "lib").rglob("*"):
        if HOST_PLATFORM_LIBRARY.fullmatch(path.name):
            raise RuntimeError(f"Platform library must remain on host: {path}")
    # Deliberately load FFmpeg BEFORE Qt/fonts, as the command-line tools do.
    # The former bundle broke newer host Pango and libva-x11 in this ordering.
    backend = ctypes.CDLL(str(runtime / "lib" / "libavcodec.so.60"), mode=ctypes.RTLD_GLOBAL)
    backend.avcodec_version.restype = ctypes.c_uint
    if backend.avcodec_version() >> 16 != 60:
        raise RuntimeError("Unexpected private FFmpeg ABI")
    for library in ("va-x11", "fontconfig", "pangoft2-1.0"):
        name = ctypes.util.find_library(library)
        if name:
            ctypes.CDLL(name)
            print(f"PASS private FFmpeg followed by host {library}", flush=True)
    print("PASS no conflicting platform libraries in bundle", flush=True)


def avs_path(path):
    # AviSynth strings accept Unix filenames directly, unlike Sylia's escapes.
    if '"' in str(path):
        raise RuntimeError(f"Double quote in AviSynth fixture pathname: {path}")
    return str(path)


def sylia_path(path):
    return str(path).replace("\\", "\\\\").replace('"', '\\"')


def find_plugin(explicit):
    if explicit:
        return explicit.resolve(strict=True)
    configured = os.environ.get("VDQT_AVISYNTH_TEST_PLUGIN")
    if configured:
        return Path(configured).resolve(strict=True)
    for directory in (Path.home() / ".local/lib64/avisynth", Path.home() / ".local/lib/avisynth",
                      Path("/usr/local/lib/avisynth"), Path("/usr/local/lib64/avisynth"),
                      Path("/usr/lib/avisynth"), Path("/usr/lib64/avisynth")):
        candidate = directory / "libLSMASHSource.so"
        if candidate.is_file():
            return candidate.resolve()
    return None


def plugin_check(application, runtime, plugin, framework_library_path):
    with tempfile.TemporaryDirectory(prefix="vdqt plugin runtime ") as temporary:
        root = Path(temporary)
        appdir = root / "bin"
        appdir.mkdir()
        app = appdir / "VirtualDubQt"
        shutil.copy2(application, app)
        (appdir / "ffmpeg-runtime").symlink_to(runtime, target_is_directory=True)
        # Installed binaries have an install-relative RUNPATH, not the build
        # tree's adjacent RUNPATH. Stage those supported layouts too; all links
        # resolve to this one read-only candidate, never a system FFmpeg.
        for libdir in ("lib", "lib64", "lib/x86_64-linux-gnu"):
            destination = root / libdir / "virtualdubqt"
            destination.mkdir(parents=True)
            (destination / "ffmpeg-runtime").symlink_to(runtime, target_is_directory=True)
        environment = os.environ.copy()
        environment.update(PATH="", QT_QPA_PLATFORM="offscreen", VD_DISABLE_AUDIO_OUTPUT="1",
                           XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                           XDG_CACHE_HOME=str(root / "cache"))
        if framework_library_path:
            environment["LD_LIBRARY_PATH"] = framework_library_path
        ffmpeg, ffprobe = (runtime / "bin" / name for name in ("ffmpeg", "ffprobe"))
        run([app, "--runtime-info"], environment)
        source = root / "source.mkv"
        run([ffmpeg, "-nostdin", "-v", "error", "-y", "-f", "lavfi", "-i",
             "testsrc2=size=96x64:rate=24", "-f", "lavfi", "-i",
             "sine=frequency=440:sample_rate=48000", "-t", "0.333333333",
             "-frames:v", "8", "-c:v", "libx264", "-preset", "ultrafast",
             "-threads", "2", "-c:a", "pcm_s16le", str(source)], environment)
        script = root / "source.avs"
        script.write_text(
            'ClearAutoloadDirs()\n'
            f'LoadPlugin("{avs_path(plugin)}")\n'
            f'v=LWLibavVideoSource("{avs_path(source)}",cache=false,threads=2)\n'
            f'a=LWLibavAudioSource("{avs_path(source)}",cache=false)\n'
            'AudioDub(v,a)\n', encoding="utf-8")
        # The CLI has a different load order from Qt. Decode video AND audio so
        # a successful LoadPlugin without evaluating its readers cannot pass.
        metadata = run([ffprobe, "-v", "error", "-show_entries", "stream=codec_type",
                        "-of", "csv=p=0", script], environment)
        if set(metadata.split()) != {"video", "audio"}:
            raise RuntimeError(f"L-SMASH did not expose video/audio: {metadata}")
        run([ffmpeg, "-nostdin", "-v", "error", "-i", script,
             "-map", "0:v:0", "-map", "0:a:0", "-f", "null", "-"], environment)
        print("PASS bundled ffmpeg/ffprobe evaluate unchanged L-SMASH video/audio", flush=True)
        for mode in (3, 1):
            raw = root / f"native-{mode}.bgra"
            encoded = root / f"native-{mode}.mkv"
            commands = (f'VirtualDub.audio.SetSource(0);VirtualDub.video.SetMode({mode});'
                        'VirtualDub.video.SetCompression(0x31564646);'
                        f'VirtualDub.SaveRawVideo("{sylia_path(raw)}",8,4,0,0);'
                        f'VirtualDub.SaveAVI("{sylia_path(encoded)}");')
            run([app, script, "--command", commands, "--exit"], environment)
            if raw.stat().st_size != 8 * 96 * 64 * 4:
                raise RuntimeError("Native L-SMASH export has wrong decoded frame count/size")
            frames = run([ffprobe, "-v", "error", "-count_frames", "-select_streams", "v:0",
                          "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", encoded], environment)
            if frames.strip() != "8":
                raise RuntimeError(f"Wrong native encoded frame count: {frames}")
            run([ffmpeg, "-nostdin", "-v", "error", "-i", encoded, "-f", "null", "-"], environment)
            print(f"PASS native L-SMASH preview/raw/FFV1 export, processing mode {mode}", flush=True)
        if (root / "native-3.bgra").read_bytes() != (root / "native-1.bgra").read_bytes():
            raise RuntimeError("Full/Fast L-SMASH raw exports differ")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--application", type=Path)
    parser.add_argument("--plugin", type=Path, help="Separately installed libLSMASHSource.so")
    parser.add_argument("--framework-library-path", help="Separately installed AviSynth library directory")
    parser.add_argument("--platform-only", action="store_true")
    args = parser.parse_args()
    try:
        runtime = args.runtime.resolve(strict=True)
        if args.platform_only:
            platform_check(runtime)
            return 0
        if not args.application:
            parser.error("--application is required for real plugin checks")
        plugin = find_plugin(args.plugin)
        if plugin is None:
            print("SKIP: install L-SMASH or set VDQT_AVISYNTH_TEST_PLUGIN to exercise this optional plugin")
            return 77
        plugin_check(args.application.resolve(strict=True), runtime, plugin, args.framework_library_path)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Script plugin runtime check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
