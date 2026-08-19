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

---

## Features

* **Native Modern UI (Qt6)**:
  * High DPI, dark theme, responsive dual-pane (Input / Output) video viewport.
  * Direct clipboard capture, frame stepping, timeline selection markers, and live zoom/panning.
* **Modern Video & Container Pipeline**:
  * Direct decoding and encoding of H.264 (`libx264`), HEVC (`libx265`), Apple ProRes (`prores_ks`), VP9, VP8, FFV1, HuffYUV, CineForm, UtVideo, and Uncompressed RGB/YUV.
  * Native export to MP4, MKV, WebM, MOV, NUT, and AVI containers with customizable FastStart and rate control modes (CRF, CBR/VBR, Lossless).
* **High-Fidelity Audio Compression & Processing**:
  * Audio extraction and transcode support for AAC, MP3 (`libmp3lame`), Opus (`libopus`), Ogg Vorbis (`libvorbis`), Dolby Digital AC-3 (`ac3`), FLAC (levels 0–8), and raw PCM.
  * Dynamic downsampling, channel conversion, and constrained VBR / hard CBR modes.
* **Native AviSynth+ Integration**:
  * Direct `.avs` script hosting through native AviSynth+, including planar/interleaved pixel-format negotiation.
* **Video Filter Engine**:
  * Session-based filter chain with 6-axis color correction, bob doubling, box blur, brightness/contrast, horizontal and vertical flip, grayscale, invert, resize, rotate, and sharpen.

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
