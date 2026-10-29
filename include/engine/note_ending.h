#pragma once
#include "engine/instrument.h"
// Copies prepared production note fences and automated controls without rendering or modifying the voice.
typedef struct EngineNoteEnding {
    int original_index;
    uint64_t start,key_off,damper_off,source_end,choke,handoff,chain_start;
    bool sampled,piano,one_shot,mono,legato,next_legato;
    EngineInstrumentParams onset_params,key_off_params,boundary_params;
} EngineNoteEnding;
// Reads one prepared descriptor by index; false marks the end of the source's prepared notes.
bool engine_instrument_source_note_ending(const EngineInstrumentSource*,int,EngineNoteEnding*);
