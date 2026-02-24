// effects_manager.c - minimal master-bus effects manager (interleaved, RT-safe)
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include "effects/effects_api.h"
#include "effects/effects_manager.h"
#include "effects/param_utils.h"

// -------------------------------
// Internal types
// -------------------------------

typedef struct FxInstance {
    FxHandle* handle;
    FxVTable  vt;
    FxDesc    desc;
    bool      enabled;
    FxInstId  id;
    FxTypeId  type;
    uint32_t  param_count;
    float     param_values[FX_MAX_PARAMS];
    float     param_current[FX_MAX_PARAMS];
    float     param_smoothing_ms[FX_MAX_PARAMS];
    FxParamMode param_mode[FX_MAX_PARAMS];
    float     param_beats[FX_MAX_PARAMS];
    const EffectParamSpec* param_specs;
    uint32_t  param_spec_count;
} FxInstance;

typedef struct FxChain {
    FxInstance* items;
    int count;
    int capacity;
} FxChain;

struct EffectsManager {
    int sample_rate;
    int max_block;
    int max_channels;

    // Interleaved scratch (size = max_block * max_channels)
    float* scratch;
    int    scratch_frames;   // == max_block
    int    scratch_channels; // == max_channels

    // Optional: borrow buffers later if we add planar paths
    struct EngineBufferPool* pool;

    // Master bus chain (v1)
    FxChain master;
    FxInstId next_inst_id;

    // Per-track chains
    FxChain* tracks;
    int track_count;
    int track_capacity;

    // Simple built-in registry table
    FxRegistryEntry* reg;
    int reg_count;
    int reg_cap;

    FxMeterTapCallback meter_cb;
    void* meter_cb_user;
    FxScopeTapCallback scope_cb;
    void* scope_cb_user;
};

// -------------------------------
// Small utilities
// -------------------------------

static void chain_free(FxChain* c) {
    if (!c) return;
    for (int i = 0; i < c->count; ++i) {
        FxInstance* inst = &c->items[i];
        if (inst->handle && inst->vt.destroy) {
            inst->vt.destroy(inst->handle);
        }
    }
    free(c->items);
    c->items = NULL;
    c->count = 0;
    c->capacity = 0;
}

static FxInstance* chain_insert(FxChain* c, int at_index) {
    if (at_index < 0 || at_index > c->count) at_index = c->count;
    if (c->count == c->capacity) {
        int new_cap = (c->capacity == 0) ? 4 : (c->capacity * 2);
        FxInstance* n = (FxInstance*)realloc(c->items, (size_t)new_cap * sizeof(FxInstance));
        if (!n) return NULL;
        c->items = n;
        c->capacity = new_cap;
    }
    if (c->count > at_index) {
        memmove(&c->items[at_index + 1],
                &c->items[at_index],
                (size_t)(c->count - at_index) * sizeof(FxInstance));
    }
    c->count++;
    return &c->items[at_index];
}

static bool chain_remove(FxChain* c, int idx) {
    if (idx < 0 || idx >= c->count) return false;
    FxInstance* inst = &c->items[idx];
    if (inst->handle && inst->vt.destroy) {
        inst->vt.destroy(inst->handle);
    }
    if (idx < c->count - 1) {
        memmove(&c->items[idx],
                &c->items[idx + 1],
                (size_t)(c->count - idx - 1) * sizeof(FxInstance));
    }
    c->count--;
    return true;
}

// Returns true if the effect type should emit a gain-delta scope stream.
static bool fxm_scope_is_gr_type(FxTypeId type) {
    return type == 7u || type == 20u || type == 21u || type == 22u || type == 23u
        || type == 24u || type == 25u || type == 26u || type == 27u;
}

// Computes RMS for an interleaved buffer to estimate gain reduction.
static float fxm_compute_rms(const float* buffer, int frames, int channels) {
    if (!buffer || frames <= 0 || channels <= 0) {
        return 0.0f;
    }
    double sum = 0.0;
    int count = frames * channels;
    for (int i = 0; i < count; ++i) {
        float v = buffer[i];
        sum += (double)v * (double)v;
    }
    return count > 0 ? (float)sqrt(sum / (double)count) : 0.0f;
}

