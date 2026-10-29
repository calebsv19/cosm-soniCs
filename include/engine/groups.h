#pragma once
#include "engine/engine.h"
struct EngineMixState;
typedef struct EngineGroupMix EngineGroupMix;
// Detects whether a project requires nested routing rather than the legacy flat path.
bool engine_groups_present(const EngineTrack*, int count);
// Resolves one immutable runtime track lifetime in a bounded owned array.
int engine_group_index(const EngineTrack*, int count, uint64_t identity);
// Tests parent ancestry without treating a lane as its own descendant.
bool engine_group_descendant(const EngineTrack*, int count, int child, int parent);
// Computes inherited mute and solo closure with the existing source/bus gain convention.
// Reports inherited mute/solo closure independently of the current static fader.
bool engine_group_enabled(const EngineTrack*,int,int);
float engine_group_gain(const EngineTrack*, int count, int track);
// Maps visible preorder rows to stable engine indices without moving processing chains.
int engine_group_visible_track(const EngineTrack*, int count, int row);
// Maps a track to its visible preorder row, or -1 below a folded ancestor.
int engine_group_visible_row(const EngineTrack*, int count, int track);
// Counts the visible preorder projection of the authored hierarchy.
int engine_group_visible_count(const EngineTrack*, int count);
// Counts ancestors for native indentation.
int engine_group_depth(const EngineTrack*, int count, int track);
// Prepares independent DAG sums and maximum-bound per-edge delay rings off the render thread.
EngineGroupMix* engine_group_mix_create(struct EngineMixState*, int block, int channels);
// Releases a detached nested mix and all prepared edge ownership.
void engine_group_mix_destroy(EngineGroupMix*);
// Resets all delayed samples on transport discontinuities.
void engine_group_mix_reset(EngineGroupMix*);
// Transfers compatible surviving edge histories by stable source/destination lifetime.
void engine_group_mix_transfer(EngineGroupMix*, EngineGroupMix*);
// Clears node sums and recomputes actual path latency without render-thread allocation.
bool engine_group_mix_begin(EngineGroupMix*, struct EngineMixState*, int frames, int channels);
// Returns the next dependency-ordered lane or -1 after the final node.
int engine_group_mix_track(const EngineGroupMix*, int position);
// Borrows one node's prepared interleaved input/output buffer for this block.
float* engine_group_mix_buffer(EngineGroupMix*, int track);
// Sends one fully processed lane into compensated parent/return/master inputs exactly once.
void engine_group_mix_forward(EngineGroupMix*, struct EngineMixState*, int track, float* master, int frames, int channels);
// Returns the complete longest track/bus path, including master insert latency.
uint64_t engine_group_mix_latency(const EngineGroupMix*, const struct EngineMixState*);

// Returns node input arrival time, or master input arrival time for a negative index.
uint64_t engine_group_mix_input_latency(const EngineGroupMix*, int track);
