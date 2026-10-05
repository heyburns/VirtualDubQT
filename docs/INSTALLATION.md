# Installation, FFmpeg, and AviSynth

VirtualDubQT targets reasonably current Linux desktops, roughly the last couple
of years. You are expected to be comfortable installing packages, running shell
commands, and managing the plugins used by your scripts. Maintaining older
systems and providing general Linux setup help are outside this project's scope.

## Choose a package

Download from the [releases page](https://github.com/heyburns/VirtualDubQT/releases).
Version 0.1.2 provides these packages for 64-bit Intel/AMD systems:

| Package | Intended use | Included dependencies |
| --- | --- | --- |
| AppImage | General download for recent Linux desktops | Qt, FFmpeg/ffprobe with AviSynth support, and AviSynth+ |
| DEB | Ubuntu 24.04 and systems with matching packages | AviSynth+; the package manager supplies Qt and FFmpeg |
| RPM | Fedora 43 and systems with matching packages | AviSynth+; the package manager supplies Qt and FFmpeg |

The AppImage requires glibc 2.38 or newer and normal desktop graphics, font,
and audio libraries. It is not a fully independent operating system. Its clean
runtime check passed on Ubuntu 24.04; the native packages passed on Ubuntu 24.04
and Fedora 43. Other distributions are not yet verified, and physical display,
audio, and capture hardware are not covered by these automated checks.

From the directory containing your download, use the matching command:

```bash
# AppImage
chmod +x VirtualDubQT-0.1.2-x86_64.AppImage
./VirtualDubQT-0.1.2-x86_64.AppImage

# DEB
sudo apt install ./virtualdubqt_0.1.2_amd64.deb

# RPM
sudo dnf install ./virtualdubqt-0.1.2-1.x86_64.rpm
```

If AppImage reports missing FUSE support, install your distro's FUSE 2 runtime
or run `APPIMAGE_EXTRACT_AND_RUN=1 ./VirtualDubQT-0.1.2-x86_64.AppImage`.
To verify a downloaded package, also download `SHA256SUMS` and run
`sha256sum --check --ignore-missing SHA256SUMS` in that directory.

If a native package cannot satisfy its dependencies, use the AppImage or build
VirtualDubQT from source against your distro's libraries. A newer FFmpeg library
is not always interchangeable with the version a package was built against.

## When custom FFmpeg is needed

There are two parts to FFmpeg: the `ffmpeg` and `ffprobe` commands, and library
files that VirtualDubQT loads when it starts. Native packages use the distro's
libraries; exports and other operations also call the commands found through
`PATH`, the list of directories Linux searches for programs.

VirtualDubQT reads `.avs` scripts directly through AviSynth+. Opening a script
does not require FFmpeg's AviSynth input support. **Fast Recompress hands the
script to the FFmpeg command, so that command must support AviSynth too.**
This explains why a script can open normally but fail during Fast Recompress.

For a native install or source build, provide FFmpeg with `--enable-avisynth`
if you use this mode with `.avs` files. Building it yourself is an expected
setup option. The AppImage already supplies matching tools and uses them ahead
of system FFmpeg, so AppImage users can skip the build recipe below.

Extra encoders also need their corresponding FFmpeg build options. A codec
listed by name in the application is not a promise that every FFmpeg build
contains it. Third-party AviSynth plugins are not bundled: install compatible
64-bit Linux versions and ensure your scripts can find them. Windows plugin
DLLs are not supported.

Replacing the commands does **not** replace the libraries loaded by a native
VirtualDubQT package. Do not change library filenames or force a package install
to get around a version mismatch. Use a matching package or rebuild the app.

## Tested custom FFmpeg recipe

This recipe uses **AviSynth+ 3.7.5 and FFmpeg 6.1.6**, the versions and build
options used for the 0.1.2 AppImage. The dependency commands below target
**Ubuntu 24.04**. On other distros, supply equivalent development packages;
these commands are not a universal installer. The pinned versions provide a
reproducible baseline, not a recommendation to ignore future security updates.

It installs AviSynth+ under `/usr/local` and the custom FFmpeg tools under
`/opt/virtualdubqt-ffmpeg`. The distro's FFmpeg packages remain installed.
This adds common software encoders, not every codec, hardware encoder, or
VapourSynth support.

### Install build dependencies

```bash
sudo apt update
sudo apt install build-essential cmake git ninja-build pkg-config nasm \
    libaom-dev libmp3lame-dev libopus-dev libsvtav1enc-dev \
    libvorbis-dev libvpx-dev libx264-dev libx265-dev
```

### Build and install AviSynth+

This step also supplies the headers and runtime needed to build VirtualDubQT
from source. If you already have an equivalent development installation, reuse
it. The AviSynth runtime inside the DEB/RPM is private to the application; it
does not supply the headers or system installation used by this recipe.

```bash
vdqt_build_dir="$(mktemp -d -t virtualdubqt-build-XXXXXXXX)"
git clone --branch v3.7.5 --depth 1 \
    https://github.com/AviSynth/AviSynthPlus.git "$vdqt_build_dir/AviSynthPlus"
cmake -S "$vdqt_build_dir/AviSynthPlus" -B "$vdqt_build_dir/AviSynthPlus/build" \
    -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_LIBDIR=lib -DENABLE_PLUGINS=OFF
cmake --build "$vdqt_build_dir/AviSynthPlus/build" --parallel 2
sudo cmake --install "$vdqt_build_dir/AviSynthPlus/build"
sudo ldconfig
```

`ENABLE_PLUGINS=OFF` builds the core runtime without the optional bundled plugin
targets. Install the source readers and filters your own scripts require.

### Build and install FFmpeg

```bash
vdqt_ffmpeg_dir="$(mktemp -d -t virtualdubqt-ffmpeg-XXXXXXXX)"
git clone --branch n6.1.6 --depth 1 \
    https://github.com/FFmpeg/FFmpeg.git "$vdqt_ffmpeg_dir/FFmpeg"
cd "$vdqt_ffmpeg_dir/FFmpeg"
./configure --prefix=/opt/virtualdubqt-ffmpeg \
    --disable-doc --disable-debug --disable-shared --enable-static \
    --enable-gpl --enable-avisynth --enable-libx264 --enable-libx265 \
    --enable-libvpx --enable-libmp3lame --enable-libopus \
    --enable-libvorbis --enable-libaom --enable-libsvtav1
make -j2
sudo make install
```

The static FFmpeg setting keeps these commands separate from the distro's
FFmpeg libraries. The commands still need AviSynth and the external codec
libraries installed above. This is not a replacement set of development
libraries for compiling VirtualDubQT.

### Verify FFmpeg and AviSynth together

Run this in Bash. It uses a generated clip, so no media file or third-party
source plugin is needed. Both commands must finish successfully.

```bash
(
    set -e
    export PATH="/opt/virtualdubqt-ffmpeg/bin:$PATH"
    command -v ffmpeg ffprobe
    ffmpeg -hide_banner -version
    vdqt_check_dir="$(mktemp -d -t virtualdubqt-check-XXXXXXXX)"
    trap 'rm -rf -- "$vdqt_check_dir"' EXIT
    printf '%s\n' 'BlankClip(length=8, width=64, height=48, fps=24, pixel_type="RGB24", audio_rate=0)' \
        > "$vdqt_check_dir/check.avs"
    ffprobe -v error -select_streams v:0 \
        -show_entries stream=width,height -of default=noprint_wrappers=1 \
        "$vdqt_check_dir/check.avs"
    ffmpeg -nostdin -v error -i "$vdqt_check_dir/check.avs" -f null -
    printf '%s\n' 'AviSynth input check passed.'
)
```

Expect `width=64`, `height=48`, and `AviSynth input check passed.` A failure here
is in the FFmpeg/AviSynth setup, before VirtualDubQT is involved. In particular,
FFmpeg must be able to find `libavisynth.so`; the installation and `ldconfig`
steps above arrange that on Ubuntu 24.04. If you use a different installation
location, configure your system's library search path accordingly.

### Use the custom tools with VirtualDubQT

Launch a native installation from a terminal:

```bash
export PATH="/opt/virtualdubqt-ffmpeg/bin:$PATH"
VirtualDubQt
```

For a source build, use `./build/VirtualDubQt` from the source directory instead.
The PATH change applies to that terminal and programs started from it. A desktop
menu launcher will not automatically inherit it; configure that launcher's
environment if you want to use it. The AppImage intentionally uses its own tools.

## Before reporting a problem

For a native install, check `command -v ffmpeg ffprobe`, `ffmpeg -version`, and
`ffmpeg -encoders` in the same terminal used to start VirtualDubQT. For an
AviSynth problem, run the generated-clip check first, then try your own script
with the same FFmpeg. Reduce a failing script to the smallest useful example.

For an AppImage problem, state that you used the AppImage; your system's FFmpeg
version does not describe its bundled tools. Try a simple `BlankClip` script in
the app before involving external source plugins.

Include your distro and version, VirtualDubQT version, package type or source
build, processing mode, exact error, and steps to reproduce. For script issues,
include the minimal script and relevant plugin versions.

Users are responsible for installing dependencies and maintaining their custom
FFmpeg and plugins. The project accepts reproducible VirtualDubQT bug reports;
it does not provide general distro administration, FFmpeg build support, or
debugging of arbitrary third-party plugins. If a setup check fails, resolve that
with the relevant project's documentation before reporting an application bug.

Reference: [FFmpeg's AviSynth documentation](https://ffmpeg.org/general.html#AviSynth)
and [AviSynth+](https://github.com/AviSynth/AviSynthPlus).
