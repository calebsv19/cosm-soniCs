# Automation model and editing

This reference describes the current Main Edit automation model and controls.
Package and runtime acceptance evidence is retained separately from source tests.

Region automation applies in region-local sample frames, including a point at
the region end. Existing projects use offset values: volume zero is neutral,
minus one is silence, and pan zero is centered. Missing metadata retains these
legacy semantics and linear interpolation with neutral boundary ramps.

New physical volume lanes use decibels from -120 dB (silence) to +24 dB, with
0 dB meaning unity. Physical pan lanes use stereo balance from -1 (left) through
0 (center) to +1 (right). Balance attenuates the opposite channel; it does not
spread a mono source into independent stereo content. Physical units are currently
supported for region volume and pan. Instrument-parameter lanes retain their
existing offset semantics.

Each point chooses the outgoing segment shape: linear, hold, or smooth
(smoothstep). The first and last boundaries independently choose a neutral
baseline ramp or holding the nearest point. Empty lanes remain neutral.
Points must have finite values, valid curve metadata, unique frames, and lie
within the region. A moved point colliding with another point is refused.
Inserting at an existing frame replaces that point's value and retains its curve.

The graphical timeline and inspector use the same application commands for
point insertion, movement, and deletion. Complete lane replacement prepares both
undo snapshots before publishing. A drag produces one history entry; a selection
without a change produces none. History binds to stable track and region IDs,
so inserted tracks cannot redirect automation undo or redo. Other active edits
retain their history ownership. The displayed curve uses the playback evaluator;
physical gain display maps silence to the bottom, unity to the center, and +24 dB
to the top.

Point dragging retains the cursor's offset inside the six-pixel point hitbox and
ignores movement smaller than three pixels until a drag begins. Alt/Option bypasses
timeline grid snapping in both the timeline and inspector.

## MCP region control

The source adapter exposes `get_automation`, `set_automation`, `undo_automation`,
and `redo_automation`. Use the generation, track ID and clip ID from
`get_arrangement`, with automation target kind `region`. Both audio and MIDI
regions support volume (target 0) and stereo balance (target 1); MIDI regions
also expose supported instrument targets in `available_targets`.

Reads return at most 128 points per page and include exact units, boundary
policies and a retained full-arrangement precondition. Pass that precondition
for subsequent pages and writes. A write replaces one complete lane with at
most 128 sorted points, preserves other lanes, and uses the graphical editor's
history command. Empty point lists neutralize a lane. Physical volume values
are in dB, physical balance values in -1..1, and instrument values remain offsets.

Writes require write admission, the endpoint instance and an immutable request
ID. Exact request replay returns its original result; `request_status` recovers
an uncertain outcome. Changed arrangement snapshots, unrelated active edits,
wrong region identities and mismatched history cursors are refused. The existing
four-state arrangement cache and 128-receipt endpoint limits apply. These tools
have passed official SDK, replay, read-only, pagination, audio/MIDI-region and
save/reopen render-parity tests in an isolated fixture.

## Precision and curve controls

With an automation point selected, 1/2/3 choose linear, hold or smooth outgoing
segments. Left/right move by the visible grid interval; Alt/Option plus left/right
moves by one sample. Up/down changes physical gain by 0.1 dB and other lanes by
0.01 units. Shift multiplies a nudge by ten. Brackets toggle the before/after
boundary between baseline and hold. Delete removes the point with undo support.
The timeline status strip shows the selected value, units, exact frame and curve.
These keys leave inspector text fields and command shortcuts with their existing
owners; a held key does not create repeated discrete history steps.

The saved seven-track, forty-region Night Pocket project has been tested through
MCP in an isolated copy. Its callback pluck and pad were given smooth gain rises
and opposite stereo balances. All MIDI notes and region timings remained intact;
the original saved file stayed unchanged. The edited preview survived save/reopen
with an identical rendered PCM fingerprint. Native Vulkan validation and the
headless stable regression lane pass.

Package/runtime acceptance is the final integration gate. A Main Edit refresh
is local development qualification and does not constitute a public release.

## Target selection

The target button opens a fixed dropdown with all 26 full parameter names.
Timeline selection changes never remove or rearrange entries. Instrument parameters
are dimmed and disabled without a selected MIDI region; the footer explains
“Instrument parameters disabled: select a MIDI region.” Clicking or pressing Enter
on a disabled row keeps the menu open and leaves the target and song unchanged. Selecting
a row enables automation and selects that lane directly. Escape or an outside click
dismisses it; dismissal owns the release and cannot insert notes or points underneath.
Mouse focus no longer draws the keyboard underline; active toolbar toggles use a filled
state and accent border. The target button includes a dropdown indicator.

A fresh pointer-down after window focus or layout cancellation starts a new gesture.
Held motion remains blocked until release, avoiding the previous lost first click.

Instrument target availability follows the effective region voice, including track
inheritance and region overrides. All rows remain visible; unsupported controls
are dimmed and cannot be selected. Hovering a dimmed row explains that the selected
instrument does not use it. MCP `available_targets` includes only active controls.
Existing dormant lane data remains readable and preserved for preset changes;
parameter influence can still depend on settings such as oscillator mix or vibrato depth.
