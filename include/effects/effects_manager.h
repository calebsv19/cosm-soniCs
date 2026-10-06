#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "effects/effects_api.h"
#include "effects/param_spec.h"
#include "time/tempo.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward decls to avoid engine header tangles.
struct EngineBufferPool; // from engine/buffer_pool.h
struct EffectsManager;

typedef struct EffectsManager EffectsManager;
// Clears exclusively owned render histories and snaps smoothing to accepted targets.
void fxm_reset_render_state(EffectsManager* fm);
typedef uint32_t FxTypeId;   // registry id (static table for now)
typedef uint32_t FxInstId;   // per-track or master instance id

#define FX_MASTER_MAX 16

typedef enum {
    FX_PARAM_MODE_NATIVE = 0,   // seconds/ms for time, Hz for rate
    FX_PARAM_MODE_BEATS,        // time expressed in beats
    FX_PARAM_MODE_BEAT_RATE     // rate expressed as a beat-period
} FxParamMode;

typedef struct {
    FxInstId  id;
    FxTypeId  type;
    bool      enabled;
    uint32_t  param_count;
    float     params[FX_MAX_PARAMS];
    FxParamMode param_mode[FX_MAX_PARAMS];
    float     param_beats[FX_MAX_PARAMS];
} FxMasterInstanceInfo;

typedef struct {
    int                  count;
    FxMasterInstanceInfo items[FX_MASTER_MAX];
} FxMasterSnapshot;

// Receives metering taps for FX instances at render time.
typedef void (*FxMeterTapCallback)(void* user,
                                   bool is_master,
                                   int track_index,
                                   FxInstId id,
                                   FxTypeId type,
                                   const float* interleaved,
                                   int frames,
                                   int channels);
// Receives scope values for FX instances at render time.
typedef void (*FxScopeTapCallback)(void* user,
                                   bool is_master,
                                   int track_index,
                                   FxInstId id,
                                   FxTypeId type,
                                   float value);

typedef struct {
    int sample_rate;
    int max_block;
    int max_channels;
    struct EngineBufferPool* pool;  // optional scratch for out-of-place fx
} FxConfig;

// Creation/destruction (non-RT)
EffectsManager* fxm_create(const FxConfig* cfg);
void            fxm_destroy(EffectsManager* fm);
// Prepares independent DSP handles from control-owned state, preserving instance identities.
EffectsManager* fxm_clone_for_render(const EffectsManager* control);
// Retains one track chain with its effect identities for undo history.
EffectsManager* fxm_clone_track_for_history(const EffectsManager* control, int track_index);
// Replaces a prepared chain only after an independent copy succeeds.
bool fxm_copy_track(EffectsManager* destination, int track_index, const EffectsManager* source, int source_index);
// Moves compatible DSP histories into a prepared revision, retaining its newly prepared targets.
// old_track_indices maps each prepared track to its previous index, or -1 for a new track.
// Both managers must be exclusively worker-owned at the adoption boundary.
bool fxm_transfer_render_state(EffectsManager* prepared, EffectsManager* previous,
                              const int* old_track_indices, int track_count);
bool            fxm_set_track_count(EffectsManager* fm, int track_count);
// Inserts an empty control-owned chain while preserving identities of shifted tracks' effects.
bool            fxm_insert_track(EffectsManager* fm, int track_index);
// Removes one control-owned chain while preserving the following chains and their instance IDs.
bool            fxm_remove_track(EffectsManager* fm, int track_index);
// Registers a callback for per-FX metering taps during render.
void            fxm_set_meter_tap_callback(EffectsManager* fm, FxMeterTapCallback cb, void* user);
// Registers a callback for per-FX scope taps during render.
void            fxm_set_scope_tap_callback(EffectsManager* fm, FxScopeTapCallback cb, void* user);

// Prepares maximum track-delay storage off the render thread; clone_for_render calls this automatically.
bool fxm_prepare_delay_compensation(EffectsManager* fm);
// Applies one block of controls and establishes parallel-track alignment before any track renders.
void fxm_begin_render_block(EffectsManager* fm, int frames);
// Applies the prepared compensation delay after track processing and before summing.
void fxm_align_track(EffectsManager* fm, int track, float* io, int frames, int channels);
// Returns latest render-block track-plus-master latency; read only from the owning render thread.
uint64_t fxm_processing_latency(const EffectsManager* fm);

