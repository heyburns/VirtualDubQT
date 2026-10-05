# VirtualDubQT

<p align="center">
  <img src="https://raw.githubusercontent.com/heyburns/VirtualDubQT/main/docs/screenshot.png" alt="VirtualDubQt Screenshot" width="800">
</p>

> **THIS IS INCOMPLETE AND BUGGY, SO USE AT YOUR OWN RISK. IF YOU FIND A BUG, PLEASE SUBMIT A REPORT AND/OR A PULL REQUEST.**

VirtualDub is one of those indispensible video editing apps that simply has no equivalent in the Linux ecosystem. **VirtualDubQT** is an attempt to create a modern 64-bit native Linux port of **VirtualDub2**, rewritten in clean C++17 and Qt6. It brings VirtualDub's utility to Linux desktop platforms without the performance overhead of using virtualization or wine.

---

## Download and requirements

Get the packages from the [releases page](https://github.com/heyburns/VirtualDubQT/releases).
For version 0.1.2, choose the **AppImage** for a bundled setup, the **DEB** for
Ubuntu 24.04, or the **RPM** for Fedora 43. All three are for 64-bit Intel/AMD
Linux. Native packages are tied to their distribution's libraries; a newer
distribution does not automatically accept an older package.

The project targets reasonably current Linux desktops, roughly the last couple
of years. This is a support target, not a claim that every distribution has been
tested. Package checks currently cover Ubuntu 24.04 and Fedora 43. The AppImage
needs glibc 2.38 or newer plus the usual desktop libraries.

- **FFmpeg and codecs:** The AppImage includes FFmpeg and ffprobe. Native installs
  use the tools and libraries on your system. The encoders you want must be
  available in the FFmpeg build the application uses.
- **AviSynth:** VirtualDubQT opens `.avs` scripts directly through AviSynth+.
  **Fast Recompress of `.avs` scripts also needs an FFmpeg executable
  built with `--enable-avisynth`.** Distro FFmpeg packages may omit this. If yours does, use
  the AppImage or build a suitable FFmpeg. The AppImage already includes it.
- **Plugins:** Install any extra AviSynth plugins your scripts require yourself.
  They must be compatible native Linux plugins; Windows DLLs do not work here.
- **VapourSynth:** `.vpy` support requires the `vapoursynth` input module in both
  the FFmpeg libraries and command-line tools. The supplied custom FFmpeg recipe
  does not enable it.

See the [installation and FFmpeg guide](docs/INSTALLATION.md) for package
commands, the tested AviSynth/FFmpeg build recipe, checks, and bug-report
requirements. Users are expected to manage their Linux dependencies and script
plugins; general system setup is outside the project's support scope.

---

## What Works

- Open common video files, play them, scrub the timeline, jump directly to a frame or time, step through frames, and move between keyframes.
- Mark a range and cut, copy, paste, delete, crop, undo, redo, append clips, mask ranges, add markers, and zoom the timeline.
- View the original and filtered video side by side. Play Preview follows filter timing, including bob-doubled output and variable-rate sources.
- Use 47 built-in video filters and 10 audio filters. The heavier filters use optimized native code, and all audio filters can be heard during preview.
- Load ordinary native Linux VirtualDub video-filter plug-ins. Plug-in settings are kept in projects and processing-setting files.
- Use Direct Stream Copy, Fast Recompress, Normal Recompress, and Full Processing modes.
- Choose from the video and audio encoders installed with FFmpeg, including bitrate, quality, and supported two-pass settings.
- Save AVI, MKV, MP4, MOV, WebM, and NUT files, plus segmented AVI, raw video, image sequences, animated GIF/APNG, Adobe Filmstrip, processed audio, and original compressed audio.
- Use external encoder sets and the local Linux frame server.
- Open image sequences, raw video, AviSynth scripts, and supported VapourSynth scripts.
- Run common VirtualDub Sylia/VCF scripts, use the script editor, or run exports and analysis from the command line.
- Save VirtualDubQT projects and settings, use the Batch Wizard and Job Control, and recover an editing session after an abnormal shutdown.
- Record from Linux V4L2 and ALSA devices with live preview, audio level, dropped-frame counts, device controls, timed stopping, and split capture files.
- Inspect histograms, audio waveforms, decode/filter speed, media details, RIFF chunks, hexadecimal file data, installed backends, and system information.

## Partly Implemented

- **Smart rendering:** Clean keyframe-aligned ranges and edit lists are copied without re-encoding. A cut inside a compressed group of frames safely falls back to full recompression instead of re-encoding only that small group.
- **VirtualDub plug-ins:** Native Linux, single-input video filters using the classic run callback are supported. Windows DLLs, filters that request future frames, multi-input filters, and VirtualDub input/output plug-ins are not supported.
- **Video filters:** Nearly all single-input built-in filters have native equivalents. The original multi-input Blend Layers and Merge Layers graph, Alias Format metadata-only filter, and floating-point filter path do not fit the current QImage pipeline.
- **Scripts and projects:** VirtualDub-generated Sylia/VCF settings, scalar variables, basic expressions, filter ranges, clipping, and opacity curves work. This is still a safe interpreter rather than the complete Sylia control-flow language. VirtualDubQT uses its own `.vdqproject` project format.
- **Capture:** The useful Linux capture controls are present in a capture dialog, but it is not a line-for-line copy of VirtualDub's Windows capture workspace and depends on the features exposed by the V4L2/ALSA drivers.
- **Batch and jobs:** Local video, audio, raw-video, image-sequence, and analysis jobs work. Shared or remote job queues are not included.
- **External encoders:** Named command templates work, but the original multi-step encoder-set graph is simplified to one external command after a lossless render.
- **Frame server:** The local NUT/FIFO server works with Linux tools, but it cannot use the original Windows-only VirtualDub frame-server protocol.

## Not Implemented

- Windows-only VFW/ACM codecs, DirectShow capture drivers, or Windows VirtualDub plug-in DLLs.
- Specialized Windows file tools such as striped/sparse AVI allocation and the old file-segmentation manager.
- Exact Windows GUI layout and operating-system integration.

---

## Build Instructions

Building from source requires a C++17 compiler, CMake 3.19 or newer, Qt 6,
FFmpeg 6 or newer development libraries, and AviSynth+ headers and runtime.
The optional regression-test presets require CMake 3.21. The 0.1.2 builds were
tested with Qt 6.4/FFmpeg 6.1 on Ubuntu 24.04 and Qt 6.10/FFmpeg 7.1 on Fedora 43.

### Prerequisites (Ubuntu 24.04)

```bash
sudo apt update
sudo apt install build-essential cmake git ninja-build pkg-config ffmpeg \
    qt6-base-dev qt6-multimedia-dev libavcodec-dev libavformat-dev \
    libavutil-dev libavfilter-dev libswscale-dev libswresample-dev
```

### Prerequisites (Fedora 43)

```bash
sudo dnf install gcc-c++ cmake git ninja-build pkgconf-pkg-config \
    qt6-qtbase-devel qt6-qtmultimedia-devel ffmpeg-free ffmpeg-free-devel
```

For either distro, also [build and install AviSynth+](docs/INSTALLATION.md#build-and-install-avisynth).
These commands do not assume an AviSynth development package exists in your
distro's repositories. Other distros need equivalent development packages.
The custom FFmpeg command-line build is a separate step if you need it;
it does not replace the FFmpeg development libraries above.

### Compile and run

Run these commands from the VirtualDubQT source directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel 2
./build/VirtualDubQt
```

To use custom FFmpeg tools, launch the application from a terminal with the
[configured PATH](docs/INSTALLATION.md#use-the-custom-tools-with-virtualdubqt).

### Maintainer Notes

The [maintainer architecture guide](docs/ARCHITECTURE.md) explains the source
layout, ownership, threading, decoding, filtering, exporting, job, script, and
testing flows before you begin changing the code.

---

## License
Licensed under the [GNU General Public License v3.0 (GPLv3)](LICENSE).
Based on the original VirtualDub and VirtualDub2 architectures by Avery Lee, Anton Shekhovtsov, and v0lt.