static bool ensure_track_capacity(EffectsManager* fm, int track_count) {
    if (!fm || track_count < 0) return false;
    if (track_count <= fm->track_capacity) {
        fm->track_count = track_count;
        return true;
    }
    int new_cap = fm->track_capacity == 0 ? 4 : fm->track_capacity;
    while (new_cap < track_count) new_cap *= 2;
    FxChain* n = (FxChain*)realloc(fm->tracks, (size_t)new_cap * sizeof(FxChain));
    if (!n) return false;
    // initialize new slots
    for (int i = fm->track_capacity; i < new_cap; ++i) {
        n[i].items = NULL;
        n[i].count = 0;
        n[i].capacity = 0;
    }
    fm->tracks = n;
    fm->track_capacity = new_cap;
    fm->track_count = track_count;
    return true;
}

static bool chain_reorder(FxChain* c, int from, int to) {
    if (from < 0 || from >= c->count) return false;
    if (to   < 0 || to   >= c->count) return false;
    if (from == to) return true;

    FxInstance tmp = c->items[from];
    if (from < to) {
        for (int i = from; i < to; ++i) {
            c->items[i] = c->items[i + 1];
        }
    } else {
        for (int i = from; i > to; --i) {
            c->items[i] = c->items[i - 1];
        }
    }
    c->items[to] = tmp;
    return true;
}

// Returns true if the param should apply changes immediately (no smoothing).
static bool fx_param_is_discrete(const EffectParamSpec* spec) {
    if (!spec) {
        return false;
    }
    switch (spec->type) {
        case FX_PARAM_TYPE_BOOL:
        case FX_PARAM_TYPE_INT:
        case FX_PARAM_TYPE_ENUM:
            return true;
        default:
            return false;
    }
}

// Computes a block-level smoothing coefficient for the requested time constant.
static float fx_param_smoothing_coeff(float smoothing_ms, int sample_rate, int frames) {
    if (smoothing_ms <= 0.0f || sample_rate <= 0 || frames <= 0) {
        return 0.0f;
    }
    float time_samples = smoothing_ms * 0.001f * (float)sample_rate;
    if (time_samples <= 1.0f) {
        return 0.0f;
    }
    return expf(-(float)frames / time_samples);
}

// -------------------------------
// Registry
// -------------------------------

static const FxRegistryEntry* reg_find_by_id(const struct EffectsManager* fm, FxTypeId id) {
    for (int i = 0; i < fm->reg_count; ++i) {
        if (fm->reg[i].id == id) return &fm->reg[i];
    }
    return NULL;
}

bool fxm_register_builtin(EffectsManager* fm, const FxRegistryEntry* entries, int count) {
    if (!fm || !entries || count <= 0) return false;
    int need = fm->reg_count + count;
    if (need > fm->reg_cap) {
        int new_cap = (fm->reg_cap == 0) ? 8 : fm->reg_cap;
        while (new_cap < need) new_cap *= 2;
        FxRegistryEntry* r = (FxRegistryEntry*)realloc(fm->reg, (size_t)new_cap * 
sizeof(FxRegistryEntry));
        if (!r) return false;
        fm->reg = r;
        fm->reg_cap = new_cap;
    }
    memcpy(&fm->reg[fm->reg_count], entries, (size_t)count * sizeof(FxRegistryEntry));
    fm->reg_count += count;
    return true;
}

const FxRegistryEntry* fxm_get_registry(const EffectsManager* fm, int* out_count) {
    if (!fm) return NULL;
    if (out_count) {
        *out_count = fm->reg_count;
    }
    return fm->reg;
}

const FxRegistryEntry* fxm_find_registry(const EffectsManager* fm, FxTypeId type) {
    if (!fm) return NULL;
    return reg_find_by_id(fm, type);
}