// ---------- Master chain (v1 minimal integration) ----------

// Add/remove/reorder effects on the MASTER bus.
FxInstId fxm_master_add(EffectsManager* fm, FxTypeId type);
bool     fxm_master_remove(EffectsManager* fm, FxInstId id);
bool     fxm_master_reorder(EffectsManager* fm, FxInstId id, int new_index);

// Params / enable-bypass (non-RT)
bool     fxm_master_set_param(EffectsManager* fm, FxInstId id, uint32_t pidx, float value);
// Sets a master param target with optional beat sync using the provided tempo.
bool     fxm_master_set_param_target(EffectsManager* fm,
                                     FxInstId id,
                                     uint32_t pidx,
                                     float value,
                                     FxParamMode mode,
                                     float beat_value,
                                     const TempoState* tempo);
bool     fxm_master_set_param_with_mode(EffectsManager* fm,
                                        FxInstId id,
                                        uint32_t pidx,
                                        float value,
                                        FxParamMode mode,
                                        float beat_value);
bool     fxm_master_set_enabled(EffectsManager* fm, FxInstId id, bool enabled);

// REAL-TIME render on the MASTER bus.
// Interleaved in-place; manager handles scratch if an effect is not in-place capable.
void     fxm_render_master(EffectsManager* fm,
                           float* interleaved_io,
                           int frames,
                           int channels);

// ---------- Per-track chains ----------

FxInstId fxm_track_add(EffectsManager* fm, int track_index, FxTypeId type);
bool     fxm_track_remove(EffectsManager* fm, int track_index, FxInstId id);
bool     fxm_track_reorder(EffectsManager* fm, int track_index, FxInstId id, int new_index);
bool     fxm_track_set_param(EffectsManager* fm, int track_index, FxInstId id, uint32_t pidx, float value);
// Sets a track param target with optional beat sync using the provided tempo.
bool     fxm_track_set_param_target(EffectsManager* fm,
                                    int track_index,
                                    FxInstId id,
                                    uint32_t pidx,
                                    float value,
                                    FxParamMode mode,
                                    float beat_value,
                                    const TempoState* tempo);
bool     fxm_track_set_param_with_mode(EffectsManager* fm,
                                       int track_index,
                                       FxInstId id,
                                       uint32_t pidx,
                                       float value,
                                       FxParamMode mode,
                                       float beat_value);
bool     fxm_track_set_enabled(EffectsManager* fm, int track_index, FxInstId id, bool enabled);
bool     fxm_track_snapshot(const EffectsManager* fm, int track_index, FxMasterSnapshot* out);

// RT render for a single track's interleaved buffer (when available).
void     fxm_render_track(EffectsManager* fm,
                          int track_index,
                          float* interleaved_io,
                          int frames,
                          int channels);

// ---------- Registry (static, C-only) ----------
// Map a small enum to factories compiled into the binary.
// Example: { FX_GAIN, gain_get_desc, gain_create }.
typedef struct {
    FxTypeId       id;
    const char*    name;
    fx_get_desc_fn get_desc;
    fx_create_fn   create;
    const EffectParamSpec* param_specs;
    uint32_t       param_spec_count;
} FxRegistryEntry;

bool fxm_register_builtin(EffectsManager* fm, const FxRegistryEntry* entries, int count);
const FxRegistryEntry* fxm_get_registry(const EffectsManager* fm, int* out_count);
const FxRegistryEntry* fxm_find_registry(const EffectsManager* fm, FxTypeId type);
bool fxm_registry_get_desc(const EffectsManager* fm, FxTypeId type, FxDesc* out_desc);
// Returns the parameter spec list for a given effect type.
const EffectParamSpec* fxm_registry_get_param_specs(const EffectsManager* fm, FxTypeId type, uint32_t* out_count);
// Returns a single parameter spec for a given effect type + param index.
const EffectParamSpec* fxm_registry_get_param_spec(const EffectsManager* fm, FxTypeId type, uint32_t param_index);
bool fxm_master_snapshot(const EffectsManager* fm, FxMasterSnapshot* out);

// Restores a complete instance into an unpublished candidate; discard the candidate on failure.
bool fxm_restore_instance(EffectsManager* candidate, int track_index, int position, const FxMasterInstanceInfo* instance);

#ifdef __cplusplus
} // extern "C"
#endif
