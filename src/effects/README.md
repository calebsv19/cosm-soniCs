# Effects implementation

`effects_manager.c` owns the registered effect factories, per-track and master chains, parameter targets, DSP handles, and processing scratch.

The S1 ownership bridge adds two operations:

- `fxm_clone_for_render` prepares independently owned DSP handles from an exclusively control-owned manager, retaining effect IDs and parameter targets. It leaves meter/scope callbacks unset for the caller to bind to the prepared render state.
- `fxm_transfer_render_state` exchanges compatible DSP handles at an exclusive block boundary, preserving processing histories and smoothing state without allocation or destruction. The old revision retains all superseded handles for deferred cleanup. Track mappings must be one-to-one and based on stable runtime identities.

The engine mixer now uses these prepared revisions. The editable effect manager is no longer processed by the mixer. Track insertion/removal moves control chains without recreating surviving effect identities. Parameter edits prepare new targets off-thread; adoption retains compatible DSP handles and current values so the existing render-side smoothing applies those targets without resetting histories. Existing effect setter allocation/DSP behavior remains part of the S3/S4 audit. See [the S1 checkpoint](../../docs/improvement/S1-CHECKPOINT.md) for the remaining analyzer, parameter, and lifecycle work.

Parameter mutation rejects non-finite values, invalid indices/modes, and missing setters before changing metadata. Engine-level structural edits prepare a candidate control manager; failed revision preparation leaves the original chain and instance IDs intact. Registry entry borrows from the engine are control-thread-only and expire at the next structural track or FX edit.

`fxm_reset_render_state` clears histories on an exclusively owned render manager and snaps its parameter smoothing to accepted targets. Explicit transport discontinuities use this after releasing the callback exclusion window; ordinary loop wraps preserve effect tails. No control-manager or shared render history is mutated by this operation.

S3.3 adds built-in API v2 optional active/max-latency and applied-reduction queries. `fxm_clone_for_render` prepares track compensation rings off-thread. The mixer calls `fxm_begin_render_block` once, renders and aligns each track, then renders the master. Direct manager users must call `fxm_prepare_delay_compensation` after structural changes before using alignment. Compatible revisions transfer alignment history; timing changes explicitly reset effect/compensation history. Limiter setters no longer allocate. Both compressor variants share `effects/dynamics_math.h`. See [S3.3](../../docs/improvement/S3-DYNAMICS.md).

S3.4's Gain effect advertises sample-domain smoothing and ramps amplitude over 20 ms; the manager supplies accepted targets without a second smoothing stage. Existing zero-latency effects crossfade bypass over 5 ms using prepared dry scratch, retaining in-flight blend state across compatible revisions. Latency-bearing effects retain the S3.3 reset contract. See [controls and transitions](../../docs/improvement/S3-CONTROL-TRANSITIONS.md).
