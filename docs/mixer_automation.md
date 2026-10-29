# Group-volume and send-level automation

## 1. Native editing

Open **Routing**, select a group or source track, then choose **Auto**. The Lane
button selects the group fader or a bound send slot. Enter an exact project frame
and absolute linear gain; Add creates a point, Set replaces the selected point,
and Delete/Clear remove it. Prev/Next choose points. Curve cycles Linear, Hold,
and Smooth; Before/After choose the static baseline or the nearest held value.
Undo/Redo share guarded routing history with MCP. Refresh explicitly adopts a
changed routing snapshot. Back returns to routing. Enlarge the effects pane to
at least 360 × 275 content pixels if the controls cannot fit.

Editing requires stopped transport, no recording and no conflicting gesture.
Playback evaluates the stored curves; a folded group retains its audio and curve.
This is a numerical point editor with preview, not timeline drag handles.

## 2. Physical and playback contract

Group volume accepts gain 0–10; sends accept 0–1. Zero is silence and one is
unity. Values replace the static fader/send gain while the curve applies; they
are not offsets or dB values. Group gain acts after its inserts/EQ and before pan
and downstream routing. Sends retain the existing post-fader route. Mute/solo
closure remains authoritative even when a curve would otherwise be audible.
A static fader/send at zero does not silence a nonzero active curve.

Each lane has at most 32 strictly increasing unsigned 64-bit project-frame points,
with 256 total points across group faders and sends. Frames use the project sample
rate and do not move with individual regions. Linear and smooth interpolation
connect adjacent points; Hold keeps the outgoing point value until the next point.
Before/After Base uses the current static control, and Hold uses the endpoint.
Empty curves restore static behavior. An empty curve has Base/Base boundaries.

Curves are independently copied into prepared render/export state. Evaluation
uses the corresponding signal's latency-aligned project clock, including nested
bus processing and send compensation; it allocates no memory during rendering.
Changing a curve invalidates an earlier private mix-capture guard.

## 3. Persistence, history and MCP

Native projects embed group and send curves. Exact frame strings and nine-digit
float serialization preserve point identity; legacy projects without these fields
keep their static behavior. Undo/redo atomically restores complete routing state.
A send with an authored curve cannot be disconnected or rebound until the curve
is cleared, so automation cannot silently move to another return.

The existing 61-tool adapter uses `get_routing`, `edit_routing`, `undo_routing`
and `redo_routing`; no additional tool names are required. Obtain fresh targets
and the routing guard from `get_routing`. Supply the existing request identity and
expected routing guard, with edits such as:

```json
{"operation":"volume_automation","target":"<group target>","automation":{
  "before":0,"after":1,"points":[
    {"frame":"0","value":0.0,"curve":0},
    {"frame":"48000","value":1.0,"curve":0}]}}
```

For a source or group send, use `operation: "send_automation"` with `slot: 0..3`
and the same complete automation object. The slot must already have a return.
Boundary enums: 0 Base, 1 Hold. Curve enums: 0 Linear, 1 Hold, 2 Smooth. To clear,
supply `{"before":0,"after":0,"points":[]}`. Do not batch two edits to the same
lane/slot. Invalid fields, duplicates, unsorted frames and out-of-range gains
refuse the whole candidate. Reconcile uncertain outcomes using the existing
request-status receipt, rather than publishing under a new identity.

`get_routing` and immutable mix-capture provenance expose complete authored
curves; a running older package needs the usual closed-app refresh to acquire
new operations. Cached SDK discovery may also need refreshing.

## 4. Verification and remaining scope

Private native/SDK fixtures check exact changing-gain PCM, nested 22-frame latency
compensation, no render allocations, native control geometry and point editing,
zero static gain versus mute, save/reopen audio identity, routing history and
receipt recovery. Headless source/package checks are separate from human GUI and
listening acceptance. No production musical project is used as a writable fixture.

Return/master fader automation, group pan automation, arbitrary effect-parameter
curves, timeline drag handles and live authoring remain separate future work.
