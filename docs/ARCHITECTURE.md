# VirtualDubQT Maintainer Architecture Guide

This document is the map for the native Linux application. Detailed ownership,
threading, and algorithm notes live beside the relevant classes and hot paths in
the `VDQt*` source files.

## Repository boundaries

`CMakeLists.txt` explicitly lists every source compiled into `VirtualDubQt`.
Those maintained native modules live in `src/VirtualDub/VDQt*` plus
`src/main.cpp`.

`vdqt_core` is a build-only static library containing those native modules. The
application and offscreen controller tests link the same compiled implementation,
with the same definitions and dependencies. It is not a new installed shared
library. Isolated audio-testing and other test-specific compilation variants
remain separate targets, so testing hooks cannot enter the production binary.

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

Source replacement is serialized by `SourceTransitionScope`. Public Open/Close
requests arriving inside another transition are coalesced and deferred until it
finishes. Private workflow helpers can replace their own source, but UI actions,
drop events, and public automation entry points cannot interrupt an active
operation. Teardown joins preview/audio consumers without pumping arbitrary
events before releasing the authoritative decoder.

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
the presentation-order index and provide VFR frame durations. A known sequential
traversal appends the next ordinal directly, even if its PTS equals the previous
frame's PTS. Timestamp equality does not mean the pictures are the same frame.
An expected indexed revisit likewise checks only its expected entry. When a seek
actually requires reconciliation, a lazy hash maps each timestamp to a sorted
list of ordinals; duplicate timestamps are retained rather than overwritten.
That auxiliary storage is released on Close or a deliberate index rebuild.

EOF and verified length are separate facts. `mIndexTraversalContiguous` records
whether the current traversal started at the beginning or a verified prefix
anchor; approximate timestamp seeks and decode errors invalidate that proof.
Only a contiguous traversal whose next ordinal equals the indexed prefix length
can promote decoder EOF to an exact frame count. The worker independently reports
EOF so playback can stop and restore the last displayed playhead without shrinking
an incomplete timeline. Only verified traversals may extend the prefix; an
approximately labeled sparse observation never becomes prefix proof. Exact
seeking and index sharing remain separate
audit work; an approximate time seek is not proof of a VFR ordinal.

`hasCompleteFrameIndex()` is independent of exact count metadata. `ensureFrameIndex()`
reuses a verified complete index for navigation/export and reports only cached
length, not a fresh health analysis. `scanVideoStream()` still deliberately decodes
the source afresh for error analysis. A cancelled fresh scan clears complete-index
proof even if its old exact count remains a valid length hint; Close and changes
to corrupt-frame recovery also invalidate it. Color-conversion changes do not
alter presentation order and therefore retain it. Raw/rendered exports use this
source-owned proof across selections, segments and retries, not a process-global
cache of previously opened files. Worker/editor snapshot sharing remains separate.

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

The bottom position label is an interactive control as well as a display. It
emits a jump request to `VDQtMainWindow`, which pauses playback, opens the
frame/time entry dialog, exits timeline zoom when necessary, and then uses the
ordinary `SetPosition()` path. Frame and time values in that dialog refer to the
edited timeline, not necessarily the underlying source-frame number.

`VDQtTimeline` stores source-identity intent separately from its segment list.
An unedited unknown/estimated source allows decoding beyond the provisional
count; exact source lengths and explicit edits remain bounded. An intentionally
empty edit never becomes identity when metadata changes. Undo/redo snapshots
carry this intent, and unchanged preview metadata does not clear history.
An explicit full-source list can retain ordinary recompression/copy eligibility
when its length is verified, without losing its persistence intent.

Exports, raw export, and frame serving carry `timelineExplicit`. Nonempty lists
also imply explicit mapping for old API callers. Empty plus false means absent
mapping/source identity; empty plus true means no output and must fail before
rendering or FIFO creation. Controllers must forward that flag when all frames
were deleted. Image/audio jobs and saves apply the same no-output rule, while
source-only analysis can still inspect the original media.

## Filter pipelines

`VDQtFilterSystem::instance()` is the editable session chain. Preview workers,
frame servers, and other concurrent consumers take independent transient
snapshots. This matters because temporal histories, asset caches, LUTs, and VDX
plug-in instances are mutable and generally not thread-safe.

Native plugin runtime keys include a pipeline-private namespace in addition to
the serialized filter ID. Reset, replacement, and destruction release only that
pipeline's instances; identical IDs in another preview/export pipeline remain
independent. Destination image storage is detached before parallel row tasks
start, and every task completes before its image owner can be released.

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

Each call copies its options and captures codecs, video filters, and audio
filters before any progress callback or event pumping. Jobs supply their saved
processing snapshot explicitly; they do not install job settings in the editor.
Rendering uses a pipeline owned by that call. Offline audio preparation accepts
the captured chain, and job audio players skip live playback-device creation.

