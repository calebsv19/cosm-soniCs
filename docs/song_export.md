# Asynchronous full-song export

Main Edit provides an independent whole-song export through **Export** in the effects toolbar and ordinary guarded MCP tools. The existing keyboard Bounce workflow remains a separate range/bounce-and-insert action.

## Native workflow

1. Stop playback/recording and finish any active edit gesture. Open Export and choose **PCM16 WAV** or **Float32 WAV**, an explicit tail (0, 1, 3, 5, 10, 30 or 60 seconds), and normalization off or if-clipping.
2. Export song captures the production graph from frame zero through the last active authored region boundary, including empty leading time and followed by the chosen release/effect tail. Loop selection does not restrict this job. Tail excludes later authored input.
3. The panel reports queued/rendering/publishing and provisional frame-work progress. Completed means the WAV passed metadata/size/content-identity checks and the file/directory were synchronized. Cancel requests queued/rendering cancellation; after publication starts it reports that boundary instead of deleting published output. Native Cancel retains the job displayed at button press even if another client starts a newer job before release. Closing or losing focus closes the panel and retains work and choices; reopening shows the latest job.

Each job writes a unique `Exports/song-XXXXXX/song.wav` below the configured output root. It cannot replace an existing file and does not insert audio, rescan a library, change authored gains, move the playhead or consume undo history. Original-level rendering is the default. PCM16 uses deterministic TPDF dither and clips samples outside its representable range if normalization is off. Float32 preserves finite over-range values. If-clipping normalization is a two-pass peak reduction on exported samples only; it is not a loudness target or true-peak limiter.

## MCP workflow

The adapter provides `start_song_export`, `get_song_export` and `cancel_song_export`. Discover current schemas rather than enforcing a tool count.

1. Read capabilities/current endpoint instance and `get_mix_snapshot`. Submit `start_song_export(instance=instance, request_id=uuid, expected_mix=guard, format="float32", tail_frames="240000", normalize=False)` for a five-second tail at 48 kHz. Tail frames are exact canonical decimal strings; default zero. Formats are `pcm16` and `float32`.
2. Retain the returned `job_id`. Read `get_song_export(job_id=id)` for atomic progress and eventual terminal publication. Omitting job_id reads the latest job or reports idle. Use modest polling; the tool does not block until completion.
3. `cancel_song_export(instance=instance, request_id=new_uuid, job_id=id)` targets that exact job. `cancel_requested`/`cancelling` precede a terminal `cancelled`; `publication_started` or `already_finished` preserves output. Read status rather than assuming cancellation succeeded.

Start requires the exact production guard, write-enabled endpoint, current instance and immutable request identity. Cancel uses the exact returned job ID and the same write/receipt admission. Status is read-only. Replaying a start/cancel receipt does not resubmit work. Once captured, later mixer/effect/arrangement edits do not alter the job. These operations report no authored-music change.

Terminal readback includes format, rate, stereo channels, authored end/tail/output frame counts, path, byte length, production configuration fingerprint, publication/durability/verification flags and the existing media-registry FNV-1a64 content ID (explicitly noncryptographic). Verification refers to bytes at completion, not subsequent external edits. Records are ephemeral: one active job and the last eight retained records in this project lifetime. Completed files survive record eviction, project replacement and shutdown; clients should retain their result metadata.

## Ownership and limits

A control-thread preparation captures a private graph, independently retained source/sample pins and a provenance guard. One existing `core_workers` worker renders/encodes/hashes without touching live engine control, UI or history. Completed graph disposal remains on control. Project replacement and teardown cancel/drain before destroying the originating sample-cache owner.

Exports support at most one hour of authored stereo material plus sixty seconds tail, at the captured project rate. There is no rate conversion, compressed format or surround mapping. WAV RIFF size and available space are checked before admission. RAM is bounded by the captured graph and block buffers; an anonymous float spool plus candidate WAV needs disk space. Progress counts render (including latency compensation) plus encode work; 100 percent remains provisional until publication, sync and identity verification finish.

Cancellation and publication are atomically arbitrated before final publication. Unpublished candidate/spool files are discarded; empty unpublished job directories are removed. A published-but-unsynced or unverified result is reported distinctly and retained for inspection. Exports do not silently normalize or alter music.

## Verification

Private native and ordinary SDK fixtures qualify immutable audio after a native mixer edit, both encodings and exact tails, independently decoded frames/bytes/content ID, monotonic progress, receipt replay, read-only and stale-guard refusals, matching native releases, cancellation, no-overwrite collision, retained completed files and project-replacement retirement. Existing synchronous export failure injection remains on the shared streaming implementation and covers allocation/spool failures, normalization, serial latency, tails and live-state isolation. Headless/package proof does not establish physical listening or GUI acceptance.
