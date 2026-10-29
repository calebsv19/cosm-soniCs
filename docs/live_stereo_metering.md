# Live channel and stereo/mono compatibility

Main Edit provides **Meters** in the effects toolbar. It measures child tracks, nested group buses, returns and the master using the actual live production path. No meter insert is needed.

## Native workflow

1. Open Meters. Master shows the final sanitized production output; Selected track follows the selected child/group/return row. Timeline selection, transport controls and global shortcuts remain usable while the meter pane stays open. Controls hidden beneath the pane have no pointer hit regions.
2. Read left/right sample peak and RMS in dBFS, per-window over-range counts, stereo correlation, energy balance, mid/side RMS and mono-fold loss. The exact measurement window is 400 ms. Warm-up, invalid samples and stale/frozen data are explicit; a source can be silent without being invalid.
3. Stereo out and Mono out select a **global listening-only** mode. Scope chooses the meter, never which tracks are folded. Mono averages `(L+R)/2` into both output channels, with a five-millisecond transition and no boost. Switching during playback preserves the cursor. The toolbar displays Mono Monitor while it is selected, even after closing the pane.

Monitoring requires an open stereo endpoint. It also applies to captured previews at the final listening callback, but the production meters exclude captured-preview PCM. Closing/focus loss closes the pane and retains the explicit monitor mode; endpoint reopen and project replacement restore Stereo. Monitoring is ephemeral, never saved in the musical project, and does not consume history or alter gains, routing, captures or exported PCM.

## MCP

The source adapter discovers 70 tools, including read-only `get_live_meters(target=None)` and guarded `set_monitor_mode`. Omit target for master; use a current discovered track target for a child/group/return. MIDI-region targets are not supported on this live surface.

Readback includes exact generation/track IDs, rendered window start/end, sample rate, processing latency, sequence, age/freshness, channel peak/RMS/dBFS, full-scale/overload sample counts and qualified stereo/mono values. `playing` is requested transport state. Fresh means a valid window was computed within one second; it is not a hardware presentation-clock guarantee. After stopping, previously valid windows remain readable and become stale/frozen rather than decaying into fabricated current levels.

`set_monitor_mode(instance=instance, request_id=uuid, expected_monitor=readback["expected_monitor"], mode="mono")` changes only final listening output. Modes are mono/stereo. Admission binds engine generation, endpoint lifetime, monitor revision and current mode. Reads work on read-only endpoints; writes require the ordinary write-enabled endpoint and immutable receipt. Retry the same request identity/payload or recover its receipt; replay does not reapply monitoring. Native mode buttons capture the same guard at press, rejecting another client's change before release. Refresh expired target or monitor guards.

## Calibration and meaning

- Sample peak is max absolute channel sample over the exact window. RMS is the square root of channel mean-square energy, without decay or stereo cancellation. dBFS is `20 log10(amplitude)` relative to full scale 1. Silence has a null dB value in JSON. Full-scale counts `abs(sample)>=1`; overload counts `abs(sample)>1`.
- Correlation is the uncentered normalized dot product `sum(LR)/sqrt(sum(L²)sum(R²))`. In-phase equals +1; opposite phase equals -1; equal-power uncorrelated audio equals zero. It is unavailable for mono input or a silent/near-zero-energy channel.
- Energy balance is `(R power-L power)/(R power+L power)`: left only -1, right only +1. Availability is explicit; silence does not masquerade as centered sound.
- Mid and side are `(L+R)/2` and `(L-R)/2`. Mono-fold loss compares Mid RMS with the mean stereo channel-energy reference `sqrt((L power+R power)/2)`. Identical channels lose 0 dB; uncorrelated or unilateral audio loses about 3.0103 dB; exact opposite channels cancel completely. A null loss plus full_mono_cancellation=true distinguishes complete cancellation from unavailable silence.

These are sample-peak, unweighted RMS and time-domain compatibility measurements, not live LUFS, true peak, frequency-dependent phase analysis, measured speaker loudness or musical-quality verdicts. The older approximate live LUFS detail is clearly marked unqualified and its hidden mode buttons are disabled. Validated offline LUFS/true-peak analysis remains available through mix captures. Existing meter inserts remain pass-through audio with host block-level observers; their window/smoothing controls are not the new fixed 400 ms production-window contract.

## Ownership and verification

Worker-owned accumulation has constant per-node storage and no allocations or locks. Exact disjoint sample windows are independent of callback block size. Seek and rate/channel/latency discontinuity warm up again. Compatible prepared-graph handoffs preserve actual output-window continuity; a window may span live edits and is not a frozen configuration capture. New track identities warm up independently. Track identity guards reject removed/shifted rows. Completed windows piggyback the existing coherent meter publication mutex with render-side try-lock/drop behavior; control copies under the lock and never reads mutable sums. Offline captures/exports do not publish live observations.

The output-only fold uses atomic mode intent and callback-owned transition state. It allocates/locks nothing and leaves Stereo samples byte-identical when settled. Native text has persistent per-label backing, preserving dense queued text lifetime.

Private headless fixtures qualify in-phase/opposite/uncorrelated/unilateral/silent/over-range/invalid data at 44.1/48/96 kHz, exact partition parity, genuine other-thread publication contention, callback retargeting, child/group/return/master production factors, stale/endpoint/lifetime guards, native releases, project replacement, history/playhead preservation and byte-identical exports under mono monitoring. Ordinary SDK fixtures verify actual live-path windows, freshness, immutable receipts and read-only refusal. Package proof is separate from physical listening and graphical human acceptance.
