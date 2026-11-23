#!/usr/bin/env python3
"""Assemble a relocatable private runtime, without borrowing system FFmpeg.

Only fresh output directories are accepted. Dependencies are discovered from
the trusted, locally built ELF files. Script frameworks and Python are excluded.
The graphics/font/audio platform and C++ runtime remain host dependencies too:
bundling older copies can break separately installed AviSynth plugins. GPU vendor
drivers are loaded at device initialization, not statically shipped here.
Release builders collect license notices and matching sources separately.
"""

import argparse
from collections import deque
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from ffmpeg_runtime_policy import HOST_PLATFORM_LIBRARY, is_host_library


DEPENDENCY = re.compile(r"^\s*(\S+)\s+=>\s+(\S+)\s+\(0x[0-9a-f]+\)")
NEEDED = re.compile(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]")
FRAMEWORK_LIBRARY = re.compile(r"^lib(?:avisynth|vapoursynth(?:-script)?|python\d[^/]*)\.so(?:\..*)?$")
FFMPEG_LIBRARIES = ("libavcodec", "libavdevice", "libavfilter", "libavformat",
                    "libavutil", "libpostproc", "libswresample", "libswscale")


def is_ffmpeg_library(name):
    return any(name.startswith(library + ".so") for library in FFMPEG_LIBRARIES)