bool fxm_registry_get_desc(const EffectsManager* fm, FxTypeId type, FxDesc* out_desc) {
    if (!fm || !out_desc) return false;
    const FxRegistryEntry* ent = reg_find_by_id(fm, type);
    if (!ent || !ent->get_desc) return false;
    FxDesc desc = {0};
    if (!ent->get_desc(&desc)) return false;
    *out_desc = desc;
    return true;
}

// -------------------------------
// Manager lifecycle
// -------------------------------

EffectsManager* fxm_create(const FxConfig* cfg) {
    if (!cfg) return NULL;
    EffectsManager* fm = (EffectsManager*)calloc(1, sizeof(EffectsManager));
    if (!fm) return NULL;

    fm->sample_rate   = cfg->sample_rate;
    fm->max_block     = cfg->max_block;
    fm->max_channels  = cfg->max_channels;
    fm->pool          = cfg->pool;

    // Interleaved scratch
    size_t samples = (size_t)fm->max_block * (size_t)fm->max_channels;
    fm->scratch = (float*)malloc(samples * sizeof(float));
    if (!fm->scratch) {
        free(fm);
        return NULL;
    }
    fm->scratch_frames   = fm->max_block;
    fm->scratch_channels = fm->max_channels;

    // chains & registry start empty
    fm->master.items = NULL;
    fm->master.count = 0;
    fm->master.capacity = 0;
    fm->tracks = NULL;
    fm->track_count = 0;
    fm->track_capacity = 0;
    fm->next_inst_id = 1;
    fm->reg = NULL;
    fm->reg_count = fm->reg_cap = 0;
    fm->meter_cb = NULL;
    fm->meter_cb_user = NULL;
    fm->scope_cb = NULL;
    fm->scope_cb_user = NULL;

    return fm;
}

void fxm_set_meter_tap_callback(EffectsManager* fm, FxMeterTapCallback cb, void* user) {
    if (!fm) {
        return;
    }
    fm->meter_cb = cb;
    fm->meter_cb_user = user;
}

void fxm_set_scope_tap_callback(EffectsManager* fm, FxScopeTapCallback cb, void* user) {
    if (!fm) {
        return;
    }
    fm->scope_cb = cb;
    fm->scope_cb_user = user;
}

void fxm_destroy(EffectsManager* fm) {
    if (!fm) return;
    chain_free(&fm->master);
    if (fm->tracks) {
        for (int i = 0; i < fm->track_count; ++i) {
            chain_free(&fm->tracks[i]);
        }
        free(fm->tracks);
    }
    free(fm->scratch);
    free(fm->reg);
    free(fm);
}

// -------------------------------
// Instantiate from registry
// -------------------------------

static bool instantiate_fx(EffectsManager* fm, const FxRegistryEntry* ent, FxInstance* out_inst) 
{
    if (!fm || !ent || !out_inst) return false;

    // Get descriptor
    FxDesc desc = {0};
    if (!ent->get_desc) return false;
    if (!ent->get_desc(&desc)) return false;

    // Create instance
    FxHandle* handle = NULL;
    FxVTable vt = {0};

    fx_create_fn create_fn = ent->create;
    if (!create_fn) return false;
    if (!create_fn(&desc, &handle, &vt,
                   (uint32_t)fm->sample_rate,
                   (uint32_t)fm->max_block,
                   (uint32_t)fm->max_channels)) {
        return false;
    }

    // Populate output
    out_inst->handle  = handle;
    out_inst->vt      = vt;
    out_inst->desc    = desc;
    out_inst->enabled = true;
    out_inst->type    = ent->id;
    out_inst->param_count = desc.num_params > FX_MAX_PARAMS ? FX_MAX_PARAMS : desc.num_params;
    out_inst->param_specs = ent->param_specs;
    out_inst->param_spec_count = ent->param_spec_count;
    for (uint32_t i = 0; i < FX_MAX_PARAMS; ++i) {
        out_inst->param_values[i] = 0.0f;
        out_inst->param_current[i] = 0.0f;
        out_inst->param_smoothing_ms[i] = 0.0f;
        out_inst->param_mode[i] = FX_PARAM_MODE_NATIVE;
        out_inst->param_beats[i] = 0.0f;
    }

    // Initialize defaults
    for (uint32_t i = 0; i < desc.num_params; ++i) {
        if (out_inst->vt.set_param) {
            out_inst->vt.set_param(out_inst->handle, i, desc.param_defaults[i]);
        }
        if (i < FX_MAX_PARAMS) {
            out_inst->param_values[i] = desc.param_defaults[i];
            out_inst->param_current[i] = desc.param_defaults[i];
            if (out_inst->param_specs && i < out_inst->param_spec_count) {
                out_inst->param_smoothing_ms[i] = out_inst->param_specs[i].smoothing_ms;
            }
        }
    }
    if (out_inst->vt.reset) {
        out_inst->vt.reset(out_inst->handle);
    }
    return true;
}

