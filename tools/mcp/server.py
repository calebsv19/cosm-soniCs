#!/usr/bin/env python3
"""Expose the DAW's owner-thread instrument actions through the official MCP SDK."""
import json
import os
import socket
from typing import Annotated, Literal

from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations
from pydantic import BaseModel, ConfigDict, Field, StrictInt, StrictFloat, StrictBool

mcp = FastMCP("soniCs local creative control", log_level="WARNING")
MAX_RESPONSE = 131072


class Target(BaseModel):
    """Preserve runtime identities exactly, without floating-point conversion."""
    model_config = ConfigDict(extra="forbid")
    generation: str = Field(pattern=r"^[1-9][0-9]{0,19}$")
    track_id: str = Field(pattern=r"^[1-9][0-9]{0,19}$")
    clip_id: str = Field(pattern=r"^(0|[1-9][0-9]{0,19})$")
    kind: Literal["track", "midi_region"]


def bridge_call(payload: dict) -> dict:
    """Send exactly one request; uncertain transport failures are never automatically replayed."""
    path = os.environ.get("DAW_CONTROL_SOCKET")
    if not path:
        raise ValueError("Set DAW_CONTROL_SOCKET to the running DAW's private endpoint.")
    raw = json.dumps(payload, sort_keys=True, separators=(",", ":"), allow_nan=False).encode() + b"\n"
    if len(raw) > 16384:
        raise ValueError("Request exceeds the 16 KiB bridge limit.")
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(5)
            connection.connect(path)
            connection.sendall(raw)
            data = bytearray()
            while len(data) <= MAX_RESPONSE:
                chunk = connection.recv(min(8192, MAX_RESPONSE + 1 - len(data)))
                if not chunk:
                    raise ConnectionError("Endpoint closed before a complete response.")
                data.extend(chunk)
                if b"\n" in chunk:
                    return json.loads(data.split(b"\n", 1)[0])
            raise ValueError("Bridge response exceeds limit.")
    except (OSError, ConnectionError) as error:
        raise RuntimeError("DAW connection failed; a sent edit may have applied. Query request_status with the same instance and request_id before retrying.") from error


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def capabilities() -> dict:
    """Read endpoint identity, write admission, presets, defaults, ranges and visible controls."""
    return bridge_call({"op": "capabilities"})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def inspect_project(offset: int = Field(default=0, ge=0, le=1000000)) -> dict:
    """List up to 64 existing track and MIDI-region instrument targets; -1 ends pagination."""
    return bridge_call({"op": "project", "offset": offset})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def get_instrument(target: Target) -> dict:
    """Read stored and effective settings plus the exact expected_state required for writes."""
    return bridge_call({"op": "get_instrument", "target": target.model_dump()})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def set_instrument(instance: str, request_id: str = Field(min_length=1, max_length=64),
                   target: Target = ..., expected_state: str = Field(max_length=8192),
                   preset: str | None = None, parameters: dict[str, Annotated[float, Field(strict=True, allow_inf_nan=False)]] | None = None) -> dict:
    """Apply one undoable instrument patch. Use capability instance, a fresh UUID request_id,
    and the last get_instrument expected_state. Check ok/code in every result. Replay an
    uncertain request with identical arguments and ID; never invent a new ID to retry it.
    Accepted authored readback does not prove audio-worker adoption or audible quality.
    """
    return bridge_call({"op": "set_instrument", "instance": instance, "request_id": request_id,
                        "target": target.model_dump(), "expected_state": expected_state,
                        "preset": preset, "parameters": parameters or {}})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def request_status(instance: str, request_id: str = Field(min_length=1, max_length=64)) -> dict:
    """Recover a terminal edit receipt. Receipts last for this endpoint lifetime only;
    stale_instance after restart means prior outcome must be reconciled from project state.
    """
    return bridge_call({"op": "request_status", "instance": instance, "request_id": request_id})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def undo_instrument(instance: str, request_id: str = Field(min_length=1, max_length=64),
                    target: Target = ..., expected_state: str = Field(max_length=8192)) -> dict:
    """Undo only the top instrument edit if it addresses this target and state still matches."""
    return bridge_call({"op": "undo_instrument", "instance": instance, "request_id": request_id,
                        "target": target.model_dump(), "expected_state": expected_state})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def redo_instrument(instance: str, request_id: str = Field(min_length=1, max_length=64),
                    target: Target = ..., expected_state: str = Field(max_length=8192)) -> dict:
    """Redo only the next instrument edit if it addresses this target and state still matches."""
    return bridge_call({"op": "redo_instrument", "instance": instance, "request_id": request_id,
                        "target": target.model_dump(), "expected_state": expected_state})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def list_presets(search: str = Field(default="", max_length=95), offset: int = Field(default=0, ge=0, le=1000000)) -> dict:
    """List up to 32 reusable user sounds with complete settings and revisions; -1 ends pagination."""
    return bridge_call({"op": "list_presets", "search": search, "offset": offset})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def get_preset(preset_id: str, preset_revision: str | None = None) -> dict:
    """Read a portable user sound; supply its revision to detect library changes."""
    payload = {"op": "get_preset", "preset_id": preset_id}
    if preset_revision is not None:
        payload["preset_revision"] = preset_revision
    return bridge_call(payload)


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def save_preset(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
                expected_state: str = Field(max_length=8192), name: str = Field(min_length=1, max_length=95),
                preset_id: str | None = None, preset_revision: str | None = None) -> dict:
    """Save the target's effective sound as a new reusable preset. To explicitly update a library sound,
    supply both its ID and current revision; the previous file remains recoverable. Saving does not
    alter project settings or add project undo history. Use immutable request IDs for uncertain retries.
    """
    return bridge_call({"op": "save_preset", "instance": instance, "request_id": request_id,
        "target": target.model_dump(), "expected_state": expected_state, "name": name,
        "preset_id": preset_id, "preset_revision": preset_revision})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def apply_preset(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
                 expected_state: str = Field(max_length=8192), preset_id: str = ..., preset_revision: str = ...) -> dict:
    """Copy a complete library revision into the project through reserved instrument history.
    Project playback remains independent of later library updates or deletion. Undo restores the prior sound.
    """
    return bridge_call({"op": "apply_preset", "instance": instance, "request_id": request_id,
        "target": target.model_dump(), "expected_state": expected_state, "preset_id": preset_id,
        "preset_revision": preset_revision})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def preview_preset(instance: str, request_id: str = Field(min_length=1, max_length=64), preset_id: str = ...,
                   preset_revision: str = ..., mode: Literal["note", "chord"] = "chord") -> dict:
    """Write a standardized three-second 48 kHz stereo PCM WAV in the personal library's Previews folder.
    Uses an isolated voice at original level, with no effects or normalization; reports peak/RMS.
    Requires write admission because it publishes a file. Project and audio device remain unchanged.
    """
    return bridge_call({"op": "preview_preset", "instance": instance, "request_id": request_id,
        "preset_id": preset_id, "preset_revision": preset_revision, "mode": mode})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def analyze_instrument(target: Target, mode: Literal["note", "chord"] = "note",
                       window: Literal["attack", "sustain", "release"] = "sustain") -> dict:
    """Measure a captured project sound privately at original level. Returns exact state and fixed-format
    waveform/RMS-envelope/Hann tonal spectrum; no effects, files, device changes or history edits.
    Analysis describes sound, not perceived quality. Refresh after edits; data is a captured snapshot.
    """
    return bridge_call({"op":"analyze_instrument", "target":target.model_dump(), "mode":mode, "window":window})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def analyze_preset(preset_id: str, preset_revision: str, mode: Literal["note", "chord"] = "note",
                   window: Literal["attack", "sustain", "release"] = "sustain") -> dict:
    """Measure one exact library revision without publishing files. Returns original-level 48 kHz C4/chord
    metrics and bounded plots with explicit window/calibration metadata; rejects stale revisions.
    """
    return bridge_call({"op":"analyze_preset", "preset_id":preset_id, "preset_revision":preset_revision,
                        "mode":mode, "window":window})


