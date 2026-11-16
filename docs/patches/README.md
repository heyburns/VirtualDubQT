# Dependency patches

`lsmashworks-audio-only-index.patch` fixes an invalid read in L-SMASH-Works
when `LWLibavAudioSource` reads an existing index without loading the video
frame records that the downstream index setup still expects. The fault can
appear when an AviSynth script is opened again in the same process. Apply it at
the root of the L-SMASH-Works source tree before building
`libLSMASHSource.so`:

```sh
patch -p1 -i /path/to/lsmashworks-audio-only-index.patch
```

The patch is based on L-SMASH-Works commit
`6ae441a195dc65fa201c228c7ac6143bfdbedb0e`.

## AviSynth Prefetch performance

These are patches for separately installed dependencies, not application
features or bundled frameworks. Neither the app nor its FFmpeg bundle silently
rewrites scripts, removes Prefetch, or forces unsafe plugin threading modes.
Restart the application after replacing either dependency. Keep copies of both
previous libraries so the pair can be restored together.

### Ordered L-SMASH source cache

`lsmashworks-prefetch-cache.patch` applies to the same L-SMASH-Works revision
above, **in addition to** the audio-only-index crash fix. Apply both:

```sh
git apply /path/to/VirtualDub_Port/docs/patches/lsmashworks-audio-only-index.patch
git apply /path/to/VirtualDub_Port/docs/patches/lsmashworks-prefetch-cache.patch
```

Prefetch workers request frames out of order. The source decoder's serialization
prevents simultaneous access, but does not order those requests. Repeated small
backward requests can therefore seek and decode an entire GOP again. The patch
retains returned AviSynth frames and fills short forward gaps in order, leaving
the decoder's high-water mark unchanged on cache hits. Frame zero is retained
separately because some motion filters request it for metadata while processing
much later frames. Real seeks rebase a bounded look-behind window; they do not
decode the entire distance from the previous position.

The cache activates with a multithreaded filter chain. Its default budget is
256 MiB per video source, including frame zero; it also has an entry cap of
128 frames. Accounting uses actual frame-buffer allocation sizes. The optional
last argument `prefetch_cache` is a MiB limit (0 disables the cache, maximum
4096 subject to address-space limits). An undersized budget falls back to the
original decode path. Decoder threading safety and frame properties are retained.

For a shared-library Linux build, use the host's matching FFmpeg development
packages and the separately installed AviSynth SDK. Example CMake settings:

```sh
cmake -S "$LS_SOURCE" -B "$LS_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_VS_PLUGIN=OFF \
  -DENABLE_DAV1D=OFF -DENABLE_MFX=OFF -DENABLE_VPX=OFF \
  -DENABLE_VULKAN=OFF -DZLIB_USE_STATIC_LIBS=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_C_VISIBILITY_PRESET=hidden -DCMAKE_CXX_VISIBILITY_PRESET=hidden \
  -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON \
  -DCMAKE_CXX_FLAGS="-I$AVS_PREFIX/include/avisynth"
cmake --build "$LS_BUILD" --parallel 6
```

Initialize the upstream `l-smash`, `xxHash` and `obuparse` submodules first.
Those optional dependency-link switches do not remove decoders from a shared
FFmpeg build: available decoders still come from the FFmpeg libraries used by
the plugin. Hidden symbol visibility is important when a native application and
an external plugin load different FFmpeg major versions into one process.

### ResampleAudio: lock audio, not the upstream video graph

