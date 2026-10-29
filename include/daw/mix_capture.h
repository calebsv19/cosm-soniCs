#pragma once
#include "daw/arrangement.h"
#include "daw/mix_frequency.h"
#include "daw/mix_loudness.h"
#include "daw/note_tails.h"
#include "daw/fx_actions.h"
#include "daw/eq_actions.h"
#include "engine/mix_probe.h"
#define DAW_MIX_TRACKS 32
#define DAW_MIX_CHANNELS 8
#define DAW_MIX_BINS 32
#define DAW_MIX_CONTEXT_SECONDS 300
#define DAW_MIX_RANGE_SECONDS 30
// Copies authored node controls and complete processing configuration without DSP history.
typedef struct DawMixNode {
    uint64_t track_id,parent_id;
    bool group,returns,active,muted,solo;
    float gain,pan;
    char name[ENGINE_CLIP_NAME_MAX];
    EngineMixerAutomation volume_automation;
    EngineSend sends[ENGINE_SEND_CAP];
    DawFxSnapshot fx;
    DawEqSnapshot eq;
} DawMixNode;
// Copies one active region's sound provenance and authored gate summary, never mutable note pointers.
typedef struct DawMixSource {
    uint64_t track_id,clip_id,start,duration,gate_min,gate_max,last_note_off;
    bool midi,inherited;
    int notes,region_automation_lanes,track_automation_lanes;
    float gain;
    char name[ENGINE_CLIP_NAME_MAX];
    EngineInstrumentPresetId preset;
    EngineInstrumentParams params;
} DawMixSource;
// Owns exact common arrangement guards plus bounded processing/routing provenance.
typedef struct DawMixSnapshot {
    uint64_t generation,settings_fingerprint;
    int tracks,sources;
    DawArrangementSnapshot arrangement;
    DawMixNode nodes[DAW_MIX_TRACKS+1];
    DawMixSource regions[DAW_ARRANGEMENT_CLIP_CAP];
} DawMixSnapshot;
// Accumulates original-level channel samples and equally spaced range buckets without retaining PCM.
typedef struct DawMixMeter {
    uint64_t active_samples,first_active_frame,last_active_frame;
    double peak,power;
    uint64_t samples,full_scale,overload;
    double bin_peak[DAW_MIX_BINS],bin_power[DAW_MIX_BINS];
    uint64_t bin_samples[DAW_MIX_BINS];
} DawMixMeter;
// Retains one node stage's own latency and independent channel accounting.
typedef struct DawMixStage {
    bool available;
    uint64_t latency;
    DawMixMeter channel[DAW_MIX_CHANNELS];
} DawMixStage;
// Accumulates actual pre-makeup applied attenuation per channel and authored-time bucket.
typedef struct DawMixReduction {
    uint64_t samples,bin_samples[DAW_MIX_BINS];
    double minimum_db,sum_db,bin_minimum_db[DAW_MIX_BINS],bin_sum_db[DAW_MIX_BINS];
} DawMixReduction;
// Retains one serial insert before/after boundary and supported applied reduction.
typedef struct DawMixEffect {
    DawMixStage boundary[2];
    bool reduction_available;
    DawMixReduction reduction[DAW_MIX_CHANNELS];
} DawMixEffect;
// Owns immutable stage measurements and the exact captured configuration until explicitly freed.
typedef struct DawMixCapture {
    bool whole_song;
    uint64_t authored_end,tail_frames;
    DawLoudness* loudness;
    DawNoteTails* tails;
    DawMixFrequency frequency;
    DawMixSnapshot* snapshot;
    uint64_t start,end,rendered_frames,master_latency;
    int channels,sample_rate,block_frames;
    DawMixEffect* effects[DAW_MIX_TRACKS+1];
    DawMixStage stages[DAW_MIX_TRACKS+1][ENGINE_MIX_STAGE_COUNT];
} DawMixCapture;
// Captures a bounded exact project guard on the application control owner.
DawActionStatus daw_action_get_mix_snapshot(const struct AppState*,DawMixSnapshot**);
// Checks current authored content and processing semantically rather than trusting a fingerprint.
bool daw_mix_snapshot_current(const struct AppState*,const DawMixSnapshot*);
// Releases a detached guard without changing the project or live DSP state.
void daw_mix_snapshot_free(DawMixSnapshot*);
// Streams full preceding context through the production graph and measures only the guarded range.
DawActionStatus daw_action_capture_mix(struct AppState*,const DawMixSnapshot*,uint64_t,uint64_t,DawMixCapture**);
// Captures the complete authored programme from zero with explicit bounded tail allowance.
DawActionStatus daw_action_capture_song_loudness(struct AppState*,const DawMixSnapshot*,uint64_t,DawMixCapture**);
// Releases one immutable diagnostic result and its captured metadata.
void daw_mix_capture_free(DawMixCapture*);

// Accumulates one private serial insert and its exact output-aligned applied reduction.
bool daw_mix_observe_effect(void*,int,int,uint64_t,bool,uint64_t,uint64_t,const float*,const float*,int,int);