class MidiNote(BaseModel):
    """Describe a region-relative event with exact frame strings and normalized velocity."""
    model_config = ConfigDict(extra="forbid")
    start_frame: str = Field(pattern=r"^(0|[1-9][0-9]{0,19})$")
    duration_frames: str = Field(pattern=r"^[1-9][0-9]{0,19}$")
    pitch: int = Field(strict=True, ge=0, le=127)
    velocity: float = Field(strict=True, ge=0, le=1, allow_inf_nan=False)


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def get_midi_region(target: Target, offset: int = Field(default=0, ge=0, le=4096),
                    tempo_offset: int = Field(default=0, ge=0, le=1024),
                    expected_state: str | None = Field(default=None, max_length=80)) -> dict:
    """Read exact region bounds plus up to 128 notes and 64 tempo events. Frame values are decimal
    strings; -1 ends each page stream. Reuse expected_state on subsequent pages to reject mixed states.
    Tokens retain eight exact snapshots per endpoint and may expire; refresh on state_conflict.
    Reads do not edit project/history or play audio.
    """
    payload = {"op": "get_midi_region", "target": target.model_dump(), "offset": offset, "tempo_offset": tempo_offset}
    if expected_state is not None:
        payload["expected_state"] = expected_state
    return bridge_call(payload)


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def set_midi_notes(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
                   expected_state: str = Field(max_length=80), mode: Literal["append", "replace"] = ...,
                   notes: list[MidiNote] = Field(max_length=64)) -> dict:
    """Append or explicitly replace a region's notes as one undo step using its current MIDI token.
    Maximum 64 supplied events and 4096 region notes. Empty replace explicitly clears the region.
    Requires stopped playback/recording/Test and no active gesture; bounds stay fixed. Check ok/code.
    Use a fresh request UUID; recover uncertain results with request_status before retrying identically.
    Returned pages are captured readback; refresh if readback_required or a retained token expires.
    """
    return bridge_call({"op": "set_midi_notes", "instance": instance, "request_id": request_id,
                       "target": target.model_dump(), "expected_state": expected_state, "mode": mode,
                       "notes": [note.model_dump() for note in notes]})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def create_midi_pattern(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
                        expected_state: str = Field(max_length=80), mode: Literal["append", "replace"] = ...,
                        pattern: Literal["melody", "chords", "arpeggio"] = ...,
                        root_note: int = Field(default=60, strict=True, ge=0, le=127),
                        start_beat: float = Field(default=0, ge=0, le=1000000, allow_inf_nan=False, strict=True),
                        velocity: float = Field(default=0.65, gt=0, le=1, allow_inf_nan=False, strict=True)) -> dict:
    """Generate a deterministic four-quarter-beat major-key phrase through the captured tempo map.
    root_note is the tonic MIDI pitch; start_beat is relative to region start. Melody/arpeggio have eight
    onsets; chords use I/vi/IV/V triads. Uses one common-action edit/undo step, refuses trimmed regions,
    pitch overflow and insufficient space. Does not create/extend regions or change instrument settings.
    Require current get_midi_region token and a fresh request UUID; reconcile uncertain receipts first.
    """
    return bridge_call({"op": "create_midi_pattern", "instance": instance, "request_id": request_id,
                       "target": target.model_dump(), "expected_state": expected_state, "mode": mode,
                       "pattern": pattern, "root_note": root_note, "start_beat": start_beat, "velocity": velocity})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def undo_midi(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
              expected_state: str = Field(max_length=80)) -> dict:
    """Undo only the top common MIDI action for this region while exact current state still matches."""
    return bridge_call({"op": "undo_midi", "instance": instance, "request_id": request_id,
                       "target": target.model_dump(), "expected_state": expected_state})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def redo_midi(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
              expected_state: str = Field(max_length=80)) -> dict:
    """Redo only the next common MIDI action for this region while exact current state still matches."""
    return bridge_call({"op": "redo_midi", "instance": instance, "request_id": request_id,
                       "target": target.model_dump(), "expected_state": expected_state})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def get_midi_layout(offset: int = Field(default=0, ge=0, le=128),
                    clip_offset: int = Field(default=0, ge=0, le=2048),
                    tempo_offset: int = Field(default=0, ge=0, le=1024),
                    expected_layout: str | None = Field(default=None, max_length=80)) -> dict:
    """Read exact project placement and tempo, including zero tracks. Pages contain up to 64 track
    handles, 128 placements and 64 tempo events; -1 ends each stream. Reuse expected_layout for
    consistent pages and structural writes. Four exact layout states are retained per endpoint.
    Notes, sound, mixer and names are outside this placement token. Reads never create tracks.
    """
    payload = {"op": "get_midi_layout", "offset": offset, "clip_offset": clip_offset, "tempo_offset": tempo_offset}
    if expected_layout is not None:
        payload["expected_layout"] = expected_layout
    return bridge_call(payload)


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def create_midi_region(instance: str, request_id: str = Field(min_length=1, max_length=64),
                       expected_layout: str = Field(max_length=80),
                       start_frame: str = Field(pattern=r"^(0|[1-9][0-9]{0,19})$"),
                       duration_frames: str = Field(pattern=r"^[1-9][0-9]{0,19}$"),
                       target: Target | None = None) -> dict:
    """Create an empty nonoverlapping MIDI region as one undo step. Supply an existing track target,
    or omit target to atomically create one new trailing track with its first region. Use current layout
    token, endpoint instance and a fresh UUID; frame strings preserve exact timing. Returns created_target
    for get_midi_region/instrument/pattern tools. Requires stopped playback, recording/Test and gestures.
    Recover uncertain requests with request_status before retrying identically. No implicit note creation.
    """
    payload = {"op": "create_midi_region", "instance": instance, "request_id": request_id,
               "expected_layout": expected_layout, "start_frame": start_frame, "duration_frames": duration_frames}
    if target is not None:
        payload["target"] = target.model_dump()
    return bridge_call(payload)


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def extend_midi_region(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
                       expected_state: str = Field(max_length=80),
                       duration_frames: str = Field(pattern=r"^[1-9][0-9]{0,19}$")) -> dict:
    """Extend an untrimmed region's right edge as one undo step against its exact MIDI token.
    duration_frames is the new total length, not an increment. Preserve notes, start, offset and sound;
    refuse shortening, overflow or overlap with neighboring audio/MIDI clips. Equal duration is a no-op.
    Use stopped creative editing and immutable request IDs; follow with normal note/pattern writes.
    """
    return bridge_call({"op": "extend_midi_region", "instance": instance, "request_id": request_id,
                       "target": target.model_dump(), "expected_state": expected_state, "duration_frames": duration_frames})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def undo_midi_region(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
                     expected_layout: str = Field(max_length=80)) -> dict:
    """Undo only matching region creation/extension history with exact current placement state.
    Undoing creation requires a pristine empty region; a created track must retain its original settings
    and have no extra clips/effects/automation. Undo note/sound edits first with their own tools.
    """
    return bridge_call({"op": "undo_midi_region", "instance": instance, "request_id": request_id,
                       "target": target.model_dump(), "expected_layout": expected_layout})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def redo_midi_region(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
                     expected_layout: str = Field(max_length=80)) -> dict:
    """Redo matching structural history with a current layout token, including an absent undone region.
    Reuse the original target; accepted redo preserves its track/clip identities for later note history.
    Refuse changed layout, occupied placement or unrelated history without consuming the command.
    """
    return bridge_call({"op": "redo_midi_region", "instance": instance, "request_id": request_id,
                       "target": target.model_dump(), "expected_layout": expected_layout})



