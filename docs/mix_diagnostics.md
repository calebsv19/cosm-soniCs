# Immutable production mix diagnostics

This first diagnostic slice adds `get_mix_snapshot`, `capture_mix` and
`get_mix_capture` to the source MCP adapter. The tools are read-only and work on
read-only endpoints. They do not edit music, mute/solo, routing, selection,
transport, history or normal-runtime files. A source or prepared-package result
is separate from installation and human listening acceptance.

## 1. Capture and identity

`get_mix_snapshot` returns an opaque `expected_mix` token, generation, sample
rate, node identities/routing/controls and a settings identifier. `capture_mix`
requires that exact guard and decimal-string `start_frame`/`end_frame`. It checks
complete common arrangement content plus scalar/routing, inserts and dedicated
EQ values on the control owner. The hash is not used to accept the guard.
Conflicting state returns `state_conflict`; recording, bounce or an active edit
returns `busy`. Preparation/render failure returns `engine_rejected` with no
partial published result.

Capture uses an independently owned export plan and the production mixer. It
renders the linear authored timeline cold from frame zero, including all
preceding material, and retains following input needed by declared lookahead
and complete production blocks. It measures only `[start_frame,end_frame)`.
`rendered_frames`, `preceding_context_frames`, `following_context_frames`,
`render_block_frames` and `master_latency_frames` expose this context. Live loop,
keyboard audition and live DSP histories are excluded. This is a synchronous,
bounded offline operation on the application owner; it can delay UI/control
requests, but no capture work or allocation occurs in the real-time callback.
The source adapter allows a 180-second connection timeout for this operation.

The result owns copied settings and measurements. `get_mix_capture` returns one
node, one serial insert, one frequency window/channel and eight global source-provenance entries per page, with independent
`node_offset`, `effect_offset` and `source_offset` cursors. `effect_offset` ranges
from 0 to 15 and refuses positions outside the selected captured chain; an empty
chain returns a null effect at offset zero. `next_effect_offset=-1` ends that stream. Old captures remain unchanged after
edits or project replacement. Four guards and four captures are retained per
endpoint; overwritten/expired tokens refuse rather than returning current data.
No PCM is retained, published or auditioned by these tools.

The noncryptographic `settings_fingerprint_fnv1a64` identifies captured numeric
settings/layout/notes/automation in the current engine lifetime. It excludes
sample-bank/decoded-media bytes and is not a cross-session audio fingerprint.
The immutable `capture_id` and generation identify the particular result.

## 2. Graph stages and accounting

Schema: `sonics_mix_capture_v3`. Stable `track_id` zero denotes master; other
track IDs survive array movement within their engine generation. Each stage
has its own `latency_frames`. Tap samples are assigned to authored frame
`render_frame - tap_latency` before range and bucket selection. Latency denotes
the engine's declared sample delay, not frequency-dependent filter group delay.
A changing tap latency refuses the capture instead of mixing timestamps.

| Node | Input | Inserts/EQ stage | Output stage | Sanitization |
| --- | --- | --- | --- | --- |
| Child | Source after child/region gain, fades and source automation | After serial inserts then dedicated EQ | After stereo balance, before routing compensation | Unavailable |
| Group/return | Compensated incoming sum | After serial inserts then dedicated EQ, before bus gain/pan | After bus gain/pan, before outgoing-edge compensation | Unavailable |
| Master | Compensated graph sum before dedicated EQ/inserts | After dedicated EQ, before serial inserts | After inserts, before sample sanitization | Actual final output |

Captured mute/solo, parent identities, sends, authored linear gain/pan, insert
settings and dedicated EQ describe routing context. `source_active_flag` applies
to child source eligibility; groups/returns process their incoming sums without
that child flag. Parent mute/solo closure affects child source gain. Return solo
suppresses dry main-to-master paths while retaining source feeds to returns.
Sends are post-processed-lane output through the appropriate compensation edge;
linear send gain remains separate from dB gain.

Children, groups, returns and master overlap in this graph. **Do not sum their
RMS/peak measurements or treat them as independent processed stems.** These are
actual in-context taps, including nonlinear bus/return/master processing. They
can locate a signal change between stages but do not produce independent
contribution percentages or a counterfactual mix.

