# VirtualDubQT (v0.1)

<p align="center">
  <img src="https://raw.githubusercontent.com/heyburns/VirtualDubQT/main/docs/screenshot.png" alt="VirtualDubQt Screenshot" width="800">
</p>

> **THIS IS INCOMPLETE AND BUGGY, SO USE AT YOUR OWN RISK. IF YOU FIND A BUG, PLEASE SUBMIT A REPORT AND/OR A PULL REQUEST.**

VirtualDub is one of those indispensible video editing apps that simply has no equivalent in the Linux ecosystem. **VirtualDubQT** is an attempt to create a modern 64-bit native Linux port of **VirtualDub2**, rewritten in clean C++17 and Qt6. It brings VirtualDub's utility to Linux desktop platforms without the performance overhead of using virtualization or wine.

---

### Important Requirements & Codec Dependencies

* **FFmpeg Dependency**: VirtualDubQT relies almost exclusively on **FFmpeg** (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`) for container demuxing, video/audio stream decoding, and encoding. **Any video or audio codecs you plan on using (e.g. `libx264`, `libx265`, `libmp3lame`, `libopus`, `libvorbis`, `prores`, etc.) must be enabled and compiled into your system's FFmpeg installation.**
* **AviSynth+ Script Support**: If you plan to import and process **AviSynth (`.avs`) scripts**, your FFmpeg build **must** be compiled with AviSynth support enabled (`--enable-avisynth`), and native **[AviSynth+ for Linux](https://github.com/AviSynth/AviSynthPlus)** (`libavisynth`) must be compiled and installed on your Linux system. For some native Linux-compatible AviSynth plugins and filters (such as MaskTools2, RgTools, NNEDI3, TIVTC, etc.), refer to **[pinterf's ported plugin repositories](https://github.com/pinterf)**.
* **VapourSynth Script Support**: Opening **VapourSynth (`.vpy`) scripts** is available when the installed FFmpeg libraries and command-line executable provide the `vapoursynth` input module. If they do not, VirtualDubQT reports that dependency explicitly instead of treating the script as ordinary media.

---

## What Works

- Open common video files, play them, scrub the timeline, step through frames, and move between keyframes.
- Mark a range and cut, copy, paste, delete, crop, undo, redo, or append compatible video segments.
- View the original and filtered video side by side and use Play Preview.
- Use 23 built-in video filters and 10 audio filters.
- Use Direct Stream Copy, Fast Recompress, Normal Recompress, and Full Processing modes.
- Save AVI, MKV, MP4, MOV, WebM, and NUT files, plus raw video, image sequences, animated GIFs, and processed audio.
- Open image sequences, raw video, AviSynth scripts, and supported VapourSynth scripts.
- Save VirtualDubQT projects and settings, and create batch jobs through Job Control.
- Use a basic local frame server and record from Linux video and audio devices.

## Partly Implemented

- **Play Preview:** Normal video and audio preview work, but variable frame-rate timing, frame-rate changes, bob-doubled output, and some audio filters may not play exactly as they will appear in the final export.
- **Video filters:** The most common filters are present, but many VirtualDub2 filters are still missing. Third-party VirtualDub filters cannot be loaded.
- **Smart rendering:** A clean range can be copied without re-encoding. More complicated cuts or edits re-encode the full range instead of only the frames around the cut.
- **Capture:** Basic recording works, but there is no full capture workspace with live meters, dropped-frame tools, device controls, timed stops, or disk-spanning capture.
- **Codecs and color formats:** A useful set is available, but the program does not expose every choice installed on the system or all of VirtualDub2's advanced options.
- **Audio:** Processed audio export works, but direct extraction of the original compressed audio, detailed audio timing controls, and a full audio routing graph are not available.
- **Batch and jobs:** Video, audio, raw video, image sequence, and analysis jobs work. Folder scanning, remote queues, and arbitrary scripted jobs are not included.
- **Frame server:** The local Linux frame server works, but it is not compatible with the original Windows VirtualDub frame server.
- **Video analysis:** The current command scans for damaged or unreadable frames. It does not yet run the full processing analysis pass from VirtualDub2.

## Not Implemented

- Windows VirtualDub plugins, video codecs, and audio codecs.
- VirtualDub `.vdscript` automation, the script editor, and the original command-line batch commands.
- Opening or saving original VirtualDub project and settings files.
- Two-pass video encoding, external encoder sets, and VirtualDub output plugins.
- Segmented or striped AVI output, animated PNG, and filmstrip export.
- Advanced timeline tools such as masks, general markers, waveform display, and timeline zoom ranges.
- The original histogram, profiling, RIFF inspection, hex viewer, sparse AVI, and file-management tools.
- Automatic recovery of an unsaved editing session after a crash. Job queues are saved automatically.

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