`avisynth-resample-audio-mt.patch` targets [AviSynth+
commit de1d1fc0](https://github.com/AviSynth/AviSynthPlus/commit/de1d1fc0d69e6d308235491b24b8e9b26ac023ff)
(3.7.5 development version, interface 12). Its original `MT_SERIALIZED`
declaration locks the entire upstream video computation even though the filter
only changes audio. Putting ResampleAudio after a heavy video chain therefore
prevents Prefetch from parallelizing that chain.

The patch uses a private audio mutex, including the no-rate-change path and its
directly owned sample-format converter. Video forwarding remains concurrent.
Global interpolation tables initialize once, safely across instances. Retained
audio input is moved with `memmove`, not overlapping `memcpy`. Audio resampling
and its output format are otherwise unchanged.

```sh
git -C "$AVS_SOURCE" apply /path/to/VirtualDub_Port/docs/patches/avisynth-resample-audio-mt.patch
cmake -S "$AVS_SOURCE" -B "$AVS_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_PLUGINS=OFF \
  -DCMAKE_INSTALL_PREFIX="$AVS_PREFIX" -DCMAKE_INSTALL_LIBDIR=lib64 \
  -DCORE_PLUGIN_INSTALL_PATH="$AVS_PREFIX/lib64"
cmake --build "$AVS_BUILD" --parallel 6
```

Set the plugin path explicitly, even when reconfiguring an existing build:
upstream caches it independently of the installation prefix. Stage and test the
new core library before replacing an installed one. Installing the SDK or
rebuilding other filters is not required for this compatible interface-12 fix.

### Linux memory allowance across repeated script loads

`avisynth-linux-memory.patch` applies to the same AviSynth revision, alongside
the resampler patch. Apply it before the build command above:

```sh
git -C "$AVS_SOURCE" apply /path/to/VirtualDub_Port/docs/patches/avisynth-linux-memory.patch
```

The previous Linux memory check uses `sysconf(_SC_AVPHYS_PAGES)`: completely
unused RAM, excluding reclaimable disk cache. On a host with about 20 GiB
available, repeated MP4/AVS loads reduced the cache allowance from 2128 MiB to
626 MiB. The filters then repeatedly recomputed intermediate frames, increasing
CPU work and frame-buffer allocations. A larger SetMemoryMax request is not a
reliable workaround because it passes through the same erroneous limit.

Use Linux's `MemAvailable` estimate from `/proc/meminfo` instead. Preserve the
previous sysconf fallback for missing/unsupported proc data, validate units and
integer ranges, and prevent unsigned underflow when subtracting the system
reserve on low-memory machines. The normal default 4 GiB maximum and explicit
smaller user requests remain in force; this is not an unconditional allocation
or an increase of the configured maximum. Other platforms keep their existing
available-memory query. Container-specific memory quotas are not added here.

### Optional dependency regression tests

These tests are separate from the normal app suite and require the matching
dependency sources/builds. They do not depend on user media or installed plugins.
Compile from the VirtualDub_Port root (variables identify the trees above):

```sh
c++ -O2 -std=c++17 -fvisibility=hidden \
  -I"$AVS_PREFIX/include/avisynth" -I"$AVS_SOURCE/avs_core" \
  -I"$AVS_BUILD/avs_core" tests/dependencies/test_avisynth_resample_mt.cpp \
  -L"$AVS_BUILD/avs_core" -Wl,-rpath,"$AVS_BUILD/avs_core" \
  -lavisynth -pthread -o "$AVS_BUILD/test-resample-mt"
"$AVS_BUILD/test-resample-mt"
c++ -O2 -std=c++17 -I"$AVS_SOURCE/avs_core" \
  tests/dependencies/test_avisynth_linux_memory.cpp -o "$AVS_BUILD/test-linux-memory"
"$AVS_BUILD/test-linux-memory"
c++ -O2 -std=c++17 -fvisibility=hidden -I"$AVS_PREFIX/include/avisynth" \
  tests/dependencies/test_lsmash_prefetch.cpp \
  -L"$AVS_BUILD/avs_core" -Wl,-rpath,"$AVS_BUILD/avs_core" \
  -lavisynth -pthread -o "$AVS_BUILD/test-lsmash-prefetch"
python3 tests/dependencies/check_avisynth_prefetch.py \
  --ffmpeg /path/to/ffmpeg-runtime/bin/ffmpeg \
  --plugin "$LS_BUILD/libLSMASHSource.so" \
  --probe "$AVS_BUILD/test-lsmash-prefetch" --library-path "$AVS_BUILD/avs_core"
```

The direct resampler test bypasses outer caches/MT guards: audio calls must stay
serialized while video calls overlap, with exact PCM matching a reference.
It covers float/S16 upsampling, downsampling, no-rate-change, direct sample-format
conversion and overlapping audio windows. The L-SMASH suite generates fixtures
and compares every visible byte, source frame properties and audio samples with
serial decoding. It covers 8/10/16-bit, alpha, RGB, grayscale, direct rendering,
frame-rate conversion, forward/reverse/random seeks, retained frames, and small
or disabled cache budgets. All fixture files and indexes live in a temporary
directory. Hardware decoding and arbitrary third-party filter safety are not
implied by these software tests.
The Linux memory helper test covers reclaimable cache, missing/malformed proc
data, integer overflow, zero available memory, reserve subtraction and explicit
small requests without changing host memory pressure.