// -------------------------------
// Master chain API
// -------------------------------

bool fxm_set_track_count(EffectsManager* fm, int track_count) {
    if (!fm || track_count < 0) return false;
    int prev = fm->track_count;
    if (!ensure_track_capacity(fm, track_count)) {
        fm->track_count = prev;
        return false;
    }
    if (fm->tracks && track_count < prev) {
        for (int i = track_count; i < prev; ++i) {
            chain_free(&fm->tracks[i]);
        }
    }
    fm->track_count = track_count;
    return true;
}

FxInstId fxm_master_add(EffectsManager* fm, FxTypeId type) {
    if (!fm) return (FxInstId)0;

    const FxRegistryEntry* ent = reg_find_by_id(fm, type);
    if (!ent) return (FxInstId)0;

    FxInstance inst = {0};
    if (!instantiate_fx(fm, ent, &inst)) return (FxInstId)0;

    int at = fm->master.count;
    FxInstance* slot = chain_insert(&fm->master, at);
    if (!slot) {
        if (inst.handle && inst.vt.destroy) inst.vt.destroy(inst.handle);
        return (FxInstId)0;
    }
    FxInstId new_id = fm->next_inst_id++;
    if (new_id == 0) {
        new_id = fm->next_inst_id++;
    }
    inst.id = new_id;
    *slot = inst;
    return new_id;
}

static FxInstance* master_get_by_id(EffectsManager* fm, FxInstId id, int* out_index) {
    if (!fm || id == 0) return NULL;
    for (int i = 0; i < fm->master.count; ++i) {
        FxInstance* inst = &fm->master.items[i];
        if (inst->id == id) {
            if (out_index) *out_index = i;
            return inst;
        }
    }
    return NULL;
}

// Applies a parameter change, optionally smoothing over time based on the spec.
static bool fxm_apply_param_change(FxInstance* inst,
                                   uint32_t pidx,
                                   float value,
                                   FxParamMode mode,
                                   float beat_value,
                                   const TempoState* tempo,
                                   bool force_immediate) {
    if (!inst || pidx >= inst->desc.num_params) {
        return false;
    }
    const EffectParamSpec* spec = NULL;
    if (inst->param_specs && pidx < inst->param_spec_count) {
        spec = &inst->param_specs[pidx];
    }
    float applied_beat_value = beat_value;
    float native_value = value;
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec) && tempo) {
        float beat_min = 0.0f;
        float beat_max = 0.0f;
        if (fx_param_spec_get_beat_bounds(spec, tempo, &beat_min, &beat_max)) {
            if (applied_beat_value < beat_min) applied_beat_value = beat_min;
            if (applied_beat_value > beat_max) applied_beat_value = beat_max;
        }
        native_value = fx_param_spec_beats_to_native(spec, applied_beat_value, tempo);
    }
    if (spec) {
        if (native_value < spec->min_value) native_value = spec->min_value;
        if (native_value > spec->max_value) native_value = spec->max_value;
        if (spec->type == FX_PARAM_TYPE_BOOL) {
            native_value = native_value >= 0.5f ? 1.0f : 0.0f;
        } else if (spec->type == FX_PARAM_TYPE_INT || spec->type == FX_PARAM_TYPE_ENUM) {
            native_value = floorf(native_value + 0.5f);
            if (native_value < spec->min_value) native_value = spec->min_value;
            if (native_value > spec->max_value) native_value = spec->max_value;
            if (spec->type == FX_PARAM_TYPE_ENUM && spec->enum_count > 0) {
                float enum_max = (float)(spec->enum_count - 1u);
                if (native_value > enum_max) native_value = enum_max;
            }
        }
    }
    if (pidx < FX_MAX_PARAMS) {
        inst->param_values[pidx] = native_value;
        inst->param_mode[pidx] = mode;
        inst->param_beats[pidx] = applied_beat_value;
    }
    if (!inst->vt.set_param) {
        return false;
    }
    bool smooth = false;
    if (!force_immediate && spec && !fx_param_is_discrete(spec)) {
        smooth = spec->smoothing_ms > 0.0f;
    }
    if (!smooth) {
        if (pidx < FX_MAX_PARAMS) {
            inst->param_current[pidx] = native_value;
        }
        inst->vt.set_param(inst->handle, pidx, native_value);
    }
    return true;
}

