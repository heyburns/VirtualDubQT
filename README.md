# VirtualDubQt (v0.1)

> **THIS IS INCOMPLETE AND BUGGY. USE AT YOUR OWN RISK. IF YOU FIND A BUG, PLEASE SUBMIT A REPORT AND/OR A PULL REQUEST.**

---

### Important Requirements & Codec Dependencies

* **FFmpeg Dependency**: VirtualDubQt relies heavily on **FFmpeg** (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`) for container demuxing, video/audio stream decoding, and encoding. **Any video or audio codecs you plan on using (e.g. `libx264`, `libx265`, `libmp3lame`, `libopus`, `libvorbis`, `prores`, etc.) must be enabled and compiled into your system's FFmpeg installation.**
* **AviSynth+ Script Support**: If you plan to import and process **AviSynth (`.avs`) scripts**, your FFmpeg build **must** be compiled with AviSynth support enabled (`--enable-avisynth`), and native **[AviSynth+ for Linux](https://github.com/AviSynth/AviSynthPlus)** (`libavisynth`) must be compiled and installed on your Linux system (note: this requires the native Linux build of AviSynth+, not the Windows version). For native Linux-compatible AviSynth plugins and filters (such as MaskTools2, RgTools, NNEDI3, TIVTC, etc.), refer to **[pinterf's ported plugin repositories](https://github.com/pinterf)**.

---

**VirtualDubQt** is a modern 64-bit native Linux port of **VirtualDub2**, rewritten in clean C++17 and Qt6. It brings VirtualDub's video filtration, AviSynth+ script hosting, frame-accurate timeline manipulation, and multiformat audio/video transcoding pipelines to modern desktop platforms without emulation or Wine.

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
  * Direct script hosting with full syntax-highlighted script editor, live reload, and native planar/interleaved pixel format negotiation.
* **Video Filter Engine**:
  * Native implementation of VirtualDub's filter subsystem, including standard filters (Resize, Brightness/Contrast, Blur, Box Blur, Deinterlace, Field Delay, Invert, Rotate, Sharpen, Threshold, WarpSharp) plus modern neural network filters (NNEDI3).

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
Licensed under the [GNU General Public License v2.0 or later (GPL-2.0-or-later)](LICENSE).
Based on the original VirtualDub and VirtualDub2 architectures by Avery Lee, Anton Shekhovtsov, and v0lt.
