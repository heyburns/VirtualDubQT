# Building VirtualDubQT

These instructions target version **0.1.4**, Ubuntu 24.04 and Fedora 43 on
64-bit Intel/AMD Linux. You do not need to build FFmpeg: use the matching SDK
download below. The application never uses distribution FFmpeg.

If you only want to run the app, use the [installation guide](INSTALLATION.md).
AviSynth+ is a separate requirement for both packages and source builds.

## Install build tools

Ubuntu 24.04:

```bash
sudo apt update
sudo apt install build-essential cmake git curl ninja-build nasm xz-utils \
    qt6-base-dev qt6-multimedia-dev libasound2t64 libva2 libva-drm2 \
    libdrm2 libcairo2 libgl1 libegl1 ca-certificates
```

Fedora 43:

```bash
sudo dnf install gcc-c++ cmake git curl ninja-build nasm xz \
    qt6-qtbase-devel qt6-qtmultimedia-devel alsa-lib libva libdrm cairo \
    mesa-libGL mesa-libEGL ca-certificates
```

Run commands in order. If a command fails, stop and keep its error text.

## Install AviSynth

AviSynth+ 3.7.5 must be installed separately before launching the app, even when
opening an ordinary video. The app links to its library. Installing the app does
not install or replace AviSynth, VapourSynth, Python, or any script plugins.

If AviSynth+ 3.7.5 is already installed and working, skip this build. Otherwise:

```bash
mkdir -p "$HOME/src"
git clone --branch v3.7.5 --depth 1 \
    https://github.com/AviSynth/AviSynthPlus.git "$HOME/src/AviSynthPlus-3.7.5"
cmake -S "$HOME/src/AviSynthPlus-3.7.5" -B "$HOME/src/AviSynthPlus-3.7.5/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_LIBDIR=lib -DENABLE_PLUGINS=OFF
cmake --build "$HOME/src/AviSynthPlus-3.7.5/build" --parallel 2
sudo cmake --install "$HOME/src/AviSynthPlus-3.7.5/build"
printf '%s\n' '/usr/local/lib' | sudo tee /etc/ld.so.conf.d/avisynth.conf
sudo ldconfig
```

This installs the upstream framework, not extra script filters. On systems where
heavy Prefetch chains or repeated loads become slow, see the separately tested
[dependency patches](patches/README.md#avisynth-prefetch-performance). They are
not automatically applied to your installation by the app packages.

## Download the source and matching FFmpeg SDK

The SDK contains the same private media backend as the release packages, plus
the matching headers needed to compile against it. It contains no script
frameworks or Python installation. It requires glibc 2.38 or newer.

```bash
git clone --branch v0.1.4 --depth 1 \
    https://github.com/heyburns/VirtualDubQT.git "$HOME/src/VirtualDubQT-0.1.4"
cd "$HOME/src/VirtualDubQT-0.1.4"
curl --fail --location --remote-name \
    https://github.com/heyburns/VirtualDubQT/releases/download/v0.1.4/VirtualDubQT-0.1.4-ffmpeg-sdk-x86_64.tar.xz
curl --fail --location --remote-name \
    https://github.com/heyburns/VirtualDubQT/releases/download/v0.1.4/SHA256SUMS
sha256sum --check --ignore-missing SHA256SUMS
mkdir -p dist/ffmpeg-runtime-locked
tar -xJf VirtualDubQT-0.1.4-ffmpeg-sdk-x86_64.tar.xz -C dist/ffmpeg-runtime-locked
```

Expect the SDK filename followed by `OK` before extracting it. Stop if the
checksum fails. The automatically generated GitHub source archive does not
contain this binary SDK; download it separately as shown.

## Compile and run

From the source directory:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel 2
./build/VirtualDubQt --runtime-info
./build/VirtualDubQt
```

The first command locates Qt, AviSynth and the private FFmpeg SDK. The runtime
report must identify bundled-only FFmpeg and paths inside the selected bundle.
An incomplete bundle or conflicting library stops startup; system FFmpeg is
never a fallback.

Keep `build/ffmpeg-runtime` beside the build-tree executable. Installed builds
use the private backend under `lib/virtualdubqt` or `lib64/virtualdubqt`.

## Run the tests

```bash
cmake -S . -B build-checks -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-checks --parallel 2
QT_QPA_PLATFORM=offscreen VD_DISABLE_AUDIO_OUTPUT=1 \
    ctest --test-dir build-checks --output-on-failure --parallel 2 -LE hardware
```

Tests use generated temporary media and the selected private tools. Physical
audio/capture tests are excluded. The real L-SMASH plugin check is skipped when
that separately installed plugin is unavailable.

## Maintainer: build expanded private FFmpeg runtime

Ordinary users can use the SDK above. Maintainers rebuilding the backend should
use an isolated Ubuntu 24.04 x86_64 builder with the dependencies listed in
`.github/workflows/release.yml`. Do not install this build over system FFmpeg.

```bash
vdqt_media_work=$(mktemp -d)
bash packaging/build-ffmpeg-runtime.sh --jobs 2 \
    --work-dir "$vdqt_media_work/sources" --prefix "$vdqt_media_work/sdk"
python3 packaging/assemble-ffmpeg-runtime.py --prefix "$vdqt_media_work/sdk" \
    --output "$vdqt_media_work/runtime"
python3 packaging/verify-ffmpeg-runtime.py --runtime "$vdqt_media_work/runtime"
```

The manifest pins source commits and codec/build options. AviSynth and
VapourSynth are build/test fixtures only; the assembler excludes their libraries,
Python, and the system graphics/font/audio/C++ stack. It includes FFmpeg, codec
dependencies, matching tools and headers. GPU vendor drivers remain host
dependencies; a listed hardware encoder is not a guarantee of compatible hardware.

For release packaging, `collect-release-sources.py` records exact Ubuntu package
versions, copies notices and downloads matching dependency source archives.
The release also supplies patched FFmpeg source and its build configuration.
`build-appimage.sh` deploys Qt while preserving the private backend and excluding
separate frameworks and host platform libraries. DEB and RPM builds use their
own distro's Qt with the same private backend.

The GitHub workflow builds packages on packaging-related pushes to main, without
publishing them. A matching `v0.1.4` tag publishes only after the Ubuntu DEB,
AppImage and Fedora RPM pass clean-runtime export tests. When the exact tagged
commit already passed on main and its artifacts are still available, it promotes
those identical tested files; otherwise it builds and validates fresh packages.
Those tests supply
AviSynth as an independent fixture, not as part of the packages.

## Common build problems

| Problem | What to check |
| --- | --- |
| CMake cannot find Qt | Install your distro's Qt development packages listed above. |
| CMake cannot find AviSynth | Install its headers and library, then run `sudo ldconfig`. For a custom prefix, pass `-DAVISYNTH_INCLUDE_DIR=/prefix/include -DAVISYNTH_LIBRARY=/prefix/lib/libavisynth.so`. |
| Missing private FFmpeg bundle | Download and extract the matching release SDK, or use the maintainer build above. System FFmpeg development packages do not replace it. |
| Startup cannot find libavisynth | Complete the separate AviSynth installation and refresh the linker cache. |
| Missing/mixed FFmpeg runtime | Run `--runtime-info`; keep the complete bundle with the app and remove loader overrides that force another FFmpeg. Do not rename libraries to disguise incompatible versions. |
| Missing AVS function or plugin | Test the blank script in the installation guide, then follow the plugin's native Linux setup instructions. |