## 3. Metrics, provenance and honest limits

Each available stage returns independent channel sample peak, unweighted RMS,
dBFS, sample count, full-scale count (`abs(sample)>=1`) and overload count
(`abs(sample)>1`). Amplitudes are original linear float levels;
`dBFS = 20*log10(amplitude)`, with JSON null for silence. Floating internal-bus
levels above one are measurements, not an automatic quality or clipping verdict.
No normalization, channel averaging, smoothing or LUFS weighting is applied.

There are 32 equally spaced frame buckets with original-level per-channel peak,
RMS and sample count. `bin_frame_edges` contains 33 exact decimal-string edges;
each bucket covers its half-open adjacent pair. Summed bucket counts equal the
whole-range count, including nonuniform final bucket sizes and empty buckets.

Source pages copy stable region/track IDs, placement, gain, effective base
instrument/preset parameters, physical parameter units, supported control IDs,
inherited/local state and automation-lane counts. Authored note gates are clipped
at the region's right edge and summarized in region-relative frames. They do not
resolve per-note automated values, mono/choke/pedal adjustments or sampled tail
lengths. Base release parameters are not measured acoustic release durations.

Current bounds: 1–32 stored lanes plus master, at most 256 stored region slots,
32,768 notes, 65,536 total region/track automation points, 8 channels, 30 measured
seconds and a measured end no later than 300 seconds from zero. There is also a
57,600,000-frame requested-end work cap and a 60-second declared-latency cap.
Invalid/over-cap projects refuse without truncating their graph. Existing graph
preparation memory limits still apply.

`unavailable` gives explicit reasons for raw audio-file region tails, unsupported-effect
reduction, individual-note audible tail attribution, asset content fingerprints,
whole-programme loudness, LRA and unmapped surround loudness. Passage LUFS and true peak are available through the separate `loudness` object described below. Existing live
scope histories and pass-through meter inserts are not measurements from this
capture. A high
RMS, frequency-band energy or stereo asymmetry does not itself establish a poor
mix; interpretation needs passage roles, comparisons and listening.

## 4. Ordinary SDK use

Using an initialized ordinary MCP `ClientSession`, read structured tool results
and calculate frame requests from the snapshot's actual sample rate:

```python
async def call(name, **arguments):
    result = await session.call_tool(name, arguments)
    assert not result.isError
    value = result.structuredContent
    assert value["ok"], value
    return value

snapshot = await call("get_mix_snapshot")
rate = snapshot["sample_rate"]
captured = await call("capture_mix", expected_mix=snapshot["expected_mix"],
                      start_frame=str(100 * rate), end_frame=str(120 * rate))
for node in range(captured["node_count"]):
    page = await call("get_mix_capture", capture_id=captured["capture_id"],
                      node_offset=node)
    # Inspect the node's own processing stages; do not sum parent/child values.
```

Use `next_source_offset` to page all source provenance independently. Repeat
`get_mix_capture` for the same immutable result; take a new exact guard/capture
for a deliberate before/after comparison. Capture/render failures never imply a
musical edit occurred.

## 5. Verification

`make test-mix-capture` qualifies sine amplitude/RMS, silence, asymmetric stereo,
full-scale versus overload, nested children/groups, return contribution, insert
gain, per-stage compensation, exact production master accounting, mute/return
solo, playback/selection/history/plan preservation, stale guards and a real
sampled violin. `make test-mcp-mix-capture` uses the ordinary official SDK and a
private read-only endpoint to verify schemas, immutable edit/replacement
readback, strict refusal, bounded retention, full preceding context and silence.
Its optional `--project <private-copy>` mode measures a supplied private project
through the same SDK without edits or audition. The standard stable/headless
lane includes the production-engine test.

## 5. Insert observations and applied reduction

Each insert page retains the captured effect ID, type, chain position and enabled
flag. Input/output boundaries use the same calibrated per-channel levels and 32
buckets as node stages. Each boundary subtracts its own cumulative serial delay
plus graph input arrival latency, before routing-edge compensation. The master
insert input follows the native master EQ; child/group/return insert output
precedes their native EQ and bus pan/gain.