bool fxm_master_remove(EffectsManager* fm, FxInstId id) {
    int idx = -1;
    if (!master_get_by_id(fm, id, &idx)) return false;
    return chain_remove(&fm->master, idx);
}

bool fxm_master_reorder(EffectsManager* fm, FxInstId id, int new_index) {
    int idx = -1;
    if (!master_get_by_id(fm, id, &idx)) return false;
    if (new_index < 0) new_index = 0;
    if (new_index >= fm->master.count) new_index = fm->master.count - 1;
    return chain_reorder(&fm->master, idx, new_index);
}

bool fxm_master_set_param(EffectsManager* fm, FxInstId id, uint32_t pidx, float value) {
    FxInstance* inst = master_get_by_id(fm, id, NULL);
    return fxm_apply_param_change(inst, pidx, value, FX_PARAM_MODE_NATIVE, 0.0f, NULL, true);
}

bool fxm_master_set_param_target(EffectsManager* fm,
                                 FxInstId id,
                                 uint32_t pidx,
                                 float value,
                                 FxParamMode mode,
                                 float beat_value,
                                 const TempoState* tempo) {
    FxInstance* inst = master_get_by_id(fm, id, NULL);
    return fxm_apply_param_change(inst, pidx, value, mode, beat_value, tempo, false);
}

bool fxm_master_set_param_with_mode(EffectsManager* fm,
                                    FxInstId id,
                                    uint32_t pidx,
                                    float value,
                                    FxParamMode mode,
                                    float beat_value) {
    FxInstance* inst = master_get_by_id(fm, id, NULL);
    return fxm_apply_param_change(inst, pidx, value, mode, beat_value, NULL, true);
}

bool fxm_master_set_enabled(EffectsManager* fm, FxInstId id, bool enabled) {
    FxInstance* inst = master_get_by_id(fm, id, NULL);
    if (!inst) return false;
    inst->enabled = enabled;
    return true;
}

