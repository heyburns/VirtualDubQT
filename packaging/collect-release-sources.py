#!/usr/bin/env python3
"""Record Ubuntu-built binary provenance and collect matching dependency sources.

Run in the trusted Ubuntu release builder, not on an end user's machine. Each
deployed library must have an installed package owner or be our own FFmpeg/app.
Source packages are downloaded, never executed or installed. A release fails
instead of silently omitting an unrecognized third-party binary.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True).strip()


# Require one exact binary package owner before attaching notices and source
# versions. Ambiguous paths cannot establish the provenance of a payload.
def package_owner(name):
    lines = output("dpkg-query", "--search", "*/" + name).splitlines()
    owners = {line.rsplit(": ", 1)[0] for line in lines if ": " in line}
    if len(owners) != 1:
        raise RuntimeError(f"Ambiguous/missing package owner for {name}: {owners}")
    return owners.pop()


# Record every packaged ELF's glibc floor, even if its source provenance
# was already collected for the private media runtime.
def collect(args):
    args.notices.mkdir(parents=True, exist_ok=True)
    args.sources.mkdir(parents=True, exist_ok=True)
    records = {}
    glibc_versions = {(0,)}
    for path in args.payload.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        glibc_versions.update(tuple(map(int, version.split("."))) for version in
                              re.findall(r"GLIBC_([0-9]+(?:\.[0-9]+)+)",
                                         output("readelf", "--version-info", str(path))))
        if args.skip_private_runtime and "ffmpeg-runtime" in path.relative_to(args.payload).parts:
            continue  # This already has its own exact provenance, notices and sources.
        if path.name in ("VirtualDubQt", "ffmpeg", "ffprobe") or path.name.startswith(
                ("libavcodec.so", "libavdevice.so", "libavfilter.so", "libavformat.so",
                 "libavutil.so", "libpostproc.so", "libswresample.so", "libswscale.so")):
            continue
        owner = package_owner(path.name)
        record = output("dpkg-query", "--show", "--showformat="
                        "${binary:Package}\t${Version}\t${source:Package}\t${source:Version}", owner)
        package, version, source, source_version = record.split("\t")
        records[package] = {"version": version, "source": source,
                            "source_version": source_version}
        notice = Path("/usr/share/doc") / package.split(":")[0] / "copyright"
        if not notice.is_file():
            raise RuntimeError(f"Missing copyright/license notice for {package}")
        target = args.notices / package.replace(":", "_")
        target.mkdir(exist_ok=True)
        shutil.copyfile(notice, target / "copyright")

    required_glibc = max(glibc_versions)
    if required_glibc > tuple(map(int, args.max_glibc.split("."))):
        raise RuntimeError(f"Payload needs glibc {required_glibc}, above documented {args.max_glibc}")
    (args.notices / "glibc-requirement.txt").write_text(".".join(map(str, required_glibc)) + "\n")
    print("::notice title=Bundled ELF glibc requirement::" + ".".join(map(str, required_glibc)))
    for source, version in sorted({(r["source"], r["source_version"]) for r in records.values()}):
        marker = args.sources / (source + "@" + version + ".version")
        if marker.exists():
            if marker.read_text().strip() != version:
                raise RuntimeError(f"Mixed versions of dependency source {source}")
            continue
        subprocess.run(["apt-get", "source", "--download-only", "--only-source",
                        f"{source}={version}"], cwd=args.sources, check=True)
        marker.write_text(version + "\n")

    (args.notices / "binary-packages.json").write_text(json.dumps(records, indent=2) + "\n")
    if args.ffmpeg_source:
        destination = args.sources / "ffmpeg"
        shutil.copytree(args.ffmpeg_source, destination, ignore=shutil.ignore_patterns(".git"))
        configuration = args.sources / "ffmpeg-build-configuration"
        configuration.mkdir()
        for relative in ("config.h", "config_components.h", "ffbuild/config.mak"):
            shutil.copyfile(args.ffmpeg_build / relative, configuration / Path(relative).name)
        scripts = Path(__file__).resolve().parent
        for name in ("build-ffmpeg-runtime.sh", "ffmpeg-runtime.json",
                     "ffmpeg-vapoursynth-optional.patch"):
            shutil.copyfile(scripts / name, configuration / name)
        ffmpeg_notices = args.notices / "ffmpeg"
        ffmpeg_notices.mkdir(exist_ok=True)
        for notice in args.ffmpeg_source.glob("COPYING.*"):
            shutil.copyfile(notice, ffmpeg_notices / notice.name)
        (ffmpeg_notices / "SOURCE.txt").write_text(
            "Built from the pinned FFmpeg source and patch in ffmpeg-runtime-manifest.json.\n"
            "Matching source packages and build configuration accompany this release\n"
            "in the third-party-sources archive. Frameworks are not bundled.\n")
    print(f"Recorded {len(records)} dependency packages with matching sources")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--notices", type=Path, required=True)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--ffmpeg-source", type=Path)
    parser.add_argument("--ffmpeg-build", type=Path)
    parser.add_argument("--skip-private-runtime", action="store_true")
    parser.add_argument("--max-glibc", default="2.38")
    arguments = parser.parse_args()
    if bool(arguments.ffmpeg_source) != bool(arguments.ffmpeg_build):
        parser.error("Supply both FFmpeg source and build paths")
    collect(arguments)
