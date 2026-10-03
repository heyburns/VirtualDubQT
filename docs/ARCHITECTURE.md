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

Playback uses device-presented audio time only while the sink is active and
error-free. `VDQtPlaybackClock` switches an ended/failed or 250 ms nonadvancing
sink to monotonic elapsed time anchored at the last heard position. This fallback
is reset at play/seek; a lagging recovered sink cannot pin the picture again.

The local frame server and some export subprocess pipelines use additional
worker threads or child `ffmpeg` processes. Their owners synchronously cancel
and join them during teardown.

Serving uses exact native AVS metadata without evaluating the whole graph and
indexes ordinary media once. Its documented wire format is CFR NUT: a cumulative
sample grid repeats long VFR intervals and can omit short ones. Expanded filter
phases are sampled in their own intervals; alpha/16-bit RGB is not forced to
RGB24. Existing reinterpretation, conversion and decimation settings determine
the output clock. Unrepresentable counts/rates fail before narrowing.

Encoder capabilities and validated arguments live in `VDQtCodecEngine`, shared
by Configure, native Fast Recompress and processed/two-pass export. Unknown
encoder families retain their defaults rather than guessed x264 controls.

`VDQtJobQueue` owns a lifetime QLockFile. Concurrent editors use independent
paired queue/recovery paths, with an empty marker making a recovery-only session
discoverable after a crash. Structural snapshots have a serialized admission
budget; global diagnostic retention leaves space under the JSON document cap.
Save failures are sticky and visible even with Job Control closed. The runner
checkpoints before encoding and after completion, stopping on failure. Malformed
old autosaves are protected, and Close defaults to keeping an unsaved editor open.

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
supplies dimensions and pixel layout. The session temporary directory contains
fresh per-copy subdirectories owned by `VDQtTemporaryMediaFile` leases. Active,
deferred-open, rollback and private-job consumers pin their lease; the last
release removes only that generated copy after its decoder/audio/worker closes.
The weak path registry does not retain old files. Reopen pins before Close,
projects keep original raw paths/parameters, and recent files exclude ephemeral
copies. Raw and two-pass copies use checked space estimates and a refreshed
volume check before writing; this is preflight, not a reservation against other
processes consuming free space.

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
approximately labeled sparse observation never becomes prefix proof. Random
frame-number seeks establish a complete presentation index before using a
timestamp anchor; frame number divided by average FPS is never used to label a
picture. Repeated-timestamp anchors are ambiguous, so those paths count from a
safe earlier unique anchor or the beginning. Seek overshoot retries from start.

`hasCompleteFrameIndex()` is independent of exact count metadata. `ensureFrameIndex()`
reuses a verified complete index for navigation/export and reports only cached
length, not a fresh health analysis. `scanVideoStream()` still deliberately decodes
the source afresh for error analysis. A cancelled fresh scan clears complete-index
proof even if its old exact count remains a valid length hint; Close and changes
to corrupt-frame recovery also invalidate it. Color-conversion changes do not
alter presentation order and therefore retain it. Raw/rendered exports use this
source-owned proof across selections, segments and retries, not a process-global
cache of previously opened files. Cancelled indexing preserves codec/packet state
when it still continues the verified prefix, allowing the latest scrub request to
resume work. Callback checks between packets/frames abandon obsolete generations;
an individual blocking demux read, AVS evaluation or plugin call is not preemptible.

Complete immutable index snapshots are shared between worker and editor, including
keyframe controls, export and audio-start timing. Matching open-source identity,
stream/timebase/codec and recovery policy are required for adoption; generation
tokens reject old-session delivery. The queued Qt alias is explicitly registered.
Partial growing prefixes are not copied on every frame, and read-only accesses do
not detach the shared array. Edited-timeline cumulative timing and VFR time-entry
remain separate audit work.