Native Compressor and Limiter expose actual sample-by-sample applied attenuation,
recorded inside the processor. Results contain per-channel minimum and arithmetic
mean gain in dB and bucket counts/minima/means. Values are nonpositive; makeup gain
is excluded. Compressor detectors are independent per channel; limiter gain is
linked across channels. Reduction is aligned to the processed output sample,
including lookahead latency. Disabled processors expose zero reduction. Other
processors explicitly report unavailable; output/input RMS ratios are never
substituted. No PCM or individual sample trace is retained after accumulation.

The existing cold captured-settings render excludes live bypass transitions and
live DSP history. Trace storage is prepared only for a private diagnostic manager;
live audio receives no diagnostic allocation or observer. Effect API v3 adds an
optional borrowed trace buffer without changing audio processing equations.

Native proof includes analytic channel-independent compressor attenuation with
makeup, disabled parity, linked limiting, nonaligned windows, and a one-sample
lookahead onset that crosses production block boundaries. Ordinary production
bounce parity remains required. SDK proof covers strict effect paging and immutable
readback through authored edits/project replacement.

## 6. Frequency analysis over time

Schema v3 retains 32 spectral observations for every child, group, return and master.
Child/group/return observations are after their inserts, dedicated EQ and bus
gain/pan, before routing-edge compensation. Master observations are after
sanitization. Node graph inputs remain in context; nested outputs overlap and
are not independently additive stems. Insert-level spectra are outside this slice.

Use `get_mix_capture(capture_id, node_offset, frequency_window=0..31,
frequency_channel=0..channels-1)`. Independent next-window and next-channel
cursors end at -1. Existing node/source/insert cursors still work. Invalid indices
refuse; retained frequency results remain immutable through edits and replacement.
Capabilities report window count, FFT size and one channel per spectral page.

Each window is at most 2048 samples, centered near its corresponding level-bin
midpoint and clamped wholly inside the requested measured range. Start/end frames
and the observed stage latency are explicit. Windows can overlap or repeat on
short ranges and can leave gaps on long ranges. This is a time-sampled spectral
series, not a continuous spectrogram or full-range band-energy integral. Ranges
shorter than three samples return an explicit unavailable reason. Short windows
use their actual periodic-Hann length and zero padding to the fixed FFT size.
`window_resolution_hz=sample_rate/actual_window_samples` distinguishes effective
resolution from FFT grid spacing; at 48 kHz a full window spans about 42.7 ms.

Each channel is analyzed independently, so opposite-phase channels retain their
individual energy. Inter-channel phase and correlation are not reported.
There is no DC removal, downmix, normalization or perceptual weighting. Bin k is
at `k*sample_rate/2048`. One-sided bin mean-square power is
`endpoint_factor*abs(DFT)^2/(2048*sum(Hann^2))`, with factor 1 at DC/Nyquist
and 2 elsewhere. Summing bins equals the Hann-weighted mean square of that window
by Parseval. This differs from tonal peak-amplitude spectra and ordinary unwindowed
RMS. Power decibels use `10*log10(power)`; exact silence is null dBFS.

Disjoint bands use edges 0, 31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000,
16000 and 32000 Hz, clipped/extended to Nyquist. Bins are assigned by center
frequency, lower-inclusive/upper-exclusive, with Nyquist included in the last
band. Their powers sum to the full spectral power. These are FFT bin integrations,
not standards-qualified octave filters, LUFS, true peak, masking scores or
proof of musical quality. Window leakage and limited low-frequency resolution
must be considered when comparing bands.

Temporary window PCM is preallocated only on the private diagnostic path and
freed after the FFT. Retained results contain powers, bounds and captured state.
The worst-case 33 nodes, 8 channels and 32 windows use about 66 MiB of temporary
window PCM and 33 MiB of retained powers, in addition to existing diagnostic
metadata; four-capture retention remains enforced. Capture stays synchronous
and can delay application control requests. Live audio acquires no diagnostic
work or allocations.

