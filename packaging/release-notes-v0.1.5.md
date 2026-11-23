VirtualDubQT v0.1.5 starts, opens ordinary media and exports without requiring
AviSynth+. It also extends the fast timeline navigation introduced in v0.1.4.

## What changed

- Install AviSynth+ only if you open `.avs` scripts. The app loads it when the
  script is opened and reports a setup error if the compatible library is missing.
- Scrubbing and seeking use existing file indexes or headers for more formats,
  including HEVC in MKV, eligible AVI and WMV files, and Sorenson Spark in FLV.
  Files without reliable timing or seek information can still require an index.
- Maintainer comments and the architecture guide now cover playback, export,
  threading, filters, and packaging. The README has an updated screenshot.

## Downloads

| File | Use it for |
| --- | --- |
| `VirtualDubQT-0.1.5-x86_64.AppImage` | Portable x86-64 package with Qt and private FFmpeg. Requires glibc 2.38 or newer and desktop graphics and audio libraries. |
| `virtualdubqt_0.1.5_amd64.deb` | Ubuntu 24.04. |
| `virtualdubqt-0.1.5-1.x86_64.rpm` | Fedora 43. |
| `SHA256SUMS` | Verify the downloads. |
| `VirtualDubQT-0.1.5-ffmpeg-sdk-x86_64.tar.xz` | Build the app with the matching backend and headers. |
| `VirtualDubQT-0.1.5-third-party-sources.tar.xz` | Third-party sources and the FFmpeg build configuration. |

All three packages use bundled FFmpeg 6.1.6 and ffprobe. Install AviSynth+ for
`.avs` scripts or VapourSynth for compatible `.vpy` scripts and install any
plugins your scripts use separately. See the [installation guide](https://github.com/heyburns/VirtualDubQT/blob/v0.1.5/docs/INSTALLATION.md).

This is an early preview. Package checks cover generated media and script
fixtures. Physical capture hardware and third-party plugins vary by system.
