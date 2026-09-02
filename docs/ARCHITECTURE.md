# VirtualDubQT Maintainer Architecture Guide

This document is the map for the native Linux application. Detailed ownership,
threading, and algorithm notes live beside the relevant classes and hot paths in
the `VDQt*` source files.

## Repository boundaries

`CMakeLists.txt` explicitly lists every source compiled into `VirtualDubQt`.
Those maintained native modules live in `src/VirtualDub/VDQt*` plus
`src/main.cpp`.

The repository also retains substantial original VirtualDub/VirtualDub2 source
under directories such as `src/VirtualDub/source`, `src/Kasumi`, and `src/system`.
That code is useful as a behavior and ABI reference, but it is not silently
globbed into the Linux application. Do not assume a change there affects the
binary; consult the explicit CMake target first.

`VDQtApplicationIcon.cpp` is generated embedded image data. Its payload should
not be manually reformatted or annotated line by line.

## Process and thread model

The Qt GUI thread owns `VDQtMainWindow`, visible widgets, the authoritative
`VDQtVideoDecoder`, `VDQtAudioPlayer`, queue controller, and frame-server
controller.

Interactive frame decoding happens on a dedicated `QThread`:

1. The GUI calls `VDQtFrameDecodeWorker::requestFrame()`.
2. The request is copied into one mutex-protected latest-request slot.
3. The worker decodes on its own thread and optionally runs its private filter
   chain snapshot.
4. The result carries a generation number back to the GUI.
5. The GUI discards results whose generation no longer matches the current
   request.

This latest-request-wins design is what keeps scrubbing from accumulating a long
queue of obsolete random seeks.

Ordinary media gets a second decoder owned by the worker. Native AviSynth is an
important exception: video preview and audio share the authoritative decoder's
single script environment and recursive mutex. Some third-party AviSynth
plug-ins keep process-global state and cannot safely be evaluated twice or
entered concurrently. Always close the worker and audio pipeline synchronously
before destroying or replacing that shared environment.

Audio has its own bounded decode-ahead producer thread. `QAudioSink` pulls only
already-converted PCM from a `QIODevice`; codec, resampler, and AviSynth graph
work must never migrate into the real-time audio callback.

The local frame server and some export subprocess pipelines use additional
worker threads or child `ffmpeg` processes. Their owners synchronously cancel
and join them during teardown.

## Source-open lifecycle

`VDQtMainWindow::openVideoFile()` is the session-level entry point:

1. Stop playback and synchronously close interactive/audio consumers.
2. Close the authoritative prior decoder.
3. Open the new source transactionally in `VDQtVideoDecoder`.
4. Open or share the interactive decoder.
5. Select/open audio.
6. Reset the edit timeline to the source's current known length.
7. Schedule the first asynchronous preview frame.

FFmpeg container frame counts may be absent or wrong. `FrameCountStatus`
distinguishes exact, estimated, and unknown lengths. Playback can extend a
provisional identity timeline as frames are discovered; operations requiring a
definite end scan to decoder EOF first.

Image sequences and appended media are represented by temporary FFconcat
manifests. Raw input is materialized to a temporary NUT file after the user
supplies dimensions and pixel layout. `mTimelineTempDirectory` owns these files
for the life of the editing session.

## Video decode and conversion

`VDQtVideoDecoder` owns one demuxer, codec context, packet, decoded frame,
conversion frame, swscale context, presentation index, and memory-bounded image
cache.

Random access seeks to a known preceding keyframe where possible, flushes codec
state, and decodes dependency frames forward. Sequential playback avoids random
seeks even when presentation drops a late frame. Best-effort timestamps populate
the presentation-order index and provide VFR frame durations.

FFmpeg/AviSynth frames are converted to packed RGB QImages. Optimized swscale
paths may finish rows with full SIMD stores, so conversion storage uses a
64-byte-aligned stride plus explicit tail padding. The allocation owner and the
aligned pixel pointer are intentionally different. Never replace this with an
exact `width * bytesPerPixel * height` allocation.

## Timeline model

`VDQtTimeline` is a non-destructive edit list. A segment references a half-open
source frame range; no segment owns decoded data. Appended files are already
flattened by their concat input.

All selection/edit ranges use `[start, end)` semantics. Masked segments retain
duration while holding the preceding visible frame. Every edit validates bounds,
normalizes adjacent compatible segments, and stores a bounded undo snapshot.

Exports receive a copy of the timeline segments. An empty segment list in export
options means identity mapping for backward compatibility.

## Filter pipelines

`VDQtFilterSystem::instance()` is the editable session chain. Preview workers,
frame servers, and other concurrent consumers take independent transient
snapshots. This matters because temporal histories, asset caches, LUTs, and VDX
plug-in instances are mutable and generally not thread-safe.

