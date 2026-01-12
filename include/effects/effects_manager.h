#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "effects/effects_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward decls to avoid engine header tangles.
struct EngineBufferPool; // from engine/buffer_pool.h
struct EffectsManager;

typedef struct EffectsManager EffectsManager;
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

typedef struct {
    int sample_rate;
    int max_block;
    int max_channels;
    struct EngineBufferPool* pool;  // optional scratch for out-of-place fx
} FxConfig;

// Creation/destruction (non-RT)
EffectsManager* fxm_create(const FxConfig* cfg);
void            fxm_destroy(EffectsManager* fm);
bool            fxm_set_track_count(EffectsManager* fm, int track_count);
// Registers a callback for per-FX metering taps during render.
void            fxm_set_meter_tap_callback(EffectsManager* fm, FxMeterTapCallback cb, void* user);

// ---------- Master chain (v1 minimal integration) ----------

// Add/remove/reorder effects on the MASTER bus.
FxInstId fxm_master_add(EffectsManager* fm, FxTypeId type);
bool     fxm_master_remove(EffectsManager* fm, FxInstId id);
bool     fxm_master_reorder(EffectsManager* fm, FxInstId id, int new_index);

// Params / enable-bypass (non-RT)
bool     fxm_master_set_param(EffectsManager* fm, FxInstId id, uint32_t pidx, float value);
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
} FxRegistryEntry;

bool fxm_register_builtin(EffectsManager* fm, const FxRegistryEntry* entries, int count);
const FxRegistryEntry* fxm_get_registry(const EffectsManager* fm, int* out_count);
const FxRegistryEntry* fxm_find_registry(const EffectsManager* fm, FxTypeId type);
bool fxm_registry_get_desc(const EffectsManager* fm, FxTypeId type, FxDesc* out_desc);
bool fxm_master_snapshot(const EffectsManager* fm, FxMasterSnapshot* out);

#ifdef __cplusplus
} // extern "C"
#endif