def command(arguments, environment=None):
    result = subprocess.run(arguments, env=environment, capture_output=True,
                            text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(f"{arguments[0]} failed: {result.stderr or result.stdout}")
    return result.stdout


def needed_libraries(output):
    return set(NEEDED.findall(output))


# Use the direct NEEDED set when walking an ELF dependency closure. A full
# ldd listing also includes descendants of desktop libraries deliberately
# provided by the host.
def dependencies(output, needed=None):
    if "not found" in output:
        raise RuntimeError(f"Unresolved shared library:\n{output}")
    result = []
    for line in output.splitlines():
        match = DEPENDENCY.match(line)
        if match and (needed is None or match[1] in needed) and not is_host_library(match[1]):
            if FRAMEWORK_LIBRARY.fullmatch(match[1]):
                raise RuntimeError(f"Framework must be optional, not a linked dependency: {match[1]}")
            path = Path(match[2])
            if not path.is_absolute():
                raise RuntimeError(f"Non-absolute dependency path: {line}")
            result.append((match[1], path))
    if needed is not None:
        unresolved = {name for name in needed if not is_host_library(name)} - {name for name, _ in result}
        if unresolved:
            raise RuntimeError(f"Missing direct dependency resolutions: {sorted(unresolved)}")
    return result


def elf(path):
    if not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


# Keep the SONAME and development alias local to the payload. A conflict
# means two distinct files would be selected through the same name.
def alias(directory, name, target):
    destination = directory / name
    if destination == target:
        return
    if destination.is_symlink():
        if destination.resolve() != target.resolve():
            raise RuntimeError(f"Conflicting library alias: {destination}")
    elif destination.exists():
        raise RuntimeError(f"Library alias would replace a file: {destination}")
    else:
        destination.symlink_to(target.name)


# Collect the linked codec closure from one build prefix and set its loader
# paths relative to the new bundle. The editor and the command-line tools
# then resolve the same library identities after installation.
def assemble(prefix, output):
    prefix = prefix.resolve(strict=True)
    output = output.absolute()
    manifest = prefix / "ffmpeg-runtime-manifest.json"
    if not manifest.is_file():
        raise RuntimeError("The prefix has no completed runtime build manifest")
    if output.exists() or output.is_symlink():
        raise RuntimeError("Choose a fresh output directory; existing files are never replaced")
    for tool in ("ffmpeg", "ffprobe"):
        if not elf(prefix / "bin" / tool):
            raise RuntimeError(f"Missing built ELF tool: {tool}")
    for tool in ("ldd", "readelf", "patchelf"):
        if not shutil.which(tool):
            raise RuntimeError(f"Missing packaging tool: {tool}")

    output.mkdir(parents=True)
    libraries = output / "lib"
    libraries.mkdir()
    codec_dependencies = libraries / "deps"
    codec_dependencies.mkdir()
    (output / "bin").mkdir()
    (output / "libexec").mkdir()
    shutil.copy2(manifest, output / manifest.name)
    shutil.copy2(prefix / "ffmpeg-vapoursynth-optional.patch", output / "ffmpeg-vapoursynth-optional.patch")
    # The application must compile against these exact FFmpeg headers, not a
    # distribution SDK with a different ABI. No framework headers are copied.
    for library in FFMPEG_LIBRARIES:
        shutil.copytree(prefix / "include" / library, output / "include" / library)
    (libraries / "pkgconfig").mkdir()
    for source in (prefix / "lib" / "pkgconfig").glob("lib*.pc"):
        if source.stem not in FFMPEG_LIBRARIES:
            continue
        contents = source.read_text()
        contents = re.sub(r"^prefix=.*$", "prefix=${pcfiledir}/../..", contents, flags=re.MULTILINE)
        contents = re.sub(r"^libdir=.*$", "libdir=${prefix}/lib", contents, flags=re.MULTILINE)
        contents = re.sub(r"^includedir=.*$", "includedir=${prefix}/include", contents, flags=re.MULTILINE)
        (libraries / "pkgconfig" / source.name).write_text(contents)
    launcher = Path(__file__).with_name("ffmpeg-runtime-launcher")

    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(prefix / "lib")
    pending = deque()
    deployed_elfs = []
    for tool in ("ffmpeg", "ffprobe"):
        source = prefix / "bin" / tool
        target = output / "libexec" / tool
        shutil.copy2(source, target)
        pending.append(source)
        deployed_elfs.append(target)
        shutil.copy2(launcher, output / "bin" / tool)
        (output / "bin" / tool).chmod(0o755)

    # Retain application-facing FFmpeg libraries, including libavfilter and
    # libswresample, even when a CLI tool does not directly depend on them.
    # In particular, never seed libavisynth/libvapoursynth from the build SDK.
    pending.extend(path for path in (prefix / "lib").glob("*.so*")
                   if is_ffmpeg_library(path.name)
                   and not FRAMEWORK_LIBRARY.fullmatch(path.name) and elf(path))

    seen = set()
    origins = {}
    host_libraries = set()
    while pending:
        source = pending.popleft()
        real = source.resolve(strict=True)
        if FRAMEWORK_LIBRARY.fullmatch(real.name):
            raise RuntimeError(f"Refusing to bundle framework: {real.name}")
        if is_host_library(real.name):
            continue
        if real.parent != libraries and real.is_relative_to(prefix / "lib"):
            target_directory = libraries if is_ffmpeg_library(real.name) else codec_dependencies
            target = target_directory / real.name
            previous = origins.get(target.name)
            if previous and previous != real:
                raise RuntimeError(f"Different libraries share a basename: {previous}, {real}")
            if not target.exists():
                shutil.copy2(real, target)
                origins[target.name] = real
                deployed_elfs.append(target)
            alias(target_directory, source.name, target)
        if real in seen:
            continue
        seen.add(real)
        # ldd lists the entire closure, including dependencies of excluded host
        # libraries. Follow only this ELF's DT_NEEDED edges: otherwise a host
        # font/graphics library's older dependencies would still get copied.
        needed = needed_libraries(command(["readelf", "-d", str(real)]))
        host_libraries.update(name for name in needed if HOST_PLATFORM_LIBRARY.fullmatch(name))
        for soname, dependency in dependencies(command(["ldd", str(real)], environment), needed):
            resolved = dependency.resolve(strict=True)
            target_directory = libraries if is_ffmpeg_library(resolved.name) else codec_dependencies
            target = target_directory / resolved.name
            previous = origins.get(target.name)
            if previous and previous != resolved:
                raise RuntimeError(f"Different dependencies share a basename: {previous}, {resolved}")
            if not target.exists():
                shutil.copy2(resolved, target)
                origins[target.name] = resolved
                deployed_elfs.append(target)
            alias(target_directory, soname, target)
            pending.append(dependency)

    # DT_RUNPATH is per object, not transitive. Set each deployed object's path,
    # so every deployed object gets its own relative library search path.
    for path in deployed_elfs:
        # Only codec dependencies live here. Platform libraries must not exist
        # in either search directory, including for command-line AVS imports.
        relative = os.path.relpath(libraries, path.parent)
        root_path = "$ORIGIN" if relative == "." else f"$ORIGIN/{relative}"
        deps_relative = os.path.relpath(codec_dependencies, path.parent)
        deps_path = "$ORIGIN" if deps_relative == "." else f"$ORIGIN/{deps_relative}"
        rpath = f"{root_path}:{deps_path}"
        command(["patchelf", "--set-rpath", rpath, str(path)])
    metadata = {
        "status": "private runtime; release notices and sources collected separately",
        "library_origins": {name: str(path) for name, path in sorted(origins.items())},
        "host_dependencies": ["glibc", "system C++ runtime", "system graphics/font/audio libraries",
                              "GPU vendor drivers for hardware encoding",
                              "AviSynth+ for .avs input", "VapourSynth for .vpy input"],
        "host_platform_libraries": sorted(host_libraries),
        "platform_library_policy": "host-desktop-v1",
        "host_platform_library_pattern": HOST_PLATFORM_LIBRARY.pattern,
        "frameworks_bundled": False
    }
    (output / "runtime-build-info.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Assembled {len(origins)} shared libraries at {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        assemble(args.prefix, args.output)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Runtime assembly failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
