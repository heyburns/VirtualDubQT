# VirtualDubQT (v0.1)

<p align="center">
  <img src="https://raw.githubusercontent.com/heyburns/VirtualDubQT/main/docs/screenshot.png" alt="VirtualDubQt Screenshot" width="800">
</p>

> **THIS IS INCOMPLETE AND BUGGY, SO USE AT YOUR OWN RISK. IF YOU FIND A BUG, PLEASE SUBMIT A REPORT AND/OR A PULL REQUEST.**

VirtualDub is one of those indispensible video editing apps that simply has no equivalent in the Linux ecosystem. **VirtualDubQT** is an attempt to create a modern 64-bit native Linux port of **VirtualDub2**, rewritten in clean C++17 and Qt6. It brings VirtualDub's video filtration, AviSynth+ script hosting, frame-accurate timeline manipulation, and multiformat audio/video transcoding pipelines to modern desktop platforms without the performance overhead of using virtualization or wine.

---

### Important Requirements & Codec Dependencies

* **FFmpeg Dependency**: VirtualDubQT relies almost exclusively on **FFmpeg** (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`) for container demuxing, video/audio stream decoding, and encoding. **Any video or audio codecs you plan on using (e.g. `libx264`, `libx265`, `libmp3lame`, `libopus`, `libvorbis`, `prores`, etc.) must be enabled and compiled into your system's FFmpeg installation.**
* **AviSynth+ Script Support**: If you plan to import and process **AviSynth (`.avs`) scripts**, your FFmpeg build **must** be compiled with AviSynth support enabled (`--enable-avisynth`), and native **[AviSynth+ for Linux](https://github.com/AviSynth/AviSynthPlus)** (`libavisynth`) must be compiled and installed on your Linux system. For some native Linux-compatible AviSynth plugins and filters (such as MaskTools2, RgTools, NNEDI3, TIVTC, etc.), refer to **[pinterf's ported plugin repositories](https://github.com/pinterf)**.
* **VapourSynth Script Support**: Opening **VapourSynth (`.vpy`) scripts** is available when the installed FFmpeg libraries and command-line executable provide the `vapoursynth` input module. If they do not, VirtualDubQT reports that dependency explicitly instead of treating the script as ordinary media.

---

## Features

* **Native Modern UI (Qt6)**:
  * High DPI, dark theme, responsive dual-pane (Input / Output) video viewport.
  * Direct clipboard capture, frame stepping, timeline selection markers, and live zoom/panning.
* **Modern Video & Container Pipeline**:
  * Direct decoding and encoding of H.264 (`libx264`), HEVC (`libx265`), Apple ProRes (`prores_ks`), VP9, VP8, FFV1, HuffYUV, CineForm, UtVideo, and Uncompressed RGB/YUV.
  * Native export to MP4, MKV, WebM, MOV, NUT, and AVI containers with customizable FastStart and rate control modes (CRF, CBR/VBR, Lossless).
  * Headerless raw-video export with selectable RGB/YUV layouts, bit depth, scanline alignment, chroma-plane order, and vertical orientation.
  * Animated GIF and image-sequence export, container text metadata, VFR/null-frame timing preservation, and optional gap collapse for video-only recompression.
  * Image-sequence and headerless raw-video input with explicit frame rate, pixel layout, geometry, and header-byte offset.
  * Conservative smart rendering: clean GOP-aligned selections are stream copied; selections requiring exact cuts, retiming, audio processing, or filters safely use the selected recompression mode.
* **High-Fidelity Audio Compression & Processing**:
  * Audio extraction and transcode support for AAC, MP3 (`libmp3lame`), Opus (`libopus`), Ogg Vorbis (`libvorbis`), Dolby Digital AC-3 (`ac3`), FLAC (levels 0–8), and raw PCM.
  * Dynamic downsampling, channel conversion, and constrained VBR / hard CBR modes.
  * Selectable embedded streams, native AviSynth audio, external audio files, or explicit video-only output.
  * Ordered audio filters for gain, low/high-pass filtering, resampling, channel mixing, pitch shift, time stretch, center cut/mix, and chorus; the same graph is used by processed exports.
* **Native AviSynth+ Integration**:
  * Direct `.avs` script hosting through native AviSynth+, including planar/interleaved pixel-format negotiation.
  * Optional `.vpy` hosting through FFmpeg's VapourSynth input module.
* **Video Filter Engine**:
  * Session-based filter chain with 6-axis color correction, bob doubling, box blur, brightness/contrast, horizontal and vertical flip, grayscale, invert, resize, rotate, sharpen, deinterlace, emboss, field swap, HSV, levels, threshold, posterize, gamma, spatial smoothing, crop, chroma shift, and pixelation.
  * Parallel scanline/block processing, a cached interpolated 6-axis color LUT, and separable sliding-window blur keep heavy Play Preview chains responsive while retaining 16-bit image precision.
* **Editing Sessions and Automation**:
  * Non-destructive frame edit lists with cut/copy/paste/delete/crop, undo/redo, selection navigation, scene-change search, and frame-accurate audio/video rendering of reordered or repeated ranges.
  * Strictly validated append-segment timelines with source-overwrite protection and multi-segment project round-tripping.
  * Versioned `.vdqproject` project files and `.vdqsettings` processing snapshots, both using atomic writes and relative media paths where possible.
  * Session job control, batch job creation, and portable `.vdqjobs` scripts with per-job processing snapshots, retry, cancellation, and atomic destination replacement.
  * Filtered audio/video frame serving through a temporary RGB/PCM NUT named pipe for local Linux applications.
  * Linux V4L2/ALSA capture with staged output, plus runtime FFmpeg codec/filter/device and native AviSynth plugin catalogs.
  * Codec, filter, decoder, raw-export, smart-render, and preference choices remain live for the application session and reset on the next launch unless explicitly saved to a project/settings file.

---

## Build Instructions

### Prerequisites (Ubuntu / Debian / Linux Mint)
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-multimedia-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
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
