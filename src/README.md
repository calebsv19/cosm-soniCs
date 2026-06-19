# Directory: src

Purpose: Engine, UI, and application implementation for the DAW prototype.

## Subdirectories
- `app/`: Program entry point and SDL wiring.
- `core/loop/`: DAW main-thread loop adapter wrappers around shared core runtime primitives; see the local README for the intentional singleton ownership boundary.
- `audio/`: Low-level audio devices, ring buffers, and clip loaders.
- `config/`: Runtime configuration parser.
- `engine/`: Real-time engine core, graph, and sources.
- `input/`: Mouse/keyboard plumbing for transport, timeline, and inspector.
- `ui/`: Rendering and layout code for on-screen panels.
- `session/`: Session/project persistence plus runtime path contract read/write
  code for project reopen, library roots, recorded media, and clip state.
