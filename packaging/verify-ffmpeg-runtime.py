#!/usr/bin/env python3
"""Validate a private FFmpeg bundle, with optional external-framework probes.

This checks advertised capabilities AND real bounded encode/decode jobs. It does
not claim hardware encoders work merely because they appear in the inventory.
All generated media lives in a disposable directory owned by this verifier.
"""

import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile

from ffmpeg_runtime_policy import HOST_PLATFORM_LIBRARY


FRAMEWORK = re.compile(r"^lib(?:avisynth|vapoursynth(?:-script)?|python\d[^/]*)\.so(?:\..*)?$")
PATTERNS = {
    "encoders": re.compile(r"^\s*[VAS][A-Z.]{5}\s+(\S+)\s"),
    "demuxers": re.compile(r"^\s*[DE]{1,2}\s+(\S+)\s"),
    "muxers": re.compile(r"^\s*[DE]{1,2}\s+(\S+)\s"),
    "devices": re.compile(r"^\s*[DE]{1,2}\s+(\S+)\s"),
    "filters": re.compile(r"^\s*[TSC.]{3}\s+(\S+)\s"),
    "protocols": re.compile(r"^\s+([a-z][a-z0-9_]+)\s*$")
}


def inventory(output, kind):
    names = set()
    for line in output.splitlines():
        match = PATTERNS[kind].match(line)
        if match and match[1] != "=":
            names.update(match[1].split(","))
    return names


