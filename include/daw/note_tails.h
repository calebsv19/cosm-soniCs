#pragma once
#include "engine/note_ending.h"
#define DAW_TAIL_THRESHOLD 0.0001
struct AppState;struct DawMixCapture;struct cJSON;
// Retains authored identity alongside prepared note fences and nearest later authored track onset.
typedef struct DawNoteEnding {
    int source,note_index;uint64_t authored_start,authored_gate,next_onset;
    uint8_t pitch;float velocity;bool prepared,eligible;
    EngineNoteEnding production;
} DawNoteEnding;
// Accumulates threshold-qualified region source activity and post-last-damper tail observations.
typedef struct DawSourceTailChannel {
    uint64_t samples,active,first,last,post_samples,post_active,post_last;
    double peak,power,post_peak,post_power;
} DawSourceTailChannel;
// Owns copied note rows and source-local pre-graph-gain observations for one private capture.
typedef struct DawNoteTails {
    int count,base[256];DawNoteEnding* notes;
    bool prepared[256];uint64_t last_gate[256];
    DawSourceTailChannel source[256][8];
} DawNoteTails;
// Copies bounded authored descriptors before the export graph is prepared.
bool daw_note_tails_prepare(struct AppState*,struct DawMixCapture*);
// Reads actual prepared source fences and returns its stable capture slot.
int daw_note_tails_source(void*,uint64_t,uint64_t,struct EngineInstrumentSource*);
// Measures private source output before track/region gain, with no live work or retained PCM.
bool daw_note_tails_observe(void*,int,uint64_t,const float*,int,int);
// Serializes one prepared note and range-qualified control provenance.
void daw_note_ending_json(struct cJSON*,const struct DawMixCapture*,int);
// Serializes region-local measured source tails without claiming independent note attribution.
void daw_source_tail_json(struct cJSON*,const struct DawMixCapture*,int);
