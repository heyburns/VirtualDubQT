# VirtualDubQT

<p align="center">
  <img src="https://raw.githubusercontent.com/heyburns/VirtualDubQT/main/docs/screenshot.png" alt="VirtualDubQt Screenshot" width="800">
</p>

> **Early preview:** Some features are incomplete and bugs remain. Please report
> problems through the [issue tracker](https://github.com/heyburns/VirtualDubQT/issues);
> the [guide](docs/INSTALLATION.md#getting-help-and-reporting-bugs) explains what to include.

VirtualDub is one of those indispensable video editing apps that simply has no equivalent in the Linux ecosystem. **VirtualDubQT** is an attempt to create a modern 64-bit native Linux port of **VirtualDub2**, rewritten in clean C++17 and Qt6. It brings VirtualDub's utility to Linux desktop platforms without the performance overhead of using virtualization or wine.

---

## Download and requirements

Start with the [installation guide](docs/INSTALLATION.md): choose a package,
launch the app, and make a first export. It includes copyable commands and
expected results. You do not need to write code to use VirtualDubQT.

Get version 0.1.2 from the [releases page](https://github.com/heyburns/VirtualDubQT/releases).
The **AppImage** includes the main supporting software and is the simplest
starting point. The **DEB** targets Ubuntu 24.04 and the **RPM** targets Fedora 43.
All three are for 64-bit Intel/AMD Linux. The AppImage needs glibc 2.38 or newer;
the guide shows how to check that requirement. Native packages use their distro's
libraries, so a newer distro does not automatically accept the same package.

The project targets reasonably current Linux desktops, roughly the last couple
of years. Package checks currently cover Ubuntu 24.04 and Fedora 43; this is not
a claim that every recent distribution has been tested.

- **FFmpeg** handles media conversion; **ffprobe** inspects media files. The
  AppImage supplies both. Native installs use your system's tools and libraries,
  which must include the encoders you want to use.
- **AviSynth+** runs `.avs` video scripts. VirtualDubQT opens these directly.
  Fast Recompress of a script also needs FFmpeg with AviSynth support. The
  AppImage includes it; native users can [check or build their FFmpeg](docs/BUILDING.md#optional-build-custom-ffmpeg).
- **Extra script plugins** are separate downloads. Follow their Linux setup
  instructions; Windows plugin DLLs are not supported. The guide includes a
  first-use AviSynth example that needs no extra plugins.
- **VapourSynth** `.vpy` scripts need its input module in both the FFmpeg
  libraries and commands. The supplied custom FFmpeg recipe does not enable it.

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

Follow [Building VirtualDubQT](docs/BUILDING.md) for the complete sequence:
install the tools, build AviSynth+, download the app source, compile, and run.
The guide provides commands for Ubuntu 24.04 and Fedora 43 and explains the
common build errors. Programming experience is not required to follow it.

The 0.1.2 builds were tested with Qt 6.4/FFmpeg 6.1 on Ubuntu 24.04 and
Qt 6.10/FFmpeg 7.1 on Fedora 43. Users maintain their own dependencies and
third-party plugins. Reproducible app bugs and unclear or broken instructions
are welcome in the [issue tracker](https://github.com/heyburns/VirtualDubQT/issues);
see [what to include](docs/INSTALLATION.md#getting-help-and-reporting-bugs).

### Maintainer Notes

The [maintainer architecture guide](docs/ARCHITECTURE.md) explains the source
layout, ownership, threading, decoding, filtering, exporting, job, script, and
testing flows before you begin changing the code.

---

## License
Licensed under the [GNU General Public License v3.0 (GPLv3)](LICENSE).
Based on the original VirtualDub and VirtualDub2 architectures by Avery Lee, Anton Shekhovtsov, and v0lt.
