#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward decls to avoid engine header tangles.
struct EngineBufferPool; // from engine/buffer_pool.h
struct EffectsManager;

typedef struct EffectsManager EffectsManager;
typedef uint32_t FxTypeId;   // registry id (static table for now)
typedef uint32_t FxInstId;   // per-track or master instance id

typedef struct {
    int sample_rate;
    int max_block;
    int max_channels;
    struct EngineBufferPool* pool;  // optional scratch for out-of-place fx
} FxConfig;

// Creation/destruction (non-RT)
EffectsManager* fxm_create(const FxConfig* cfg);
void            fxm_destroy(EffectsManager* fm);

// ---------- Master chain (v1 minimal integration) ----------

// Add/remove/reorder effects on the MASTER bus.
FxInstId fxm_master_add(EffectsManager* fm, FxTypeId type);
bool     fxm_master_remove(EffectsManager* fm, FxInstId id);
bool     fxm_master_reorder(EffectsManager* fm, FxInstId id, int new_index);

// Params / enable-bypass (non-RT)
bool     fxm_master_set_param(EffectsManager* fm, FxInstId id, uint32_t pidx, float value);
bool     fxm_master_set_enabled(EffectsManager* fm, FxInstId id, bool enabled);

// REAL-TIME render on the MASTER bus.
// Interleaved in-place; manager handles scratch if an effect is not in-place capable.
void     fxm_render_master(EffectsManager* fm,
                           float* interleaved_io,
                           int frames,
                           int channels);

// ---------- (Optional next step) Per-track chains ----------
// NOTE: Your current EngineGraph mixes sources directly to one buffer.
// We'll first ship master-only (above). When you introduce per-track
// mix buffers, we can flip on these APIs with minimal changes.

FxInstId fxm_track_add(EffectsManager* fm, int track_index, FxTypeId type);
bool     fxm_track_remove(EffectsManager* fm, int track_index, FxInstId id);
bool     fxm_track_reorder(EffectsManager* fm, int track_index, FxInstId id, int new_index);
bool     fxm_track_set_param(EffectsManager* fm, int track_index, FxInstId id, uint32_t pidx, 
float value);
bool     fxm_track_set_enabled(EffectsManager* fm, int track_index, FxInstId id, bool enabled);

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
    int          (*get_desc)(void* out_desc);  // cast to fx_get_desc_fn
    int          (*create)(const void* desc, void** out_handle, void* out_vt,
                           uint32_t sr, uint32_t max_block, uint32_t max_ch);
} FxRegistryEntry;

bool fxm_register_builtin(EffectsManager* fm, const FxRegistryEntry* entries, int count);

#ifdef __cplusplus
} // extern "C"
#endif

