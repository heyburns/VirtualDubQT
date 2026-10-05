VirtualDubQT v0.1.2 is a stability and correctness update to the native Linux preview of VirtualDub2.

Highlights:

- More reliable source loading, frame indexing, seeking, and playback timing.
- Correct selected audio streams, edited audio, AviSynth audio padding, and video/audio export timing.
- Safer exports, temporary-file handling, project recovery, and durable job queues.
- Improved high-bit-depth filters, color handling, sample aspect ratios, and temporal interpolation.
- Qt 6.4 compatibility fixes and expanded isolated Release/sanitizer regressions.
- AppImage includes FFmpeg/ffprobe with AviSynth support for script Fast Recompress.

Downloads:

- `VirtualDubQT-0.1.2-x86_64.AppImage`: 64-bit Linux AppImage built on Ubuntu 24.04.
- `virtualdubqt_0.1.2_amd64.deb`: package for Ubuntu 24.04 and compatible systems.
- `virtualdubqt-0.1.2-1.x86_64.rpm`: package for Fedora 43 and compatible systems.
- `SHA256SUMS`: SHA-256 checksums for the three packages.

This remains an early preview. Physical capture/audio hardware and arbitrary external plugin combinations are not covered by the automated checks. See `docs/AUDIT_STATUS.txt` for remaining work.

Native DEB/RPM packages install the distribution's FFmpeg tools. Available codecs and script Fast Recompress depend on that FFmpeg build; AviSynth Fast Recompress requires `--enable-avisynth` and an AviSynth runtime accessible to the FFmpeg command. The AppImage supplies its own matching tools. External AviSynth plugins are not bundled.