@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def list_effects(offset: int = Field(default=0, ge=0, le=1000000)) -> dict:
    """Discover eight effect types per page, including physical parameter ranges and enum labels."""
    return bridge_call(dict(op="list_effects", offset=offset))

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def get_effect_chain(target: Target | None = None) -> dict:
    """Read a complete ordered track chain, or the master chain when target is omitted."""
    return bridge_call(dict(op="get_effect_chain", **({"target": target.model_dump()} if target else {})))

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def edit_effect_chain(instance: str, request_id: str, expected_state: str,
                      action: Literal["add", "remove", "reorder", "enable", "parameter"],
                      target: Target | None = None, type_id: StrictInt | None = None, effect_id: StrictInt | None = None,
                      position: StrictInt | None = None, enabled: StrictBool | None = None, parameter: StrictInt | None = None,
                      value: StrictFloat | None = None, mode: Literal["native", "beats", "beat_rate"] | None = None,
                      beats: StrictFloat | None = None) -> dict:
    """Apply one exact-state guarded effect-chain edit as one reserved undo step."""
    payload = dict(op="edit_effect_chain", instance=instance, request_id=request_id, expected_state=expected_state, action=action)
    if target: payload["target"] = target.model_dump()
    for key, item in dict(type_id=type_id, effect_id=effect_id, position=position, enabled=enabled, parameter=parameter, value=value, mode=mode, beats=beats).items():
        if item is not None: payload[key] = item
    return bridge_call(payload)

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def undo_effect_chain(instance: str, request_id: str, expected_state: str, target: Target | None = None) -> dict:
    """Undo only matching guarded chain history; omitted target selects master."""
    return bridge_call(dict(op="undo_effect_chain", instance=instance, request_id=request_id, expected_state=expected_state, **({"target": target.model_dump()} if target else {})))

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def redo_effect_chain(instance: str, request_id: str, expected_state: str, target: Target | None = None) -> dict:
    """Redo matching complete chain history with preserved effect identities."""
    return bridge_call(dict(op="redo_effect_chain", instance=instance, request_id=request_id, expected_state=expected_state, **({"target": target.model_dump()} if target else {})))


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def analyze_composition(start_frame: str = Field(pattern=r"^(0|[1-9][0-9]{0,19})$"),
                        end_frame: str = Field(pattern=r"^[1-9][0-9]{0,19}$"), target: Target | None = None,
                        tail_frames: str = Field(default="0", pattern=r"^(0|[1-9][0-9]{0,19})$"),
                        window_frame: str = Field(default="0", pattern=r"^(0|[1-9][0-9]{0,19})$"), dry: StrictBool = False) -> dict:
    """Capture original-level production audio and analysis; omit target for project, use track/region to isolate."""
    return bridge_call(dict(op="analyze_composition", start_frame=start_frame, end_frame=end_frame, tail_frames=tail_frames,
                           window_frame=window_frame, dry=dry, **({"target": target.model_dump()} if target else {})))

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def preview_composition(instance: str, request_id: str = Field(min_length=1, max_length=64), render_id: str = ...) -> dict:
    """Publish a PCM16 WAV from the immutable analyzed render; clipping refuses rather than normalizes."""
    return bridge_call(dict(op="preview_composition", instance=instance, request_id=request_id, render_id=render_id))

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def list_effect_presets(search: str = Field(default="", max_length=95), offset: int = Field(default=0, ge=0, le=128)) -> dict:
    """List eight reusable effect chains per page without creating library storage."""
    return bridge_call(dict(op="list_effect_presets", search=search, offset=offset))

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def get_effect_preset(preset_id: str, preset_revision: str | None = None) -> dict:
    """Read a complete portable effect chain and optionally require its current library revision."""
    return bridge_call(dict(op="get_effect_preset", preset_id=preset_id, **({"preset_revision":preset_revision} if preset_revision is not None else {})))

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def save_effect_preset(instance: str, request_id: str = Field(min_length=1,max_length=64), expected_state: str = ..., name: str = Field(max_length=95), target: Target | None = None, preset_id: str | None = None, preset_revision: str | None = None) -> dict:
    """Save an exact current track/master chain; updating requires an explicit preset ID and current revision."""
    payload=dict(op="save_effect_preset",instance=instance,request_id=request_id,expected_state=expected_state,name=name)
    if target is not None:payload["target"]=target.model_dump()
    if preset_id is not None:payload["preset_id"]=preset_id
    if preset_revision is not None:payload["preset_revision"]=preset_revision
    return bridge_call(payload)

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def apply_effect_preset(instance: str, request_id: str = Field(min_length=1,max_length=64), expected_state: str = ..., preset_id: str = ..., preset_revision: str = ..., target: Target | None = None) -> dict:
    """Replace an exact track/master chain with a current library revision as one guarded undo step."""
    return bridge_call(dict(op="apply_effect_preset",instance=instance,request_id=request_id,expected_state=expected_state,preset_id=preset_id,preset_revision=preset_revision,**({"target":target.model_dump()} if target is not None else {})))