Headless calibration covers independent and anti-phase stereo, bin-centered
tones, DC, Nyquist, deterministic noise, silence, short/zero-padded ranges, five
sample rates, arbitrary render block partitions, graph latency, changing tones
and nested child/group/return/master gain factors. Existing production bounce
parity and authored/history/transport preservation checks remain mandatory.
MCP proof covers strict independent paging, band sums, silence and immutability.

## 7. Note endings and observed source tails

`get_mix_capture(..., note_offset=0..note_count)` adds an independent eight-note
page and `next_note_offset`; offset equal to count returns an empty terminal page.
Every row preserves original authored note index, track/region identity, pitch,
velocity, authored key-off and next distinct viable authored track onset. The
next onset ignores zero-duration/zero-velocity and region-external triggers; it
does not imply that muted or zero-gain tracks are audible.

Prepared rows read the actual detached production source. They distinguish
prepared key-off, piano damper-off, region end, source render fence, hat choke,
mono handoff and legato chain start. Original indices survive export compaction.
Evaluated controls are copied at onset, damper-off and immediately before region
end, including release, ADSR, level, pedal and release-noise targets. The normal
export automation freeze and piano forced-pedal-up rules apply. Sampled voices
have an explicit 4 ms click-protection release floor. These are selected actual
control evaluations, not a complete automation history, smoothed sampler
dynamics trace or a predicted audible endpoint. Dynamic controls, recorded PCM,
percussion decay and shared piano mechanics can change the sound duration.

Each MIDI region now has aggregate source measurements after instrument
expressions/clipping and instrument gain/pan automation, before authored
track/region scalar gain and serial FX/EQ. A source skipped by the graph because
of mute or zero gain has zero observed samples, not manufactured silent audio.
Audio-file regions explicitly report that MIDI ending diagnostics are unavailable.
All observations stay inside the requested measured range; there is no automatic
tail extension. Request a range spanning the relevant transition to observe it.

Per-channel source readback gives peak/RMS, first/last sample at or above
1e-4 amplitude (-80 dBFS), and the same metrics after the latest prepared
damper-off among viable notes in that region. If no viable damper gate exists,
that field is null and post-gate accounting stays empty. The range-end flag only
means activity reaches the final measured sample; lack of such a flag does not
prove no later resurgence. Below threshold is not mathematical silence or a
psychoacoustic audibility verdict. Overlapping notes and piano mechanical layers
remain aggregate; no individual-note audible tail is fabricated.

Existing node/insert channel pages also expose above-threshold sample count and
first/last authored frames on their own latency-corrected clocks. This permits
comparison between source activity, processed lanes, returns and master without
misattributing reverb/FX tails to one note. Nested measurements still overlap.

Capture bounds are unchanged: 32 tracks, 256 regions, 32768 total authored notes,
four retained immutable results. Copied prepared note targets and source meters
are bounded and freed with each result. The getter and observer are installed
only on private export ownership; live audio receives no diagnostic callbacks.

Qualification includes measured gate/release behavior, hard region truncation,
pre-child-gain measurements, unobserved muted sources, sample-protection floors,
piano pedal automation, original-index mono/legato handoff after export
compaction, hat choke fences, production bounce/FFT parity and ordinary MCP
strict paging/immutable readback. Actual composition quality still requires
passage roles and listening.

## 8. Selected-passage LUFS and true peak

`get_mix_capture` adds an immutable `loudness` object for the selected node's
processed output; master uses sanitized output. It contains integrated LUFS,
final momentary (400 ms) and short-term (3 s) LUFS, relative threshold, original
per-channel true-peak linear amplitude/dBTP, and M/S history at 100 ms endpoints
plus the exact final sample endpoint. The existing read-only guard, node cursor,
four-capture lifetime and 30-second range limits apply. No project, insert,
transport, undo, persistence or live DSP mutation is performed.

Pinned MIT libebur128 1.2.6 implements K-weighting, unit-weight mono or left/right
stereo, -70 LUFS absolute and -10 LU relative gates, 400 ms gating blocks with
75% overlap, and interpolated true peak (4x at 44.1/48 kHz, 2x at 96 kHz).
Only those rates and explicit mono/stereo layouts are qualified; other layouts
or rates return `available:false` with a reason, while ordinary graph meters
remain available. Mono is not counted as dual mono. Individual channel peaks
are separate from the weighted channel sum used for loudness.

