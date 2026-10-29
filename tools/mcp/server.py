#!/usr/bin/env python3
"""Expose the DAW's owner-thread instrument actions through the official MCP SDK."""
import json
import os
import socket
from typing import Annotated, Literal

from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations
from pydantic import BaseModel, ConfigDict, Field

mcp = FastMCP("soniCs local instrument control", log_level="WARNING")
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


if __name__ == "__main__":
    mcp.run(transport="stdio")
