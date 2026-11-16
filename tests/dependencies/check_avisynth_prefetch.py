#!/usr/bin/env python3
"""Optional real L-SMASH/AviSynth regression suite; never changes user media.

Requires a separately built test_lsmash_prefetch probe, FFmpeg and candidate
plugin/framework. The ordinary app test suite does not require these sources.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(command, environment):
    result = subprocess.run([str(value) for value in command], env=environment,
                            capture_output=True, text=True, timeout=90)
    if result.returncode:
        raise RuntimeError(f"Failed ({result.returncode}): {command}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def quoted(path):
    if '"' in str(path):
        raise ValueError("Double quote in fixture path")
    return f'"{path}"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", required=True, type=Path)
    parser.add_argument("--plugin", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--library-path", required=True, type=Path)
    args = parser.parse_args()
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(args.library_path.resolve(strict=True))
    ffmpeg, plugin, probe = (path.resolve(strict=True) for path in (args.ffmpeg, args.plugin, args.probe))
    with tempfile.TemporaryDirectory(prefix="vdqt-prefetch-regression-") as temporary:
        root = Path(temporary)
        fixtures = [("h264", "libx264", "yuv420p"),
                    ("ten", "ffv1", "yuv420p10le"),
                    ("sixteen", "ffv1", "yuv444p16le"),
                    ("alpha", "ffv1", "yuva444p16le"),
                    ("rgb", "ffv1", "gbrp16le"),
                    ("packed", "ffv1", "bgr0"),
                    ("gray", "ffv1", "gray16le"),
                    ("oversize", "ffv1", "yuv420p")]
        checks = 0
        for name, codec, pixel_format in fixtures:
            source = root / f"{name}.mkv"
            size = "1024x768" if name == "oversize" else "128x72"
            frames = 100 if name == "oversize" else 200
            options = ["-g", "48", "-bf", "3", "-preset", "ultrafast"] if codec == "libx264" else ["-level", "3"]
            run([ffmpeg, "-nostdin", "-v", "error", "-f", "lavfi", "-i",
                 f"testsrc2=size={size}:rate=25", "-f", "lavfi", "-i",
                 "sine=frequency=997:sample_rate=44100", "-t", str(frames / 25),
                 "-c:v", codec, *options, "-pix_fmt", pixel_format,
                 "-threads", "2", "-filter_threads", "1", "-c:a", "pcm_s16le", source], environment)
            prefix = f"ClearAutoloadDirs()\nLoadPlugin({quoted(plugin)})\n"
            def script(cache, workers, extra="", dr=True):
                return (prefix + f"v=LWLibavVideoSource({quoted(source)},threads=2,"
                        f"indexingpr=false,dr={'true' if dr else 'false'},prefetch_cache={cache}{extra})\n"
                        f"a=LWLibavAudioSource({quoted(source)},indexingpr=false)\n"
                        "AudioDub(v,a)\nResampleAudio(48000)\n" +
                        (f"Prefetch({workers})\n" if workers else ""))
            reference = root / "reference.avs"
            candidate = root / "candidate.avs"
            # Small cap exercises entry eviction; zero explicitly disables it;
            # an undersized cap must fall back safely, without a decode-zero loop.
            cases = [(256, 6, "", True), (1, 12, "", True), (0, 4, "", True),
                     (256, 12, ",fpsnum=50,fpsden=1", False)] if name in ("h264", "oversize") else [(256, 12, "", True)]
            for cache, workers, extra, dr in cases:
                reference.write_text(script(0, 0, extra, dr), encoding="utf-8")
                candidate.write_text(script(cache, workers, extra, dr), encoding="utf-8")
                output = run([probe, reference, candidate], environment)
                print(f"{name} cache={cache} workers={workers} dr={dr}: {output.strip()}", flush=True)
                checks += 1
        print(f"PASS {checks} Prefetch dependency cases", flush=True)


if __name__ == "__main__":
    main()