def run(arguments, environment=None, timeout=45, allow_failure=False):
    child = subprocess.Popen([str(value) for value in arguments], env=environment,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.communicate()
        raise RuntimeError(f"Timed out: {arguments}")
    if child.returncode and not allow_failure:
        raise RuntimeError(f"Failed ({child.returncode}): {arguments}\n{stderr}")
    return child.returncode, stdout, stderr


def library_encoders(root):
    # Public AVCodec prefix is stable for libavcodec 60; reject other ABI majors
    # before inspecting structures. This is never a substitute for media tests.
    class AVCodec(ctypes.Structure):
        _fields_ = [("name", ctypes.c_char_p), ("long_name", ctypes.c_char_p),
                    ("type", ctypes.c_int)]
    codec = ctypes.CDLL(str(root / "lib" / "libavcodec.so.60"))
    codec.avcodec_version.restype = ctypes.c_uint
    if codec.avcodec_version() >> 16 != 60:
        raise RuntimeError("Unexpected FFmpeg library ABI")
    codec.av_codec_iterate.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    codec.av_codec_iterate.restype = ctypes.POINTER(AVCodec)
    codec.av_codec_is_encoder.argtypes = [ctypes.POINTER(AVCodec)]
    codec.av_codec_is_encoder.restype = ctypes.c_int
    state = ctypes.c_void_p()
    names = set()
    while True:
        entry = codec.av_codec_iterate(ctypes.byref(state))
        if not entry:
            return names
        if codec.av_codec_is_encoder(entry):
            names.add(entry.contents.name.decode("ascii"))


def software_roundtrips(ffmpeg, ffprobe, manifest, environment, directory):
    # Each override is a codec's real format/container constraint, not an
    # invented alias. Tests keep thread use bounded on the user's workstation.
    video = {
        "rawvideo": ("nut", "bgr24", []),
        "prores_ks": ("mov", "yuv422p10le", ["-profile:v", "3"]),
        "huffyuv": ("avi", "yuv422p", []),
        "cfhd": ("mov", "yuv422p10le", []),
        "mjpeg": ("avi", "yuvj422p", []),
        "mpeg1video": ("mpg", "yuv420p", []),
        "mpeg2video": ("mpg", "yuv420p", []),
        "mpeg4": ("avi", "yuv420p", []),
        "png": ("png", "rgb24", ["-update", "1"]),
        "gif": ("gif", "rgb8", []),
        "apng": ("apng", "rgba", []),
        "libx264": ("mkv", "yuv420p", ["-preset", "ultrafast"]),
        "libx265": ("mkv", "yuv420p", ["-preset", "ultrafast", "-x265-params", "pools=2:frame-threads=1"]),
        "libvpx": ("webm", "yuv420p", ["-deadline", "realtime"]),
        "libvpx-vp9": ("webm", "yuv420p", ["-deadline", "realtime", "-cpu-used", "8"]),
        "libsvtav1": ("mkv", "yuv420p", ["-preset", "12", "-svtav1-params", "lp=2"]),
        "libaom-av1": ("mkv", "yuv420p", ["-cpu-used", "8"]),
        "hap": ("mov", "rgba", []),
        "libjxl": ("jxl", "rgb24", []),
        "libopenjpeg": ("j2k", "rgb24", []),
        "librav1e": ("mkv", "yuv420p", ["-speed", "10"]),
        "libtheora": ("ogv", "yuv420p", []),
        "libwebp_anim": ("webp", "yuv420p", []),
        "libwebp": ("webp", "yuv420p", []),
        "libxvid": ("avi", "yuv420p", [])
    }
    audio = {
        "pcm_s16le": ("wav", 48000, 2), "pcm_s24le": ("wav", 48000, 2),
        "pcm_s32le": ("wav", 48000, 2), "pcm_f32le": ("wav", 48000, 2),
        "aac": ("m4a", 48000, 2), "ac3": ("ac3", 48000, 2),
        "flac": ("flac", 48000, 2), "libmp3lame": ("mp3", 44100, 2),
        "libopus": ("ogg", 48000, 2), "libvorbis": ("ogg", 48000, 2),
        "libcodec2": ("c2", 8000, 1), "libgsm": ("gsm", 8000, 1),
        "libgsm_ms": ("wav", 8000, 1), "libshine": ("mp3", 44100, 2),
        "libspeex": ("ogg", 16000, 1), "libtwolame": ("mp2", 44100, 2)
    }
    tested = []
    for encoder in manifest["encoders"]:
        if encoder in manifest["hardware_encoders"]:
            continue
        base = [ffmpeg, "-nostdin", "-v", "error", "-y", "-filter_threads", "2"]
        if encoder in audio:
            suffix, rate, channels = audio[encoder]
            target = directory / f"{encoder}.{suffix}"
            args = base + ["-f", "lavfi", "-i", f"sine=frequency=440:sample_rate={rate}:duration=0.5",
                           "-c:a", encoder, "-ar", str(rate), "-ac", str(channels),
                           "-threads", "2", target]
            frames = None
        else:
            suffix, pixels, options = video.get(encoder, ("mkv", "yuv420p", []))
            target = directory / f"{encoder}.{suffix}"
            frames = 1 if encoder in {"png", "libjxl", "libopenjpeg", "libwebp"} else 4
            args = base + ["-f", "lavfi", "-i", "testsrc2=size=128x96:rate=24",
                           "-frames:v", str(frames), "-an", "-c:v", encoder,
                           "-pix_fmt", pixels, "-threads", "2"] + options + [target]
        run(args, environment)
        if not target.is_file() or not target.stat().st_size:
            raise RuntimeError(f"{encoder}: no encoded output")
        # FFmpeg 6.1 does not decode animated WebP, although its libwebp_anim
        # encoder does work. Validate RIFF/ANIM/ANMF plus its static companion;
        # this is explicitly NOT represented as an animated decode roundtrip.
        if encoder == "libwebp_anim":
            data = target.read_bytes()
            if data[:4] != b"RIFF" or data[8:12] != b"WEBP" or b"ANIM" not in data or data.count(b"ANMF") != frames:
                raise RuntimeError("Invalid animated WebP output")
        else:
            _, probe, _ = run([ffprobe, "-v", "error", "-count_frames", "-show_entries",
                               "stream=nb_read_frames", "-of", "json", target], environment)
            streams = json.loads(probe).get("streams", [])
            if len(streams) != 1 or int(streams[0].get("nb_read_frames", "0")) < 1:
                raise RuntimeError(f"{encoder}: no readable media frames")
            if frames is not None and int(streams[0]["nb_read_frames"]) != frames:
                raise RuntimeError(f"{encoder}: wrong decoded frame count")
            run([ffmpeg, "-nostdin", "-v", "error", "-threads", "2", "-i", target,
                 "-threads", "2", "-f", "null", "-"], environment)
        tested.append(encoder)
        print(f"PASS {encoder}", flush=True)
    return tested


def framework_probes(ffmpeg, ffprobe, environment, directory):
    scripts = {
        "avisynth": ('blank.avs', 'BlankClip(length=8, width=128, height=96, fps=24, pixel_type="YV12", audio_rate=0)\n'),
        "vapoursynth": ('blank.vpy', 'import vapoursynth as vs\nvs.core.num_threads = 2\n'
                        'vs.core.std.BlankClip(width=128, height=96, length=8, fpsnum=24, format=vs.YUV420P8).set_output()\n')
    }
    for demuxer, (name, contents) in scripts.items():
        script = directory / name
        script.write_text(contents)
        target = directory / f"{demuxer}.mkv"
        run([ffmpeg, "-nostdin", "-v", "error", "-y", "-f", demuxer, "-i", script,
             "-an", "-c:v", "ffv1", "-threads", "2", target], environment)
        _, output, _ = run([ffprobe, "-v", "error", "-count_frames", "-show_entries",
                            "stream=nb_read_frames", "-of", "json", target], environment)
        if json.loads(output)["streams"][0]["nb_read_frames"] != "8":
            raise RuntimeError(f"Wrong external {demuxer} frame count")
        print(f"PASS external {demuxer} input", flush=True)


def high_depth_probes(ffmpeg, ffprobe, environment, directory):
    modes = [
        ("x264-10bit", "libx264", "yuv420p10le", ["-preset", "ultrafast", "-profile:v", "high10"]),
        ("x265-10bit", "libx265", "yuv420p10le", ["-preset", "ultrafast", "-x265-params", "pools=2:frame-threads=1"]),
        ("x265-12bit", "libx265", "yuv420p12le", ["-preset", "ultrafast", "-x265-params", "pools=2:frame-threads=1"]),
        ("ffv1-16bit-alpha", "ffv1", "gbrap16le", []),
        ("prores-4444-alpha", "prores_ks", "yuva444p10le", ["-profile:v", "4", "-alpha_bits", "16"])
    ]
    for name, encoder, pixels, options in modes:
        target = directory / f"{name}.{'mov' if encoder == 'prores_ks' else 'mkv'}"
        run([ffmpeg, "-nostdin", "-v", "error", "-y", "-filter_threads", "2", "-f", "lavfi",
             "-i", "testsrc2=size=128x96:rate=24", "-frames:v", "4", "-an", "-c:v", encoder,
             "-threads", "2", "-pix_fmt", pixels] + options + [target], environment)
        _, output, _ = run([ffprobe, "-v", "error", "-count_frames", "-show_entries",
                            "stream=nb_read_frames,pix_fmt", "-of", "json", target], environment)
        stream = json.loads(output)["streams"][0]
        # FFmpeg's ProRes decoder reports 12-bit planar output for the 4444
        # profile, even when its encoder input was 10-bit. Preserve alpha.
        expected = "yuva444p12le" if encoder == "prores_ks" else pixels
        if stream != {"nb_read_frames": "4", "pix_fmt": expected}:
            raise RuntimeError(f"{name}: wrong frame count or bit depth: {stream}")
        run([ffmpeg, "-nostdin", "-v", "error", "-threads", "2", "-i", target,
             "-f", "null", "-"], environment)
        print(f"PASS {name}", flush=True)


def absent_framework_probes(ffmpeg, environment, directory):
    # Use only in an environment where both framework runtimes are deliberately
    # unavailable. Script failures must not prevent ordinary codec operation.
    for demuxer, suffix in (("avisynth", "avs"), ("vapoursynth", "vpy")):
        script = directory / f"missing.{suffix}"
        script.write_text("# No external framework should be available in this check.\n")
        status, _, error = run([ffmpeg, "-nostdin", "-v", "error", "-f", demuxer,
                                "-i", script, "-f", "null", "-"], environment, allow_failure=True)
        if status <= 0 or "Segmentation fault" in error:
            raise RuntimeError(f"Missing {demuxer} framework did not fail cleanly: {status}, {error}")
        if demuxer == "vapoursynth" and "not installed or cannot be loaded" not in error:
            raise RuntimeError(f"The VapourSynth framework was not actually hidden: {error}")
        print(f"PASS absent {demuxer} framework fails cleanly", flush=True)


def nvenc_probes(ffmpeg, ffprobe, environment, directory):
    for encoder in ("h264_nvenc", "hevc_nvenc", "av1_nvenc"):
        target = directory / f"{encoder}.mkv"
        run([ffmpeg, "-nostdin", "-v", "error", "-y", "-f", "lavfi", "-i",
             "testsrc2=size=256x128:rate=24", "-frames:v", "8", "-an", "-c:v", encoder,
             "-threads", "2", target], environment)
        _, output, _ = run([ffprobe, "-v", "error", "-count_frames", "-show_entries",
                            "stream=nb_read_frames", "-of", "json", target], environment)
        if json.loads(output)["streams"][0]["nb_read_frames"] != "8":
            raise RuntimeError(f"Wrong {encoder} frame count")
        run([ffmpeg, "-nostdin", "-v", "error", "-threads", "2", "-i", target,
             "-f", "null", "-"], environment)
        print(f"PASS real hardware {encoder}", flush=True)


def verify(args):
    root = args.runtime.resolve(strict=True)
    manifest = json.loads((root / "ffmpeg-runtime-manifest.json").read_text())
    metadata = json.loads((root / "runtime-build-info.json").read_text())
    if metadata.get("platform_library_policy") != "host-desktop-v1":
        raise RuntimeError("Reassemble this bundle with the host desktop/plugin library policy")
    environment = os.environ.copy()
    # Tool wrappers preserve the user's framework setup; portable codec checks
    # deliberately provide no executable PATH and no build SDK library paths.
    environment["PATH"] = ""
    environment.pop("LD_LIBRARY_PATH", None)
    ffmpeg, ffprobe = (root / "bin" / tool for tool in ("ffmpeg", "ffprobe"))
    for path in root.rglob("*"):
        if FRAMEWORK.fullmatch(path.name) or path.name in {"site-packages", "dist-packages", "python"}:
            raise RuntimeError(f"Framework/Python payload is forbidden: {path}")
        if HOST_PLATFORM_LIBRARY.fullmatch(path.name):
            raise RuntimeError(f"Host platform library must not be bundled: {path}")
    for tool in (ffmpeg, ffprobe):
        _, version, _ = run([tool, "-version"], environment)
        if not re.match(r"^(?:ffmpeg|ffprobe) version n?6\.1\.6\b", version):
            raise RuntimeError(f"Wrong tool version: {tool}")
        if "--enable-nonfree" in version:
            raise RuntimeError("Unexpected nonfree configuration")
    actual = {}
    for kind in PATTERNS:
        _, listing, _ = run([ffmpeg, "-hide_banner", f"-{kind}"], environment)
        actual[kind] = inventory(listing, kind)
        missing = set(manifest[kind]) - actual[kind]
        if missing:
            raise RuntimeError(f"Missing {kind}: {sorted(missing)}")
        print(f"PASS required {kind}: {len(manifest[kind])}", flush=True)
    linked = library_encoders(root)
    if linked != actual["encoders"]:
        raise RuntimeError(f"CLI/library mismatch: CLI-only={sorted(actual['encoders'] - linked)}, "
                           f"library-only={sorted(linked - actual['encoders'])}")
    print(f"PASS CLI and application libraries agree on all {len(linked)} encoders", flush=True)
    with tempfile.TemporaryDirectory(prefix="vdqt-media-runtime-check-") as temporary:
        directory = Path(temporary)
        tested = software_roundtrips(ffmpeg, ffprobe, manifest, environment, directory)
        high_depth_probes(ffmpeg, ffprobe, environment, directory)
        if args.frameworks:
            # These dependencies are test fixtures / independently installed
            # host frameworks. They must never be added to the bundle itself.
            framework_environment = environment.copy()
            if args.framework_library_path:
                framework_environment["LD_LIBRARY_PATH"] = args.framework_library_path
            framework_probes(ffmpeg, ffprobe, framework_environment, directory)
        if args.without_frameworks:
            absent_framework_probes(ffmpeg, environment, directory)
        if args.nvenc:
            nvenc_probes(ffmpeg, ffprobe, environment, directory)
    print(f"Validated {len(tested)} software encoders; animated WebP is encode-only.")
    untested_hardware = [name for name in manifest['hardware_encoders'] if not (args.nvenc and name.endswith("_nvenc"))]
    print(f"Hardware availability not exercised: {', '.join(untested_hardware)}")
    if not args.frameworks:
        print("External AviSynth/VapourSynth execution not exercised (support inventory checked).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--frameworks", action="store_true", help="Probe separately installed script frameworks")
    parser.add_argument("--framework-library-path", help="External frameworks only, never copied into runtime")
    parser.add_argument("--without-frameworks", action="store_true", help="Require clean errors with external frameworks unavailable")
    parser.add_argument("--nvenc", action="store_true", help="Require real NVIDIA H.264/HEVC/AV1 encode/decode tests")
    args = parser.parse_args()
    if args.frameworks and args.without_frameworks:
        parser.error("Choose external-framework or framework-absent probes, not both")
    try:
        verify(args)
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print(f"Runtime verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