@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def transform_midi(instance: str, request_id: str = Field(min_length=1, max_length=64), target: Target = ...,
                   expected_state: str = ..., operation: Literal["quantize", "velocity", "humanize"] = ...,
                   grid_beats: float | None = Field(default=None, ge=.03125, le=4),
                   strength: float | None = Field(default=None, ge=0, le=1),
                   swing: float | None = Field(default=None, ge=0, le=.75),
                   velocity_scale: float | None = Field(default=None, ge=0, le=4),
                   timing_ms: float | None = Field(default=None, ge=0, le=100),
                   velocity_amount: float | None = Field(default=None, ge=0, le=.5),
                   seed: StrictInt | None = Field(default=None, ge=0, le=4294967295)) -> dict:
    """Transform all region notes as one undo step: quantize needs grid/strength/swing; velocity needs scale; humanize needs timing/velocity/seed. Omit unrelated settings. Timing overflow and duplicate results refuse the entire edit."""
    payload=dict(op="transform_midi",instance=instance,request_id=request_id,target=target.model_dump(),expected_state=expected_state,operation=operation)
    for key,value in dict(grid_beats=grid_beats,strength=strength,swing=swing,velocity_scale=velocity_scale,timing_ms=timing_ms,velocity_amount=velocity_amount,seed=seed).items():
        if value is not None:payload[key]=value
    return bridge_call(payload)



