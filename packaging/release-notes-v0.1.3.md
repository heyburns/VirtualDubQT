VirtualDubQT v0.1.3 updates the native Linux preview of VirtualDub2.

Highlights:

- All packages now use the same private FFmpeg 6.1.6 tools and libraries. The app never falls back to system FFmpeg, and reports an error if its bundle is missing or mismatched.
- Expanded software codec support and AviSynth/VapourSynth readers, with the script frameworks and third-party plugins kept separate.
- Fixed a decoder setting that smeared the bottom rows of some MPEG-2 videos, including processed exports.
- Improved compatibility between the private media backend and separately installed script plugins.
- Added dependency patches and regression tests for Linux AviSynth Prefetch, audio resampling, and repeated-load cache performance. These patches are documented for separately installed dependencies; the packages do not replace your frameworks or plugins.

Downloads:

- `VirtualDubQT-0.1.3-x86_64.AppImage`: portable x86_64 package built on Ubuntu 24.04, including Qt and the private FFmpeg backend.
- `virtualdubqt_0.1.3_amd64.deb`: Ubuntu 24.04 package.
- `virtualdubqt-0.1.3-1.x86_64.rpm`: Fedora 43 package.
- `SHA256SUMS`: checksums for the release downloads.
- `VirtualDubQT-0.1.3-ffmpeg-sdk-x86_64.tar.xz`: matching backend and headers for building the app without compiling FFmpeg.
- `VirtualDubQT-0.1.3-third-party-sources.tar.xz`: matching dependency sources and FFmpeg build configuration; license notices are also included in the packages.

Unlike older packages, these do not bundle AviSynth+, VapourSynth, Python, or script plugins. AviSynth+ 3.7.5 must be installed separately before starting the app; VapourSynth is needed only for `.vpy` input. See the installation guide for setup instructions. The AppImage requires glibc 2.38 or newer and normal desktop graphics/audio libraries.

This remains an early preview. Clean-runtime tests cover opening and exporting generated video and AviSynth scripts, not physical capture/audio hardware or every third-party plugin combination.
