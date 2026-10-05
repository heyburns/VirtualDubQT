# Building VirtualDubQT

Building means turning the project's source files into a program for your
computer. You do not need to edit the code. This guide takes you from installing
the tools to running version **0.1.2**.

If you just want to use the app, [install a package](INSTALLATION.md#choose-a-package).
Read the short [terminal instructions](INSTALLATION.md#before-running-commands)
before copying commands. Use the commands for your distro and stop if a step
fails. Keep the terminal open while working through a recipe.

The app needs a C++17 compiler, CMake 3.19+, Qt 6, FFmpeg 6+ development files,
and AviSynth+ development files. *Development files* include headers: descriptions
the compiler needs, in addition to the libraries used to run the app. Installing
only the `ffmpeg` command is not enough to compile VirtualDubQT.

The tested 0.1.2 combinations are Ubuntu 24.04 with Qt 6.4/FFmpeg 6.1 and Fedora
43 with Qt 6.10/FFmpeg 7.1. Other distros need equivalent packages; a minimum
version does not mean every newer combination has been tested.

## 1. Install the build tools

Choose **one** of these blocks. The package manager installs the compiler and
the development files; you do not need to find their individual download sites.

### Ubuntu 24.04

```bash
sudo apt update
sudo apt install build-essential cmake git ninja-build pkg-config ffmpeg \
    qt6-base-dev qt6-multimedia-dev libavcodec-dev libavformat-dev \
    libavutil-dev libavfilter-dev libswscale-dev libswresample-dev
```

### Fedora 43

```bash
sudo dnf install gcc-c++ cmake git ninja-build pkgconf-pkg-config \
    qt6-qtbase-devel qt6-qtmultimedia-devel ffmpeg-free ffmpeg-free-devel
```

When installation finishes without errors, continue with AviSynth+.

## 2. Build and install AviSynth+

The app uses AviSynth+ directly, so this step is required for an app source
build even if you do not plan to open scripts. If you already have its headers
and library installed, you can reuse that installation. The DEB/RPM's private
AviSynth runtime does not include these development files.

These commands download version 3.7.5 into a new temporary folder, compile it,
and install it under `/usr/local`. `sudo ldconfig` refreshes Linux's library
lookup information. Run this entire block in the same terminal:

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

Expect CMake to finish configuring, a successful build, and installation messages
for files under `/usr/local`. No output from `ldconfig` is normal. The core
runtime includes `BlankClip`, used by the first-use example. Optional source
readers and filters are separate plugins you install as needed.

## 3. Download the application source

The commands below create a `src` folder in your home directory and download the
released source into `VirtualDubQT-0.1.2`. `git clone` means “download a working
copy.” The `v0.1.2` tag selects the release rather than unfinished changes on
`main`. Git may mention a “detached HEAD”; that is normal when checking out a tag.

```bash
mkdir -p "$HOME/src"
git clone --branch v0.1.2 --depth 1 \
    https://github.com/heyburns/VirtualDubQT.git "$HOME/src/VirtualDubQT-0.1.2"
cd "$HOME/src/VirtualDubQT-0.1.2"
```

If that folder already contains your source copy, skip `git clone` and use `cd`
to enter it. If you downloaded GitHub's source ZIP instead, extract it and open
a terminal in the extracted folder. In either case, `ls` should show
`CMakeLists.txt`, `README.md`, and `src` before you continue.

## 4. Compile and run

Run these commands from that source folder:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel 2
./build/VirtualDubQt
```

The first command checks dependencies and prepares a `build` folder. Expect
“Configuring done” and “Generating done.” The second compiles the program;
`--parallel 2` uses two build jobs. The last opens the editor. `BUILD_TESTING=OFF`
skips building the developer regression tests; it does not remove app features.

This runs your build without installing it system-wide. To open it again later:

```bash
"$HOME/src/VirtualDubQT-0.1.2/build/VirtualDubQt"
```

Use your actual extracted folder if you chose a ZIP instead. Next, follow
[your first open and export](INSTALLATION.md#your-first-open-and-export).
For developer tests, see [TESTING.txt](TESTING.txt); their presets need CMake 3.21+.

## Optional: build custom FFmpeg

You can skip this section if your current tools meet your needs. The AppImage
already includes an AviSynth-enabled FFmpeg. For a native package or source
build, [check your current FFmpeg](#check-ffmpeg-and-avisynth-together) before
deciding whether to build one.

This recipe targets **Ubuntu 24.04** and uses **FFmpeg 6.1.6 with AviSynth+ 3.7.5**,
the versions and options used for the 0.1.2 AppImage. It adds common software
encoders, not every codec, hardware encoder, or VapourSynth support. On other
distros, the package names and available encoder libraries differ; this is not
a tested custom-FFmpeg recipe for Fedora. The AppImage is the ready-made option.

The commands install custom tools under `/opt/virtualdubqt-ffmpeg`, leaving
the distro's FFmpeg packages in place. These pinned versions reproduce the
release setup; maintain your installation as security updates become available.

### Install the extra development files

```bash
sudo apt update
sudo apt install build-essential cmake git ninja-build pkg-config nasm \
    libaom-dev libmp3lame-dev libopus-dev libsvtav1enc-dev \
    libvorbis-dev libvpx-dev libx264-dev libx265-dev
```

If you have not installed AviSynth+ headers and runtime, complete
[step 2](#2-build-and-install-avisynth) above, then return here. You do not need
to compile VirtualDubQT itself to replace the FFmpeg commands.

### Compile FFmpeg

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

Expect `ffmpeg` and `ffprobe` under `/opt/virtualdubqt-ffmpeg/bin`. The static
setting includes FFmpeg's own libraries in these commands. They still need
AviSynth and the external codec libraries installed above; this does not replace
the development libraries used to compile VirtualDubQT.

Select the newly built tools in this terminal before running the check:

```bash
export PATH="/opt/virtualdubqt-ffmpeg/bin:$PATH"
```

`PATH` is the list of folders Linux searches for programs. Putting this folder
first makes the terminal—and an app launched from it—use the custom tools.

## Check FFmpeg and AviSynth together

For a native install, run this in the same terminal you will use to launch the
app. It checks the FFmpeg currently selected by `PATH`; for a new custom build,
first run the `export PATH=...` command above. AppImage users should use the
[in-app example](INSTALLATION.md#your-first-open-and-export) instead, because
terminal FFmpeg commands do not test the AppImage's bundled tools.

This block generates a tiny clip, checks it, and removes the temporary file.
It needs no video download or extra source plugin:

```bash
(
    set -e
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

The first lines identify the commands being tested. With the custom build,
both paths should begin with `/opt/virtualdubqt-ffmpeg/bin/`. Expect `width=64`,
`height=48`, and `AviSynth input check passed.` If it stops with an error,
use the table below; no VirtualDubQT code is involved in this check.

## Use the custom tools with VirtualDubQT

For an installed DEB/RPM, run:

```bash
export PATH="/opt/virtualdubqt-ffmpeg/bin:$PATH"
VirtualDubQt
```

For the source build from this guide, run this instead:

```bash
export PATH="/opt/virtualdubqt-ffmpeg/bin:$PATH"
"$HOME/src/VirtualDubQT-0.1.2/build/VirtualDubQt"
```

Repeat the appropriate pair of commands in a new terminal when you want to use
the custom tools. A desktop menu does not inherit changes made in a terminal.
The AppImage uses its own bundled tools regardless of this PATH setting.

### Optional desktop menu shortcut

For a **DEB/RPM installation** with the custom tools above, the following creates
a separate menu entry called **VirtualDubQT (custom FFmpeg)**. It leaves the
original entry available. Run:

```bash
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications"
cat > "$HOME/.local/bin/virtualdubqt-custom-ffmpeg" <<'SH'
#!/bin/sh
export PATH="/opt/virtualdubqt-ffmpeg/bin:$PATH"
exec /usr/bin/VirtualDubQt "$@"
SH
chmod +x "$HOME/.local/bin/virtualdubqt-custom-ffmpeg"
cat > "$HOME/.local/share/applications/virtualdubqt-custom-ffmpeg.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=VirtualDubQT (custom FFmpeg)
Exec="$HOME/.local/bin/virtualdubqt-custom-ffmpeg" %F
Icon=video-x-generic
Terminal=false
Categories=AudioVideo;AudioVideoEditing;
EOF
```

For a **source build**, first create the shortcut above, then open
`$HOME/.local/bin/virtualdubqt-custom-ffmpeg` in a text editor and replace its
last line with:

```sh
exec "$HOME/src/VirtualDubQT-0.1.2/build/VirtualDubQt" "$@"
```

If you used a different source folder, put that path here. Test the shortcut
with `"$HOME/.local/bin/virtualdubqt-custom-ffmpeg"`, then select the new name in
your desktop's application menu. Some desktops refresh their menu only after
you log out and back in.

## Build and setup problems

| Message or symptom | Meaning and next step |
| --- | --- |
| `cmake`, `git`, `ninja`, or compiler not found | The build tools are missing. Complete step 1 for your distro. |
| CMake cannot find `Qt6` or an FFmpeg module | Install the development packages from step 1. Having a media player or the FFmpeg command installed is not sufficient. |
| AviSynth headers/library not found | Finish step 2, including installation. If you installed it somewhere else, use that installation's actual header and library paths. |
| CMake says the generator does not match | The existing `build` folder was configured with another build tool. Use a new folder, such as `build-ninja`, in **both** CMake commands, then run `./build-ninja/VirtualDubQt`. |
| `git clone`: destination already exists | Enter your existing source folder, or choose a new empty destination. Do not clone over an existing working copy. |
| Compiler killed, or out of memory | Retry the build command with `--parallel 1`; for FFmpeg use `make -j1`. |
| FFmpeg configure cannot find `x264`, `x265`, or another encoder library | Install the extra development packages in the custom FFmpeg section, then rerun configure. |
| FFmpeg rejects `.avs` or reports it cannot load AviSynth | Check `ffmpeg -hide_banner -demuxers` for `avisynth`. If absent, select/build an enabled FFmpeg. If present, it also needs the installed AviSynth runtime; on Ubuntu, complete step 2 and `sudo ldconfig`. |
| Custom build works in the terminal, but not from the menu | The menu may start the distro's tools. Use the explicit launch command or optional shortcut above. |

If your steps match this guide and still fail, report the distro/version,
failing command, and first error with surrounding output. Documentation mistakes
are worth reporting; you are not expected to reverse-engineer a missing step.
See [getting help](INSTALLATION.md#getting-help-and-reporting-bugs).

Further reference: [FFmpeg's AviSynth documentation](https://ffmpeg.org/general.html#AviSynth)
and [AviSynth+](https://github.com/AviSynth/AviSynthPlus).