Image sequence manifests configure each still-image demuxer's rational input rate,
not only concat durations. Otherwise its default 25fps clock quantizes requested
30/60/100fps timing even when presentation ordinals are retained. Fresh imports,
project reload and queued reconstruction use the same manifest builder.

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
Time entry uses an indexed cumulative edited-boundary callback, including the
exclusive end, instead of multiplying by average FPS. The dialog binary-searches
the frame displayed at the requested time. Its operation scope joins preview
work and blocks source/edit replacement while that callback borrows the decoder.

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

Field Delay/Interlace history retains the incoming stage frame, not an output
already containing a copied old field. Recursive blend/smoother history instead
retains its output intentionally. Sequential context controls whether history is
usable after a seek. At each stage boundary, channel layout and alpha are normalized
to straight RGB888/RGBA8888/RGBA64; image depth alone cannot identify transformed
ARGB/premultiplied storage safely.

`processFrameSequence()` is the authoritative API. Rate-changing filters such as
bob deinterlacing may emit multiple temporal phases for one source image.
`processFrame()` exists for older single-image callers and returns only phase
zero.

Expansion is stage-wise: each stage consumes the preceding stage's ordered
sequence, and Bob doubles each item before the next stage sees it. Upstream work
is not rerun for every final phase. Downstream frame numbers, rate, time and
duration advance with emitted phases, and each temporal filter keeps the preceding
emitted image rather than a separate same-phase history. The phase-count cap and
a conservative 512 MiB budget for retained stage images/temporal history reject
oversized sequences with an explicit failure. Caller-owned images and individual
kernel scratch retain separate normal allocation-error handling.

`VDQtFilterValidation` owns shared numeric types/bounds and relationships for
generic controls, loaded JSON, script configuration and the processing boundary.
Calculated resize/framing/rotation dimensions are checked against the current
stage size and precision before allocation. Invalid settings report an error;
they must not enter unsafe casts or level calculations. Pipeline/project loading
also migrates missing or duplicate IDs once while preserving valid unique IDs.

`VDQtFilterGeometry` supplies the same checked sizing plan to processing,
validation and the chain table. Relative aspect/alignment controls resolve
against each incoming stage. Existing saved absolute dimensions are already
resolved; the long Windows script signature resolves its controls once during
configuration. Conditional or plug-in-defined table dimensions remain unknown.
`VDQtImageResampler` implements the existing point, linear, cubic and Lanczos
choices at source precision, with reduction-aware support for precise modes,
premultiplied color accumulation and parity-separated interlaced resizing.

Required effects must not be silently omitted. A failed sequence discards every
output phase, resets partial runtime history, and records a pipeline-local
`VDFilterProcessingError` (filter ID/name and actionable message). Preview keeps
the valid input but clears stale output and shows the error in the status bar;
export/server callers propagate it and do not publish partial output. Missing
image reads are not cached, so restoring an asset allows a retry.

Transient chain replacement resets temporal history and private plugin runtimes,
but retains immutable parameter-keyed six-axis tables (at most eight). Gamma,
sRGB conversion, Curves and Levels also use bounded immutable channel tables,
computed with their original integer-level formulas at 8/16-bit precision.
Derived caches are pipeline-private and keyed by all affecting parameters. Asset
images are pipeline-local and bounded by 64 entries / 64 MiB of decoded pixels;
larger valid images are drawn without retention. Each use checks canonical path,
size and modification time before reusing an image. This catches ordinary edits,
replacement and deletion, not content changes that preserve all that metadata.
Clear and persistent chain replacement release derived caches as well as history.

Source sample-aspect ratio travels with each QImage through a normalized rational
metadata tag, including cached and worker-delivered images. Manual display aspect
overrides are presentation-only. Resize/crop/pad inherit the ratio, while exact
quarter turns reciprocate it. Export and frame serving declare the resulting
stream ratio and reject unsupported changes mid-stream. Timestamped NUT declares
the packed raw-video FourCC explicitly; leaving it unset can make a demuxer
interpret RGB24 as RGB15 despite the supplied pixel-format field.