const EffectParamSpec* fxm_registry_get_param_specs(const EffectsManager* fm, FxTypeId type, uint32_t* out_count) {
    if (!fm) {
        return NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    const FxRegistryEntry* entry = reg_find_by_id(fm, type);
    if (!entry || !entry->param_specs || entry->param_spec_count == 0) {
        return NULL;
    }
    if (out_count) {
        *out_count = entry->param_spec_count;
    }
    return entry->param_specs;
}

const EffectParamSpec* fxm_registry_get_param_spec(const EffectsManager* fm, FxTypeId type, uint32_t param_index) {
    uint32_t count = 0;
    const EffectParamSpec* specs = fxm_registry_get_param_specs(fm, type, &count);
    if (!specs || param_index >= count) {
        return NULL;
    }
    return &specs[param_index];
}

bool fxm_master_snapshot(const EffectsManager* fm, FxMasterSnapshot* out) {
    if (!fm || !out) return false;
    FxMasterSnapshot snap = {0};
    int limit = fm->master.count;
    if (limit > FX_MASTER_MAX) {
        limit = FX_MASTER_MAX;
    }
    for (int i = 0; i < limit; ++i) {
        const FxInstance* inst = &fm->master.items[i];
        FxMasterInstanceInfo info = {0};
        info.id = inst->id;
        info.type = inst->type;
        info.enabled = inst->enabled;
        uint32_t pc = inst->desc.num_params;
        if (pc > FX_MAX_PARAMS) {
            pc = FX_MAX_PARAMS;
        }
        info.param_count = pc;
        for (uint32_t p = 0; p < pc; ++p) {
            info.params[p] = inst->param_values[p];
            info.param_mode[p] = inst->param_mode[p];
            info.param_beats[p] = inst->param_beats[p];
        }
        snap.items[snap.count++] = info;
    }
    *out = snap;
    return true;
}

static FxInstance* track_get_by_id(EffectsManager* fm, int track_index, FxInstId id, int* out_index) {
    if (!fm || !fm->tracks || id == 0) return NULL;
    if (track_index < 0 || track_index >= fm->track_count) return NULL;
    FxChain* c = &fm->tracks[track_index];
    for (int i = 0; i < c->count; ++i) {
        FxInstance* inst = &c->items[i];
        if (inst->id == id) {
            if (out_index) *out_index = i;
            return inst;
        }
    }
    return NULL;
}

FxInstId fxm_track_add(EffectsManager* fm, int track_index, FxTypeId type) {
    if (!fm || track_index < 0) return (FxInstId)0;
    if (!ensure_track_capacity(fm, track_index + 1)) return (FxInstId)0;
    FxChain* chain = &fm->tracks[track_index];

    const FxRegistryEntry* ent = reg_find_by_id(fm, type);
    if (!ent) return (FxInstId)0;

    FxInstance inst = {0};
    if (!instantiate_fx(fm, ent, &inst)) return (FxInstId)0;

    FxInstance* slot = chain_insert(chain, chain->count);
    if (!slot) {
        if (inst.handle && inst.vt.destroy) inst.vt.destroy(inst.handle);
        return (FxInstId)0;
    }
    FxInstId new_id = fm->next_inst_id++;
    if (new_id == 0) new_id = fm->next_inst_id++;
    inst.id = new_id;
    *slot = inst;
    return new_id;
}

bool fxm_track_remove(EffectsManager* fm, int track_index, FxInstId id) {
    int idx = -1;
    FxInstance* inst = track_get_by_id(fm, track_index, id, &idx);
    if (!inst) return false;
    return chain_remove(&fm->tracks[track_index], idx);
}

bool fxm_track_reorder(EffectsManager* fm, int track_index, FxInstId id, int new_index) {
    if (!fm || track_index < 0 || track_index >= fm->track_count) return false;
    int idx = -1;
    FxInstance* inst = track_get_by_id(fm, track_index, id, &idx);
    if (!inst) return false;
    FxChain* chain = &fm->tracks[track_index];
    if (new_index < 0) new_index = 0;
    if (new_index >= chain->count) new_index = chain->count - 1;
    return chain_reorder(chain, idx, new_index);
}

bool fxm_track_set_param(EffectsManager* fm, int track_index, FxInstId id, uint32_t pidx, float value) {
    FxInstance* inst = track_get_by_id(fm, track_index, id, NULL);
    return fxm_apply_param_change(inst, pidx, value, FX_PARAM_MODE_NATIVE, 0.0f, NULL, true);
}

bool fxm_track_set_param_target(EffectsManager* fm,
                                int track_index,
                                FxInstId id,
                                uint32_t pidx,
                                float value,
                                FxParamMode mode,
                                float beat_value,
                                const TempoState* tempo) {
    FxInstance* inst = track_get_by_id(fm, track_index, id, NULL);
    return fxm_apply_param_change(inst, pidx, value, mode, beat_value, tempo, false);
}

bool fxm_track_set_param_with_mode(EffectsManager* fm,
                                   int track_index,
                                   FxInstId id,
                                   uint32_t pidx,
                                   float value,
                                   FxParamMode mode,
                                   float beat_value) {
    FxInstance* inst = track_get_by_id(fm, track_index, id, NULL);
    return fxm_apply_param_change(inst, pidx, value, mode, beat_value, NULL, true);
}

bool fxm_track_set_enabled(EffectsManager* fm, int track_index, FxInstId id, bool enabled) {
    FxInstance* inst = track_get_by_id(fm, track_index, id, NULL);
    if (!inst) return false;
    inst->enabled = enabled;
    return true;
}

bool fxm_track_snapshot(const EffectsManager* fm, int track_index, FxMasterSnapshot* out) {
    if (!fm || !out || !fm->tracks) return false;
    if (track_index < 0 || track_index >= fm->track_count) return false;
    FxChain* chain = &fm->tracks[track_index];
    FxMasterSnapshot snap = {0};
    int limit = chain->count;
    if (limit > FX_MASTER_MAX) limit = FX_MASTER_MAX;
    for (int i = 0; i < limit; ++i) {
        const FxInstance* inst = &chain->items[i];
        FxMasterInstanceInfo info = {0};
        info.id = inst->id;
        info.type = inst->type;
        info.enabled = inst->enabled;
        uint32_t pc = inst->desc.num_params;
        if (pc > FX_MAX_PARAMS) pc = FX_MAX_PARAMS;
        info.param_count = pc;
        for (uint32_t p = 0; p < pc; ++p) {
            info.params[p] = inst->param_values[p];
            info.param_mode[p] = inst->param_mode[p];
            info.param_beats[p] = inst->param_beats[p];
        }
        snap.items[snap.count++] = info;
    }
    *out = snap;
    return true;
}

// Advances smoothed parameters for an instance and pushes changes into the DSP.
static void fxm_apply_param_smoothing(EffectsManager* fm, FxInstance* inst, int frames) {
    if (!fm || !inst || !inst->vt.set_param) {
        return;
    }
    uint32_t pc = inst->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : inst->param_count;
    for (uint32_t p = 0; p < pc; ++p) {
        float target = inst->param_values[p];
        float current = inst->param_current[p];
        if (fabsf(target - current) < 1e-6f) {
            continue;
        }
        const EffectParamSpec* spec = NULL;
        if (inst->param_specs && p < inst->param_spec_count) {
            spec = &inst->param_specs[p];
        }
        bool smooth = spec && !fx_param_is_discrete(spec) && spec->smoothing_ms > 0.0f;
        if (!smooth) {
            inst->param_current[p] = target;
            inst->vt.set_param(inst->handle, p, target);
            continue;
        }
        float coeff = fx_param_smoothing_coeff(spec->smoothing_ms, fm->sample_rate, frames);
        if (coeff <= 0.0f) {
            inst->param_current[p] = target;
            inst->vt.set_param(inst->handle, p, target);
            continue;
        }
        float next = target + (current - target) * coeff;
        if (fabsf(next - target) < 1e-5f) {
            next = target;
        }
        if (fabsf(next - current) >= 1e-6f) {
            inst->param_current[p] = next;
            inst->vt.set_param(inst->handle, p, next);
        }
    }
}

// -------------------------------
// Real-time render (master bus)
// -------------------------------

void fxm_render_master(EffectsManager* fm, float* interleaved_io, int frames, int channels) {
    if (!fm || !interleaved_io || frames <= 0 || channels <= 0) return;

    // Safety: if incoming block exceeds configured scratch, clamp (or early out).
    if (frames > fm->scratch_frames || channels > fm->scratch_channels) {
        // In a debug build you may assert; here we just clamp work to safe region.
        int safe_frames = (frames > fm->scratch_frames) ? fm->scratch_frames : frames;
        int safe_channels = (channels > fm->scratch_channels) ? fm->scratch_channels : channels;
        frames = safe_frames;
        channels = safe_channels;
    }

    float* io = interleaved_io;
    float* scratch = fm->scratch;
    const int n = frames * channels;

    for (int i = 0; i < fm->master.count; ++i) {
        FxInstance* inst = &fm->master.items[i];
        if (!inst->enabled) continue;
        const bool emit_gr = fm->scope_cb && fxm_scope_is_gr_type(inst->type);
        float in_rms = 0.0f;
        if (emit_gr) {
            in_rms = fxm_compute_rms(io, frames, channels);
        }
        if (inst->type >= 100u && inst->type <= 109u) {
            if (fm->meter_cb) {
                fm->meter_cb(fm->meter_cb_user, true, -1, inst->id, inst->type, io, frames, channels);
            }
            continue;
        }

        fxm_apply_param_smoothing(fm, inst, frames);
        const bool inplace_ok = (inst->desc.flags & FX_FLAG_INPLACE_OK) != 0;
        if (inplace_ok) {
            // process in-place: out == in
            inst->vt.process(inst->handle, io, io, frames, channels);
            if (emit_gr) {
                float out_rms = fxm_compute_rms(io, frames, channels);
                float gr_db = 20.0f * log10f((out_rms + 1e-12f) / (in_rms + 1e-12f));
                fm->scope_cb(fm->scope_cb_user, true, -1, inst->id, inst->type, gr_db);
            }
        } else {
            // out-of-place: process to scratch, then copy back to io
            inst->vt.process(inst->handle, io, scratch, frames, channels);
            if (emit_gr) {
                float out_rms = fxm_compute_rms(scratch, frames, channels);
                float gr_db = 20.0f * log10f((out_rms + 1e-12f) / (in_rms + 1e-12f));
                fm->scope_cb(fm->scope_cb_user, true, -1, inst->id, inst->type, gr_db);
            }
            memcpy(io, scratch, (size_t)n * sizeof(float));
        }
    }
}

// -------------------------------
// Track render
// -------------------------------

void fxm_render_track(EffectsManager* fm, int track_index, float* interleaved_io, int frames, int channels) {
    if (!fm || !fm->tracks || track_index < 0 || track_index >= fm->track_count) return;
    FxChain* chain = &fm->tracks[track_index];
    if (chain->count == 0) return;
    int n = frames * channels;
    if (n <= 0) return;
    float* scratch = fm->scratch;
    if (!scratch) return;
    for (int i = 0; i < chain->count; ++i) {
        FxInstance* inst = &chain->items[i];
        if (!inst->enabled) continue;
        FxHandle* h = inst->handle;
        if (!h || !inst->vt.process) continue;
        const bool emit_gr = fm->scope_cb && fxm_scope_is_gr_type(inst->type);
        float in_rms = 0.0f;
        if (emit_gr) {
            in_rms = fxm_compute_rms(interleaved_io, frames, channels);
        }
        if (inst->type >= 100u && inst->type <= 109u) {
            if (fm->meter_cb) {
                fm->meter_cb(fm->meter_cb_user, false, track_index, inst->id, inst->type, interleaved_io, frames, channels);
            }
            continue;
        }

        fxm_apply_param_smoothing(fm, inst, frames);
        if (inst->desc.flags & FX_FLAG_INPLACE_OK) {
            inst->vt.process(h, interleaved_io, interleaved_io, frames, channels);
            if (emit_gr) {
                float out_rms = fxm_compute_rms(interleaved_io, frames, channels);
                float gr_db = 20.0f * log10f((out_rms + 1e-12f) / (in_rms + 1e-12f));
                fm->scope_cb(fm->scope_cb_user, false, track_index, inst->id, inst->type, gr_db);
            }
        } else {
            if (n > fm->scratch_frames * fm->scratch_channels) {
                continue;
            }
            inst->vt.process(h, interleaved_io, scratch, frames, channels);
            if (emit_gr) {
                float out_rms = fxm_compute_rms(scratch, frames, channels);
                float gr_db = 20.0f * log10f((out_rms + 1e-12f) / (in_rms + 1e-12f));
                fm->scope_cb(fm->scope_cb_user, false, track_index, inst->id, inst->type, gr_db);
            }
            memcpy(interleaved_io, scratch, (size_t)n * sizeof(float));
        }
    }
}