The loudness meter starts cold at the selected range's left edge, although the
production audio graph renders all preceding context. Incomplete gating blocks
are discarded. M and S are ungated and return null before their complete window
or for silence; integrated LUFS returns null with no qualifying gated blocks.
True peak includes the interpolator's 32-frame zero-extension flush at the range
boundary, excluded from every loudness window and gate. No additional production
tail is measured. Parent/child/return measurements overlap and cannot be added.
Integrated LUFS is **selected-passage**, not whole-programme loudness unless the
entire programme fits the selection. LRA and full EBU Mode certification are
outside this slice; existing live LUFS insert placeholders are unchanged.

`make test-loudness-analysis` regenerates reconstructed [EBU Tech 3341](https://tech.ebu.ch/docs/tech/tech3341.pdf)
definitions 1-5, 9, 12 and 15-19, analytic mono/stereo, silence, insufficient
windows, terminal impulses/intersample peaks, block partition invariance and the
30-second history bound. It compares with FFmpeg's separate `ebur128` filter and
checks published +/-0.1 LU and +0.2/-0.4 dBTP tolerances. Long gate definitions
exercise the pinned algorithm beyond the wrapper history range in the test only.
The official EBU ZIP returned HTTP 403; authentic programme cases 7/8 and official
transient vectors 20-23 have not been qualified. The test requires FFmpeg and the
existing MCP Python interpreter but no optional numeric Python library.

## 9. Whole-song integrated loudness and LRA

Use the same immutable guard and node readback with `capture_mix(expected_mix=guard,
whole_song=True, tail_frames="240000")`. Leave start/end at their mode defaults
`0`/`1`; those are wire placeholders and arbitrary passage endpoints are refused.
The actual endpoint is derived from all active region positions/durations in the
exact arrangement snapshot. `tail_frames` explicitly adds release/ambience time
(240000 = 5 s at 48 kHz); default zero adds no tail. No music/clip/transport edit
or normalization occurs. All children, buses, returns and sanitized master are
measured in their original production context with node latency correction.

Whole-song capture supports up to 600 seconds of authored arrangement plus at
most 30 seconds explicit tail, mono/stereo at 44.1/48/96 kHz. Empty or over-cap
arrangements refuse; they are never silently cropped. It uses the existing four
immutable capture slots. The response schema is `sonics_song_loudness_v1`;
`get_mix_capture(capture_id, node_offset=n)` returns node provenance and programme
LUFS/LRA/true peak. Other cursors must remain zero; detailed frequency, insert,
source/note and passage-history pages are intentionally excluded to bound storage.
The original detailed passage contract and 30-second limit remain unchanged.
Loudness routines use standard -O2 compilation (no fast-math). Captures are synchronous and share the existing 180-second MCP transport timeout;
this is analysis, not a full-song audio export or cancellable background job.

LRA follows [EBU Tech 3342 (2023)](https://tech.ebu.ch/docs/tech/tech3342.pdf):
complete three-second short-term windows sampled every 100 ms, -70 LUFS absolute
gate, -20 LU relative gate derived from the absolute-gated mean energy, and the
interpolated 95th minus 10th loudness percentile. `lra_complete_windows`,
`lra_gated_windows`, threshold and percentile readbacks make the result inspectable.
No complete or qualifying windows gives null LRA, while constant material gives
zero LU. `lra_provisional:true` marks programmes shorter than 60 seconds; LRA is a
statistical variation measure, not crest factor or musical-quality judgement.
The last fractional 100 ms interval affects I/M/S/TP but adds no off-cadence LRA
window. Terminal true-peak padding is excluded from all loudness/LRA statistics.

Qualification adds reconstructed EBU 3342 definitions 1–4 at all three supported
rates, independent FFmpeg comparisons, block partitioning, silence and insufficient
window tests. The unavailable official programme files remain outside full EBU
certification. The private 40-second production fixture checks complete extent,
explicit tails, no detailed storage, bounds/stale guards and ordinary bounce
parity. The private V4 SDK pass checks every whole-song node, no edits and immutable
readback independently of Desktop GUI or listening acceptance.