class ArrangementEdit(BaseModel):
    """Describe one explicit existing MIDI-region operation with stable handles."""
    model_config = ConfigDict(extra="forbid")
    operation: Literal["duplicate", "move", "trim", "delete"]
    target: Target
    destination: Target | None = None
    start_frame: str | None = Field(default=None, pattern=r"^(0|[1-9][0-9]{0,19})$")
    duration_frames: str | None = Field(default=None, pattern=r"^[1-9][0-9]{0,19}$")


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def get_arrangement(expected_arrangement: str | None = None) -> dict:
    """Capture complete authored placement, tempo, notes, sound and automation guards.
    Readback lists up to 128 tracks and 256 placements. Four endpoint captures are retained.
    Optional expected_arrangement requires that exact captured state to remain current.
    """
    payload = {"op": "get_arrangement"}
    if expected_arrangement is not None:
        payload["expected_arrangement"] = expected_arrangement
    return bridge_call(payload)


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def arrange_regions(instance: str, request_id: str = Field(min_length=1, max_length=64),
                    expected_arrangement: str = ..., edits: list[ArrangementEdit] = Field(min_length=1, max_length=16)) -> dict:
    """Apply duplicate, move, right-edge trim or delete as one guarded atomic undo step.
    Move/duplicate require start_frame and may name an existing destination track.
    Trim requires duration_frames only and refuses discarded notes. Delete takes target only.
    Overlaps refuse the entire batch; cross-lane inherited sounds become explicit region settings.
    Output targets correspond to input edits, with null for deletion. Recover uncertain writes with request_status.
    """
    return bridge_call({"op": "arrange_regions", "instance": instance, "request_id": request_id,
                        "expected_arrangement": expected_arrangement,
                        "edits": [edit.model_dump(exclude_none=True) for edit in edits]})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def undo_arrangement(instance: str, request_id: str, expected_arrangement: str) -> dict:
    """Undo only an arrangement command at the shared history cursor after full current-state checks."""
    return bridge_call({"op": "undo_arrangement", "instance": instance, "request_id": request_id, "expected_arrangement": expected_arrangement})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def redo_arrangement(instance: str, request_id: str, expected_arrangement: str) -> dict:
    """Redo an arrangement command with exact guards and the same accepted region identities."""
    return bridge_call({"op": "redo_arrangement", "instance": instance, "request_id": request_id, "expected_arrangement": expected_arrangement})