`OperationScope` stops and joins interactive preview work, blocks editor actions
for the entire workflow, and restores controls on every return path. Private
helpers may nest scopes, but public actions cannot start a competing operation.
Job Control and progress/cancel dialogs stay usable. Unattended error reporting
never disables this protection.

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

Numbered AVI exports, interactive/script/queued image sequences, and split
captures share `VDQtOutputTransaction`. Inspect the **actual generated paths**
before approval, then retain that transaction through rendering/commit; do not
re-inspect after approval and thereby authorize a newly arrived file. Unattended
scripts reject collisions; image jobs use their explicit replacement option.
The transaction checks destination and parent-directory identities, preserves
ordinary permissions, and uses Linux no-overwrite atomic moves without a
cross-filesystem copy fallback.

For a multi-file commit, originals move into the staging directory's `backups/`
and a `recovery.txt` maps backup paths to their original destinations. A failed
commit restores originals without overwriting intervening files. Rollback moves
new outputs into private quarantine before removing them, so a changed public
destination is not blindly deleted. If removal/restoration fails, staging
auto-removal stays disabled and the returned error lists exact recovery paths.
Never replace that error with a generic failure message, or re-enable cleanup
from a caller. Individual moves are atomic; a whole multi-file export is not a
single atomic filesystem operation or a power-loss recovery guarantee.

## Source protection

`VDQtSourceDependencies` inspects local input files without running scripts or
opening a media decoder. FFconcat headers take precedence over filename suffixes,
and an already-open concat demuxer supplies a format hint when available. Nested
manifests and literal script imports are followed recursively; unknown functions,
runtime paths, missing files, unsupported playlists, cycles, and exhausted audit
limits keep the report incomplete. This is conservative inspection, not a full
AviSynth/Python interpreter or proof of arbitrary plug-in behavior.

Collection uses hash sets, streamed image-pattern enumeration, and bounded text,
path, reference, document, and recursion budgets. Concat path tokens follow
FFmpeg's single-quote/backslash rules, not shell quoting. Native filesystem
resolution preserves `symlink/../` semantics. A manifest reached through another
directory keeps its own relative-path context. Ordered immediate concat entries
(including repeats) are separate from deduplicated recursive safety dependencies;
only the former belong in the editing/project source list.

`VDQtSourceSafety::captureSources()` builds an operation-scoped snapshot containing
protected paths and device/inode identities. Image/segment loops check only their
destination paths against this snapshot instead of rereading every input for
every output. Refresh once after rendering/approval and before commit; refresh
retains both the original open-file identities and newly discovered dependencies.
Do not replace the original snapshot with a fresh one that forgets old sources.
An incomplete audit refuses existing destinations but permits unrelated new paths.
These checks do not lock external writers or provide an atomic view of the entire
source graph; staged-output commit must still apply its own destination checks.

## Persistent and session state

`VDQtProcessingState` contains codec, filter, and processing choices.
`VDQtProjectState` adds sources, position, timeline, selection, and markers.
`VDQtJobState` adds durable queue status and output instructions.
`VDQtProjectFile` is the versioned JSON boundary for all three.

Loaders parse into temporary values and commit only after full validation.
Writers use `QSaveFile` so a crash cannot leave a half-written project or queue.
The GUI also validates/indexes saved frame references using the candidate decoder
before replacing the current source or processing settings. A failed reference
check or cancelled validation therefore retains the current editing session.
`SessionRollbackScope` also retains the full timeline (including history), frame
clipboard, processing/audio choices, markers, selection, zoom, position and saved
project path during the final source replacement. A late open failure reopens the
original source and restores that state. If the original source or soundtrack is
also no longer available, the guard saves the original project to a uniquely named
recovery document and reports its path rather than silently losing the session.
These checks are not filesystem locks against concurrent external changes.

Append validates a uniquely named candidate manifest before installing it; it
never overwrites the manifest backing the active decoder. The same rollback guard
preserves editor state on failure and preserves audio choices and editing history
on success. An untouched source remains identity; existing edits receive a bounded
segment for the appended material. Recovery autosaves are protected during commit.
Temporary-source lifetime changes must pin the original materialization until the
guard has either committed or finished restoring it.

Document version 7 stores timeline intent in projects/jobs and source-count
accuracy in projects. The editor saves untouched sources as implicit identity,
not as bounded segments derived from an estimate. Versions 1-6 still load using
their original rules: missing/empty edit arrays mean identity, and nonempty arrays
remain explicit. New-format missing/nonboolean/inconsistent intent is rejected.
Older binaries cannot read newly saved version-7 files; retain older originals
when a binary rollback is needed. Processing-settings files retain version 6
because their schema is unchanged. Legacy estimated-identity ambiguity remains;
nonempty old edit lists cannot safely be reinterpreted as untouched sources.

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
