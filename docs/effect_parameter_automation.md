# Selected effect-parameter automation

## 1. Qualified controls and native editing

This slice supports **Gain / gain_db** (-96..24 dB) and **Pan / pan** (-1..1)
on child, group, return and master inserts. These are effect parameters, distinct
from track/group base controls. Pan retains its existing constant-power stereo
law: center attenuates each stereo channel by about 3 dB; mono is unchanged.
Other processor controls are not advertised as curve-supported by this slice.

Choose the desired effects target, then **FX Auto** in the effects toolbar.
The editor selects the selected qualified insert, or the first qualified insert
in that chain. Lane cycles qualified inserts. Enter an exact project frame and
physical value; Add/Set/Delete/Clear edit points. Prev/Next select points, Curve
cycles Linear/Hold/Smooth, and Before/After select Base/Hold extrapolation.
Undo/Redo share effect-chain history with MCP. Refresh explicitly adopts external
chain changes; Back/Escape closes the editor. Point editing and a preview are
available in an effects-pane content area of at least 360 × 275 pixels.

Authoring requires stopped transport and no recording/conflicting gesture.
Playback/rendering evaluates independently copied curves without allocations.
Frames are absolute project sample frames, not region-local beats; moving a
region or reordering an insert retains its project-frame curve and insert binding.
There are 32 points per insert and 256 effect points across the entire project,
separate from the existing mixer curve budget. Only parameter zero is supported
for the two qualified insert types.

## 2. DSP, boundaries and persistence

Values replace the static parameter within the curve; they are not offsets.
Linear interpolation is in physical dB or pan units. Hold uses the outgoing value
until the next point; Smooth uses smoothstep. Before/After Base uses the current
static parameter, and Hold uses the nearest point. The last exact point retains
its authored value. Empty points with Base/Base restore static behavior.

Each insert follows the signal's project clock after incoming bus compensation
and preceding serial processor latency. Authored Gain curves use an exact
render-owned target, while ordinary static Gain edits keep the existing 20 ms
amplitude ramp. Clearing/changing curves does not reuse stale automated Gain
state as a static target. Bypass retains existing semantics and stored curves.

Native projects embed complete curves on track and master effect instances;
legacy projects without curves keep their static settings. Canonical decimal
uint64 frame strings and nine-digit float values preserve point identity.
Invalid ordering, range, fields, binding or total capacity refuses the private
candidate before adoption. Curve changes invalidate exact mix-capture guards;
immutable effect provenance includes authored curves.

Portable effect-chain presets remain static: saving a chain with project-time
curves refuses instead of silently omitting them. Save the project for automation.
The builtin processor source interface is v4, adding an optional exact target
setter; the program development version remains separate.

## 3. Guarded MCP

The adapter retains 61 tools. `list_effects` exposes `automation_supported` on
qualified parameters. `get_effect_chain` includes each insert's `automation`
object or null. Reuse `edit_effect_chain` with `action="automation"`, exact fresh
`expected_state`, `effect_id`, `parameter=0` and complete `automation`:

```json
{"before":0,"after":1,"points":[
  {"frame":"0","value":-12.0,"curve":0},
  {"frame":"48000","value":0.0,"curve":2}
]}
```

For Pan, use values -1..1. Boundary enums are 0 Base / 1 Hold; curve enums are
0 Linear / 1 Hold / 2 Smooth. Clear with `{"before":0,"after":0,"points":[]}`.
Omit target for master or supply a fresh track target for child/group/return.
Use existing `undo_effect_chain` / `redo_effect_chain`; unrelated shared history
cannot be consumed. Reconcile uncertain writes through the existing request
receipt under the same request identity. Unknown/duplicate curve fields,
noncanonical or overflowing frame strings, unsupported parameters and invalid
values refuse the complete candidate.

A closed Main Edit refresh installs the implementation. A future running endpoint
must independently establish identity and discovery; source/package proof does
not imply a live endpoint. Cached SDK discovery may need refreshing.

## 4. Qualification and remaining scope

Private native tests compare every stereo sample with independent dB and
constant-power Pan equations across child/group/return/master routes, including
17-frame processing-delay compensation. Save/reopen reproduces detached audio
exactly. Native point edits, clear, undo/redo, stale guards and range/type refusals
are exercised. Ordinary SDK checks cover signed curve readback, retry recovery,
exact rendered undo/redo, clear-to-static audio and Pan bounds.

Filter/EQ, dynamics, time/tempo-dependent delay/reverb parameters, group pan,
return/master base faders, timeline drag handles and live curve authoring remain
separate work. Headless and installed-package proof are distinct from human
GUI/listening acceptance; no musical project is edited as a test fixture.