class RoutingRoleEdit(BaseModel):
    """Change an existing empty lane's return role within one guarded final graph."""
    model_config = ConfigDict(extra="forbid")
    operation: Literal["role"]
    target: Target
    is_return: StrictBool


class RoutingSendEdit(BaseModel):
    """Set one post-fader send using a stable return target and physical linear gain."""
    model_config = ConfigDict(extra="forbid")
    operation: Literal["send"]
    target: Target
    slot: StrictInt = Field(ge=0, le=3)
    return_target: Target | None
    gain: StrictFloat = Field(ge=0, le=1)


RoutingEdit = Annotated[RoutingRoleEdit | RoutingSendEdit, Field(discriminator="operation")]


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=True))
def get_routing() -> dict:
    """Capture complete track roles and post-fader sends with an exact routing state token."""
    return bridge_call({"op": "get_routing"})


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def create_return(instance: str, request_id: str, expected_routing: str) -> dict:
    """Create one empty return with guarded history; works from an empty project."""
    return bridge_call(dict(op="create_return", instance=instance, request_id=request_id, expected_routing=expected_routing))


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def edit_routing(instance: str, request_id: str, expected_routing: str,
                 edits: list[RoutingEdit] = Field(min_length=1, max_length=16)) -> dict:
    """Apply one atomic role/send batch; linear gains 0..1, no return feedback, stopped writes only."""
    return bridge_call(dict(op="edit_routing", instance=instance, request_id=request_id, expected_routing=expected_routing,
                            edits=[edit.model_dump() for edit in edits]))


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def undo_routing(instance: str, request_id: str, expected_routing: str) -> dict:
    """Undo matching routing/return creation at the shared cursor without losing later lane edits."""
    return bridge_call(dict(op="undo_routing", instance=instance, request_id=request_id, expected_routing=expected_routing))


@mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True))
def redo_routing(instance: str, request_id: str, expected_routing: str) -> dict:
    """Redo matching routing history with the same accepted return identities."""
    return bridge_call(dict(op="redo_routing", instance=instance, request_id=request_id, expected_routing=expected_routing))


if __name__ == "__main__":
    mcp.run(transport="stdio")