`VDQtFilterContextForFrame()` is the common input clock contract. Filter position
and time refer to the edited timeline; separate source fields identify the decoded
picture. Masked ranges hold a picture while their underlying frame durations keep
timeline time advancing. Complete immutable decoder snapshots include cumulative
duration prefixes, reused by all timing consumers. A preview request carries the
timeline ordinal and segment snapshot in its coalesced pending slot; the worker
resolves time after decoding/indexing instead of using an obsolete GUI estimate.

Reserved `_sylia.*` numeric parameters preserve VirtualDub script range,
clipping, and opacity metadata without changing the public filter-instance
structure. Do not expose those reserved keys as ordinary user parameters.

## Audio pipeline

`VDQtAudioPlayer` selects a stream and builds one of these pull pipelines:

- FFmpeg source -> source-rate/channel S16 decode-ahead device -> shared effects
  graph -> sound-device format conversion -> Qt audio sink.
- Native AviSynth clip -> source-rate/channel S16 decode-ahead device -> the
  same effects graph and device conversion -> Qt audio sink.

`VDQtAudioFilterDevice` uses the export libavfilter algorithms for every enabled
effect, not separate approximations for fixed-rate effects. The no-effect,
same-format path remains a transparent adapter. Conversion to a preferred
UInt8/Int16/Int32/Float device happens after filtering, so a device fallback never
bypasses filters or changes their source-rate interpretation. Graphs use one
worker thread and bounded pending output. Errors are returned explicitly;
callback diagnostics are synchronized for UI reads, and failed graphs never
fall back to unfiltered audio. The parameter validator also guards exports and
saved settings before exponential/sample-rate calculations.

EOF includes effect tails and QIODevice read-ahead output. Seeking/reconfiguring
flushes both the graph and Qt's internal byte buffer. Partial upstream sample
frames are retained across short reads; an incomplete final frame is an error.

The decoder cursor includes buffered read-ahead. A/V synchronization must use
the sink's presented playback time instead. Seeking stops and joins the producer,
flushes decoder/filter history, establishes a new sample origin, primes the
buffer, and then resumes.

Timeline masks hold only the video picture. `mapOutputToAudioSource()` maps the
advancing source interval for seeking, clock origins and cut detection; using
the held picture's mapping would repeatedly rewind sound inside a mask. The
decode worker likewise reports the advancing interval's duration in both plain
Play and Play Preview. Physical listening remains a separate validation gate.

Offline audio export uses the same conversion rules but writes transactionally
through a staged file. Small timestamp jitter is smoothed; genuine gaps remain
silence.

`VDQtAudioExportRequest` captures codec/conversion settings, filters and ordered
source-sample ranges for manual Save Audio, scripts and jobs. Range resolution
indexes bounded edits and rounds source timestamp boundaries, not each duration
independently. Full identity export retains a soundtrack longer than its video.
The shared renderer extracts source-precision PCM without effects, concatenates
edits, then filters/encodes once. Cuts do not restart effect history or append a
tail per segment. A transaction checks destination identity before installation;
the outer caller still owns source-graph safety and user overwrite approval.

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

Both file and text parsing enforce a 4 MiB input limit. Expressions have a
64-level nesting bound, 1 Mi-character string limit and shared 64 Mi-unit work
budget across statements. Floating arithmetic is kept floating; integer-only
operators check finite, integral, representable operands first. Overflow and
limit errors never publish a partially parsed program. Token matching uses
views/offsets instead of repeatedly allocating the unparsed suffix.

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

Bitmap timing describes the current stage (including emitted Bob phases); legacy
state also exposes the original source identity/time. Known seeks reset temporal
instances. Definitions own module lifetime tokens, never the reverse. The host
supports sequential NEEDS_LAST history, not arbitrary prefetch, native rate
conversion or Windows drawing contexts; these contracts fail explicitly. Its
32-bit RGB boundary requires an explicit precision conversion for high-depth
chains, so the host cannot silently reduce their precision.

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
