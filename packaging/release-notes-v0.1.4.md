VirtualDubQT v0.1.4 makes opening, scrubbing and navigating ordinary MP4/MOV files much more responsive.

Highlights:

- Scrubbing no longer requires decoding the entire video first when the file contains reliable frame information. The app uses that information and decodes only the nearby pictures needed to reach the selected frame, following Windows VirtualDub2's approach.
- Playback from the middle, keyframe buttons, editing, exports and frame serving use the same frame information. Frame selection and variable-rate timing remain accurate.
- Preview and export share the frame map, avoiding repeated work. Closing or replacing a source releases the shared index without allocating a replacement copy.
- Files with incomplete, unusual or contradictory frame information retain the verified decoding fallback. Explicit error scans still inspect the whole video.
- Added coverage for H.264, HEVC, AV1, VP9, ProRes, variable-rate footage, rapid scrubbing, audio boundaries and malformed MP4 metadata.

Downloads:

- `VirtualDubQT-0.1.4-x86_64.AppImage`: portable x86_64 package built on Ubuntu 24.04, including Qt and the private FFmpeg backend.
- `virtualdubqt_0.1.4_amd64.deb`: Ubuntu 24.04 package.
- `virtualdubqt-0.1.4-1.x86_64.rpm`: Fedora 43 package.
- `SHA256SUMS`: checksums for the release downloads.
- `VirtualDubQT-0.1.4-ffmpeg-sdk-x86_64.tar.xz`: matching backend and headers for building the app without compiling FFmpeg.
- `VirtualDubQT-0.1.4-third-party-sources.tar.xz`: matching dependency sources and FFmpeg build configuration; license notices are also included in the packages.

The packages continue to use their private FFmpeg 6.1.6 backend and never fall back to system FFmpeg. **AviSynth+ 3.7.5 must be installed separately before starting the app**, even for ordinary video files. AviSynth, VapourSynth, Python and script plugins are not bundled. VapourSynth is needed only for `.vpy` input. The AppImage needs glibc 2.38 or newer and normal desktop graphics/audio libraries. See the [installation guide](https://github.com/heyburns/VirtualDubQT/blob/v0.1.4/docs/INSTALLATION.md).

This remains an early preview. Package checks use generated media and separately supplied script frameworks; they do not cover physical capture/audio hardware or every third-party plugin combination.
