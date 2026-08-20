# VirtualDubQT (v0.1)

<p align="center">
  <img src="https://raw.githubusercontent.com/heyburns/VirtualDubQT/main/docs/screenshot.png" alt="VirtualDubQt Screenshot" width="800">
</p>

> **THIS IS INCOMPLETE AND BUGGY, SO USE AT YOUR OWN RISK. IF YOU FIND A BUG, PLEASE SUBMIT A REPORT AND/OR A PULL REQUEST.**

VirtualDub is one of those indispensible video editing apps that simply has no equivalent in the Linux ecosystem. **VirtualDubQT** is an attempt to create a modern 64-bit native Linux port of **VirtualDub2**, rewritten in clean C++17 and Qt6. It brings VirtualDub's utility to Linux desktop platforms without the performance overhead of using virtualization or wine.

---

### Important Requirements & Codec Dependencies

* **FFmpeg Dependency**: VirtualDubQT relies almost exclusively on **FFmpeg** (`libavcodec`, `libavformat`, `libavutil`, `libavfilter`, `libswscale`, `libswresample`) for opening, processing, and saving media. **Any video or audio codecs you plan on using (such as `libx264`, `libx265`, `libmp3lame`, `libopus`, `libvorbis`, or ProRes) must be included in your system's FFmpeg installation.**
* **AviSynth+ Script Support**: If you plan to import and process **AviSynth (`.avs`) scripts**, your FFmpeg build **must** be compiled with AviSynth support enabled (`--enable-avisynth`), and native **[AviSynth+ for Linux](https://github.com/AviSynth/AviSynthPlus)** (`libavisynth`) must be compiled and installed on your Linux system. For some native Linux-compatible AviSynth plugins and filters (such as MaskTools2, RgTools, NNEDI3, TIVTC, etc.), refer to **[pinterf's ported plugin repositories](https://github.com/pinterf)**.
* **VapourSynth Script Support**: Opening **VapourSynth (`.vpy`) scripts** is available when the installed FFmpeg libraries and command-line executable provide the `vapoursynth` input module. If they do not, VirtualDubQT reports that dependency explicitly instead of treating the script as ordinary media.

---

## What Works

- Open common video files, play them, scrub the timeline, step through frames, and move between keyframes.
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

### Prerequisites (Ubuntu / Debian / Linux Mint)

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-multimedia-dev \
    libavcodec-dev libavformat-dev libavutil-dev libavfilter-dev libswscale-dev libswresample-dev \
    libavisynth-dev yasm
```

### Prerequisites (Arch Linux / Manjaro)

```bash
sudo pacman -S base-devel cmake qt6-base qt6-multimedia ffmpeg avisynthplus yasm
```

### Prerequisites (Fedora / RHEL)

```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qtmultimedia-devel \
    ffmpeg-free-devel avisynthplus-devel yasm
```

### Compiling

```bash
mkdir -p build && cd build
cmake ..
cmake --build . -j$(nproc)
```

### Running

```bash
./VirtualDubQt
```

---

## License
Licensed under the [GNU General Public License v3.0 (GPLv3)](LICENSE).
Based on the original VirtualDub and VirtualDub2 architectures by Avery Lee, Anton Shekhovtsov, and v0lt.
