# VirtualDubQt (v0.1)

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