`processFrameSequence()` is the authoritative API. Rate-changing filters such as
bob deinterlacing may emit multiple temporal phases for one source image.
`processFrame()` exists for older single-image callers and returns only phase
zero.

Reserved `_sylia.*` numeric parameters preserve VirtualDub script range,
clipping, and opacity metadata without changing the public filter-instance
structure. Do not expose those reserved keys as ordinary user parameters.

## Audio pipeline

`VDQtAudioPlayer` selects a stream and builds one of these pull pipelines:

- FFmpeg source -> FFmpeg decode-ahead device -> optional audio filters -> Qt
  audio sink.
- Native AviSynth clip -> AVS decode-ahead device -> optional audio filters ->
  Qt audio sink.

The decoder cursor includes buffered read-ahead. A/V synchronization must use
the sink's presented playback time instead. Seeking stops and joins the producer,
flushes decoder/filter history, establishes a new sample origin, primes the
buffer, and then resumes.

Offline audio export uses the same conversion rules but writes transactionally
through a staged file. Small timestamp jitter is smoothed; genuine gaps remain
silence.

## Export modes

`VDQtVideoExporter` is the common implementation behind interactive saves,
automation, jobs, and several specialized front ends.

- Direct Stream Copy remuxes compressed packets when edits/ranges permit it.
- Fast Recompress keeps video in an FFmpeg-native planar pipeline and bypasses
  the QImage filter chain.
- Normal Recompress decodes/re-encodes without applying the session video
  filters.
- Full Processing maps the timeline, decodes QImages, runs the filter chain, and
  encodes the resulting images.

Arbitrary edit lists require frame-accurate rendering unless smart rendering can
prove a clean copyable range. Two-pass output first creates a replayable lossless
intermediate because a streaming pipe cannot be consumed twice.

Exports stage output beside the destination, recheck source alias safety after
rendering, and atomically rename only on success. Preserve that ordering: source
files must survive cancellation, encoder failure, script ambiguity, and path
changes during a long render.

## Persistent and session state

`VDQtProcessingState` contains codec, filter, and processing choices.
`VDQtProjectState` adds sources, position, timeline, selection, and markers.
`VDQtJobState` adds durable queue status and output instructions.
`VDQtProjectFile` is the versioned JSON boundary for all three.

Loaders parse into temporary values and commit only after full validation.
Writers use `QSaveFile` so a crash cannot leave a half-written project or queue.

Codec/filter/decompression choices intentionally persist only while the
application is open. Recent-file and window UI history may use `QSettings`.
Recovery snapshots and queue autosaves are explicit files, not hidden processing
preferences.

## Jobs and batch

The Batch Wizard turns input rows plus a processing template into independent
`VDQtJobState` records. `VDQtJobQueue` validates records, owns status transitions,
emits model notifications, and debounces autosave. It does not encode.

`VDQtMainWindow::runPendingJobs()` executes one job at a time using the same
validated export paths as interactive commands. The Job Control window is only a
model/view controller over that queue. Unattended execution returns errors into
job state instead of opening blocking message boxes.

## Scripts

`VDQtScriptEngine` parses a bounded, command-oriented subset of Sylia/VCF into
inert `VDQtScriptCommand` values. It supports scalar values and basic expressions
needed by generated settings but deliberately has no arbitrary function calls,
loops, filesystem access, or native evaluation.

`VDQtMainWindow::executeAutomationProgram()` is the execution boundary. New
commands should be parsed generically, validated at execution, and routed through
the same application methods as their interactive equivalents.

## Plug-ins

`VDQtPluginHost` supports Linux-native modules exposing the legacy VDX video
filter entry points. Windows DLLs cannot be loaded into this process. Each active
filter ID owns a runtime instance, and module lifetime is retained until all
callbacks/instances finish.

Legacy filters may assume padded/aligned XRGB storage and mutable state. The host
adapts QImages through aligned buffers, serializes instance callbacks, and runs
ABI teardown in the required order.

## Testing and change checklist

The CTest targets intentionally divide concerns:

- `stabilization_tests`: broad media/workflow regressions.
- `playback_performance_tests`: sequential decode/seek behavior.
- `audio_backend_tests`: optional physical Qt audio output smoke test.
- `filter_performance_tests`: heavy-filter performance guardrails.
- `plugin_host_tests`: real shared-module VDX lifecycle.
- `script_engine_tests`: accepted/rejected parser grammar.
- `parity_contract_tests`: stable catalogs and reference behavior.
- `generated_vcf_execution_test`: full-process unattended script smoke test.

For decoder/audio/lifetime changes, run both the release suite and the sanitizer
stabilization suite. LeakSanitizer may need to be disabled in ptraced/container
environments, but AddressSanitizer and UndefinedBehaviorSanitizer should remain
enabled.
