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
#include "effects/sample_ramp.h"

// -------------------------------
// Internal types
// -------------------------------

typedef struct FxInstance {
    FxHandle* handle;
    FxVTable  vt;
    FxDesc    desc;
    bool      enabled;
    bool      rendered;
    FxSampleRamp bypass;
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

// Owns one serial chain and its prepared parallel-track compensation history.
typedef struct FxChain {
    FxInstance* items;
    int count;
    int capacity;
    float* alignment;
    uint32_t alignment_capacity, alignment_write, latency;
} FxChain;

struct EffectsManager {
    int sample_rate;
    int max_block;
    int max_channels;

    // Interleaved scratch (size = max_block * max_channels)
    float* scratch;
    float* bypass_dry; // Prepared dry block used by zero-latency bypass transitions.
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
    uint32_t track_latency, master_latency;
    bool alignment_dirty, block_prepared;

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
    free(c->alignment);
    c->alignment = NULL;
    c->alignment_capacity = c->alignment_write = c->latency = 0;
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
        return true;
    }
    int new_cap = fm->track_capacity == 0 ? 4 : fm->track_capacity;
    while (new_cap < track_count) new_cap *= 2;
    FxChain* n = (FxChain*)realloc(fm->tracks, (size_t)new_cap * sizeof(FxChain));
    if (!n) return false;
    // initialize new slots
    for (int i = fm->track_capacity; i < new_cap; ++i) {
        memset(&n[i], 0, sizeof(n[i]));
    }
    fm->tracks = n;
    fm->track_capacity = new_cap;
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
    if (!cfg || cfg->sample_rate <= 0 || cfg->max_block <= 0 || cfg->max_channels <= 0 ||
        (size_t)cfg->max_block > SIZE_MAX / sizeof(float) / (size_t)cfg->max_channels) return NULL;
    EffectsManager* fm = (EffectsManager*)calloc(1, sizeof(EffectsManager));
    if (!fm) return NULL;

    fm->sample_rate   = cfg->sample_rate;
    fm->max_block     = cfg->max_block;
    fm->max_channels  = cfg->max_channels;
    fm->pool          = cfg->pool;

    // Interleaved scratch
    size_t samples = (size_t)fm->max_block * (size_t)fm->max_channels;
    fm->scratch = (float*)malloc(samples * sizeof(float));
    fm->bypass_dry = (float*)malloc(samples * sizeof(float));
    if (!fm->bypass_dry) { free(fm->scratch); free(fm); return NULL; }
    if (!fm->scratch) {
        free(fm->bypass_dry);
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
    free(fm->bypass_dry);
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

    if ((desc.flags & FX_FLAG_DYNAMIC_LATENCY) && (!vt.latency || !vt.max_latency)) {
        if (vt.destroy) vt.destroy(handle);
        return false;
    }

    // Populate output
    out_inst->handle  = handle;
    out_inst->vt      = vt;
    out_inst->desc    = desc;
    out_inst->enabled = true;
    out_inst->rendered = false;
    fx_sample_ramp_reset(&out_inst->bypass, 1);
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

// Builds independent handles while copying identities and parameter targets from a control chain.
static bool chain_clone_for_render(EffectsManager* destination, FxChain* output, const FxChain* input) {
    for (int i = 0; i < input->count; ++i) {
        const FxInstance* source = &input->items[i];
        const FxRegistryEntry* entry = reg_find_by_id(destination, source->type);
        FxInstance instance = {0};
        if (!entry || !instantiate_fx(destination, entry, &instance)) return false;
        FxInstance* slot = chain_insert(output, output->count);
        if (!slot) {
            if (instance.vt.destroy) instance.vt.destroy(instance.handle);
            return false;
        }
        *slot = instance;
        slot->id = source->id;
        slot->enabled = source->enabled;
        for (uint32_t p = 0; p < slot->param_count; ++p) {
            slot->param_values[p] = source->param_values[p];
            slot->param_current[p] = source->param_values[p];
            slot->param_mode[p] = source->param_mode[p];
            slot->param_beats[p] = source->param_beats[p];
            if (slot->vt.set_param) slot->vt.set_param(slot->handle, p, slot->param_values[p]);
        }
    }
    return true;
}

// Replaces a prepared chain atomically while retaining stable effect identities.
bool fxm_copy_track(EffectsManager* destination, int track_index, const EffectsManager* source, int source_index) {
    if (!destination || !source || track_index < 0 || track_index >= destination->track_count ||
        source_index < 0 || source_index >= source->track_count) return false;
    FxChain candidate = {0};
    if (!chain_clone_for_render(destination, &candidate, &source->tracks[source_index])) {
        chain_free(&candidate);
        return false;
    }
    chain_free(&destination->tracks[track_index]);
    destination->tracks[track_index] = candidate;
    if (destination->next_inst_id < source->next_inst_id) destination->next_inst_id = source->next_inst_id;
    destination->alignment_dirty = true;
    return true;
}

// Captures only the requested chain rather than retaining unrelated project effects.
EffectsManager* fxm_clone_track_for_history(const EffectsManager* control, int track_index) {
    if (!control || track_index < 0 || track_index >= control->track_count) return NULL;
    FxConfig config = {.sample_rate = control->sample_rate, .max_block = control->max_block,
                       .max_channels = control->max_channels, .pool = control->pool};
    EffectsManager* snapshot = fxm_create(&config);
    if (!snapshot) return NULL;
    if ((control->reg_count && !fxm_register_builtin(snapshot, control->reg, control->reg_count)) ||
        !fxm_set_track_count(snapshot, 1) || !fxm_copy_track(snapshot, 0, control, track_index)) {
        fxm_destroy(snapshot);
        return NULL;
    }
    return snapshot;
}

// Creates a complete independently destroyable render revision without reading live DSP history.
EffectsManager* fxm_clone_for_render(const EffectsManager* control) {
    if (!control) return NULL;
    FxConfig config = {.sample_rate = control->sample_rate, .max_block = control->max_block,
                       .max_channels = control->max_channels, .pool = control->pool};
    EffectsManager* prepared = fxm_create(&config);
    if (!prepared) return NULL;
    if ((control->reg_count && !fxm_register_builtin(prepared, control->reg, control->reg_count)) ||
        !fxm_set_track_count(prepared, control->track_count) ||
        !chain_clone_for_render(prepared, &prepared->master, &control->master)) goto fail;
    for (int t = 0; t < control->track_count; ++t) {
        if (!chain_clone_for_render(prepared, &prepared->tracks[t], &control->tracks[t])) goto fail;
    }
    if (!fxm_prepare_delay_compensation(prepared)) goto fail;
    prepared->next_inst_id = control->next_inst_id;
    return prepared;
fail:
    fxm_destroy(prepared);
    return NULL;
}

// Exchanges compatible handles so tails and smoothing history survive edits while new targets apply.
static void chain_transfer_render_state(FxChain* prepared, FxChain* previous) {
    for (int i = 0; i < prepared->count; ++i) {
        FxInstance* next = &prepared->items[i];
        for (int j = 0; j < previous->count; ++j) {
            FxInstance* old = &previous->items[j];
            if (next->id != old->id || next->type != old->type || next->param_count != old->param_count) continue;
            if (next->vt.process != old->vt.process || next->vt.destroy != old->vt.destroy ||
                next->vt.set_param != old->vt.set_param) continue;
            FxHandle* replacement = next->handle;
            next->handle = old->handle;
            old->handle = replacement;
            next->rendered = old->rendered;
            next->bypass = old->bypass;
            for (uint32_t p = 0; p < next->param_count; ++p) {
                float current = next->param_current[p];
                next->param_current[p] = old->param_current[p];
                old->param_current[p] = current;
            }
            break;
        }
    }
}

// Reports serial signal delay or its preparation bound, excluding bypassed effects when active.
static uint32_t chain_latency(const FxChain* chain, bool maximum) {
    uint64_t total = 0;
    for (int i = 0; i < chain->count; ++i) {
        const FxInstance* inst = &chain->items[i];
        if (!maximum && !inst->enabled) continue;
        if (maximum && inst->vt.max_latency) total += inst->vt.max_latency(inst->handle);
        else if (inst->vt.latency) total += inst->vt.latency(inst->handle);
        else if (inst->desc.flags & FX_FLAG_HAS_LATENCY) total += inst->desc.latency_samples;
    }
    return total >= UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

// Allocates compensation rings off the render thread for every possible current-chain delay.
bool fxm_prepare_delay_compensation(EffectsManager* fm) {
    if (!fm) return false;
    uint32_t maximum = 0;
    for (int t = 0; t < fm->track_count; ++t) {
        uint32_t bound = chain_latency(&fm->tracks[t], true);
        if (bound > maximum) maximum = bound;
    }
    if (maximum == UINT32_MAX || (size_t)maximum + 1 > SIZE_MAX / sizeof(float) / fm->max_channels) return false;
    uint32_t capacity = maximum ? maximum + 1 : 0;
    for (int t = 0; t < fm->track_count; ++t) {
        FxChain* chain = &fm->tracks[t];
        if (chain->alignment_capacity == capacity) continue;
        float* data = capacity ? calloc((size_t)capacity * fm->max_channels, sizeof(float)) : NULL;
        if (capacity && !data) return false;
        free(chain->alignment);
        chain->alignment = data;
        chain->alignment_capacity = capacity;
        chain->alignment_write = 0;
        fm->alignment_dirty = true;
    }
    return true;
}

// Detects routing edits that invalidate captured audio in a latency-bearing chain.
static bool chain_timing_topology_changed(const FxChain* next, const FxChain* old) {
    if (!chain_latency(next, true) && !chain_latency(old, true)) return false;
    if (next->count != old->count) return true;
    for (int i = 0; i < next->count; ++i)
        if (next->items[i].id != old->items[i].id || next->items[i].enabled != old->items[i].enabled) return true;
    return false;
}

// Transfers a compatible alignment ring with the same ownership swap used for DSP handles.
static void chain_transfer_alignment(FxChain* next, FxChain* old) {
    next->latency = old->latency;
    if (next->alignment_capacity != old->alignment_capacity) return;
    float* data = next->alignment;
    next->alignment = old->alignment;
    old->alignment = data;
    uint32_t position = next->alignment_write;
    next->alignment_write = old->alignment_write;
    old->alignment_write = position;
}

// Adopts histories only across format-compatible revisions with a valid one-to-one track mapping.
bool fxm_transfer_render_state(EffectsManager* prepared, EffectsManager* previous,
                              const int* old_track_indices, int track_count) {
    if (!prepared || !previous || prepared == previous || track_count != prepared->track_count ||
        (track_count && !old_track_indices) || prepared->sample_rate != previous->sample_rate ||
        prepared->max_block != previous->max_block || prepared->max_channels != previous->max_channels) return false;
    for (int t = 0; t < track_count; ++t) {
        int old = old_track_indices[t];
        if (old < -1 || old >= previous->track_count) return false;
        for (int earlier = 0; old >= 0 && earlier < t; ++earlier) {
            if (old_track_indices[earlier] == old) return false;
        }
    }
    prepared->alignment_dirty = previous->alignment_dirty ||
        chain_timing_topology_changed(&prepared->master, &previous->master);
    prepared->track_latency = previous->track_latency;
    prepared->master_latency = previous->master_latency;
    prepared->master.latency = previous->master.latency;
    chain_transfer_render_state(&prepared->master, &previous->master);
    for (int t = 0; t < track_count; ++t) {
        if (old_track_indices[t] >= 0) {
            FxChain* next = &prepared->tracks[t];
            FxChain* old = &previous->tracks[old_track_indices[t]];
            prepared->alignment_dirty |= chain_timing_topology_changed(next, old) ||
                next->alignment_capacity != old->alignment_capacity;
            chain_transfer_render_state(next, old);
            chain_transfer_alignment(next, old);
        }
        else if (prepared->tracks[t].alignment_capacity) prepared->alignment_dirty = true;
    }
    return true;
}

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

// Inserts a new chain without recreating the effects belonging to shifted tracks.
bool fxm_insert_track(EffectsManager* fm, int track_index) {
    if (!fm || track_index < 0 || track_index > fm->track_count) return false;
    int previous_count = fm->track_count;
    if (!ensure_track_capacity(fm, previous_count + 1)) return false;
    memmove(&fm->tracks[track_index + 1], &fm->tracks[track_index],
            (size_t)(previous_count - track_index) * sizeof(FxChain));
    memset(&fm->tracks[track_index], 0, sizeof(FxChain));
    fm->track_count = previous_count + 1;
    return true;
}

// Deletes only the removed track's effects and moves surviving chain ownership in place.
bool fxm_remove_track(EffectsManager* fm, int track_index) {
    if (!fm || track_index < 0 || track_index >= fm->track_count) return false;
    chain_free(&fm->tracks[track_index]);
    memmove(&fm->tracks[track_index], &fm->tracks[track_index + 1],
            (size_t)(fm->track_count - track_index - 1) * sizeof(FxChain));
    --fm->track_count;
    memset(&fm->tracks[fm->track_count], 0, sizeof(FxChain));
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
    if (!inst || pidx >= inst->desc.num_params || pidx >= FX_MAX_PARAMS || !inst->vt.set_param ||
        !isfinite(value) || !isfinite(beat_value) || mode < FX_PARAM_MODE_NATIVE || mode > FX_PARAM_MODE_BEAT_RATE ||
        (mode != FX_PARAM_MODE_NATIVE && beat_value <= 0.0f)) {
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
        smooth = spec->smoothing_ms > 0.0f && !(inst->desc.flags & FX_FLAG_SAMPLE_PARAM_SMOOTHING);
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
    if (fm->track_count <= track_index) fm->track_count = track_index + 1;
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
        bool smooth = spec && !fx_param_is_discrete(spec) && spec->smoothing_ms > 0.0f &&
            !(inst->desc.flags & FX_FLAG_SAMPLE_PARAM_SMOOTHING);
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
// Clears DSP and compensation histories while retaining the current smoothed controls.
static void chain_clear_timing(FxChain* chain, int channels) {
    for (int i = 0; i < chain->count; ++i) {
        FxInstance* inst = &chain->items[i];
        if (inst->vt.reset) inst->vt.reset(inst->handle);
        fx_sample_ramp_reset(&inst->bypass, inst->enabled ? 1 : 0);
        inst->rendered = false;
    }
    if (chain->alignment)
        memset(chain->alignment, 0, (size_t)chain->alignment_capacity * channels * sizeof(float));
    chain->alignment_write = 0;
}

// Applies controls once, then establishes a single track-alignment delay for the whole render block.
void fxm_begin_render_block(EffectsManager* fm, int frames) {
    if (!fm || frames <= 0) return;
    uint32_t maximum = 0;
    bool changed = fm->alignment_dirty;
    for (int t = -1; t < fm->track_count; ++t) {
        FxChain* chain = t < 0 ? &fm->master : &fm->tracks[t];
        for (int i = 0; i < chain->count; ++i)
            if (chain->items[i].enabled) fxm_apply_param_smoothing(fm, &chain->items[i], frames);
        uint32_t latency = chain_latency(chain, false);
        changed |= latency != chain->latency;
        chain->latency = latency;
        if (t >= 0 && latency > maximum) maximum = latency;
    }
    changed |= maximum != fm->track_latency;
    fm->track_latency = maximum;
    fm->master_latency = fm->master.latency;
    if (changed) {
        chain_clear_timing(&fm->master, fm->max_channels);
        for (int t = 0; t < fm->track_count; ++t) chain_clear_timing(&fm->tracks[t], fm->max_channels);
    }
    fm->alignment_dirty = false;
    fm->block_prepared = true;
}

// Delays a processed track to the longest active serial chain before the master sum.
void fxm_align_track(EffectsManager* fm, int track, float* io, int frames, int channels) {
    if (!fm || track < 0 || track >= fm->track_count || !io || frames <= 0 ||
        channels <= 0 || channels > fm->max_channels) return;
    FxChain* chain = &fm->tracks[track];
    uint32_t delay = fm->track_latency - chain->latency;
    if (!delay) return;
    if (!chain->alignment || delay >= chain->alignment_capacity) {
        // A caller skipped non-RT preparation; never silently emit a misaligned track.
        memset(io, 0, (size_t)frames * channels * sizeof(float));
        return;
    }
    for (int n = 0; n < frames; ++n) {
        uint32_t read = (chain->alignment_write + chain->alignment_capacity - delay) % chain->alignment_capacity;
        for (int ch = 0; ch < channels; ++ch) {
            chain->alignment[(size_t)chain->alignment_write * fm->max_channels + ch] = io[(size_t)n * channels + ch];
            io[(size_t)n * channels + ch] = chain->alignment[(size_t)read * fm->max_channels + ch];
        }
        chain->alignment_write = (chain->alignment_write + 1) % chain->alignment_capacity;
    }
}

// Returns the common project-rate output delay of the prepared render block.
uint64_t fxm_processing_latency(const EffectsManager* fm) {
    return fm ? (uint64_t)fm->track_latency + fm->master_latency : 0;
}

// Real-time render (master bus)
// -------------------------------

// Renders a serial chain and crossfades bypass only where dry and wet have equal signal latency.
static void fxm_render_chain(EffectsManager* fm, FxChain* chain, bool master, int track,
                             float* io, int frames, int channels) {
    if (!io || frames <= 0 || channels <= 0 || frames > fm->max_block || channels > fm->max_channels) return;
    size_t samples = (size_t)frames * channels;
    for (int i = 0; i < chain->count; ++i) {
        FxInstance* inst = &chain->items[i];
        bool has_latency = inst->vt.max_latency ? inst->vt.max_latency(inst->handle) > 0 :
            ((inst->desc.flags & FX_FLAG_HAS_LATENCY) && inst->desc.latency_samples > 0);
        float target = inst->enabled ? 1 : 0;
        if (!inst->rendered || has_latency) fx_sample_ramp_reset(&inst->bypass, target);
        else if (target != inst->bypass.target) {
            if (target == 1 && inst->bypass.current == 0 && inst->vt.reset) inst->vt.reset(inst->handle);
            fx_sample_ramp_target(&inst->bypass, target, (uint32_t)(fm->sample_rate / 200));
        }
        if (!inst->enabled && inst->bypass.current == 0 && !inst->bypass.remaining) {
            inst->rendered = true;
            if (fm->scope_cb && inst->vt.gain_reduction_db)
                fm->scope_cb(fm->scope_cb_user, master, track, inst->id, inst->type, 0);
            continue;
        }
        if (inst->type >= 100u && inst->type <= 109u) {
            if (inst->enabled && fm->meter_cb)
                fm->meter_cb(fm->meter_cb_user, master, track, inst->id, inst->type, io, frames, channels);
            continue;
        }
        if (!inst->handle || !inst->vt.process) continue;
        if (!fm->block_prepared) fxm_apply_param_smoothing(fm, inst, frames);
        bool emit = fm->scope_cb && fxm_scope_is_gr_type(inst->type);
        float before = emit && !inst->vt.gain_reduction_db ? fxm_compute_rms(io, frames, channels) : 0;
        bool blend = inst->bypass.remaining || inst->bypass.current != 1;
        if (blend) memcpy(fm->bypass_dry, io, samples * sizeof(float));
        if (inst->desc.flags & FX_FLAG_INPLACE_OK) inst->vt.process(inst->handle, io, io, frames, channels);
        else {
            inst->vt.process(inst->handle, io, fm->scratch, frames, channels);
            memcpy(io, fm->scratch, samples * sizeof(float));
        }
        inst->rendered = true;
        if (emit) {
            float value = inst->vt.gain_reduction_db ? inst->vt.gain_reduction_db(inst->handle) :
                20 * log10f((fxm_compute_rms(io, frames, channels) + 1e-12f) / (before + 1e-12f));
            fm->scope_cb(fm->scope_cb_user, master, track, inst->id, inst->type, value);
        }
        if (blend) for (int frame = 0; frame < frames; ++frame) {
            float wet = fx_sample_ramp_next(&inst->bypass);
            for (int ch = 0; ch < channels; ++ch) {
                size_t n = (size_t)frame * channels + ch;
                io[n] = fm->bypass_dry[n] * (1 - wet) + io[n] * wet;
            }
        }
    }
}

// Processes the master and closes the block's shared parameter-update boundary.
void fxm_render_master(EffectsManager* fm, float* io, int frames, int channels) {
    if (!fm) return;
    fxm_render_chain(fm, &fm->master, true, -1, io, frames, channels);
    fm->block_prepared = false;
}

// Processes one track while leaving compensation and master summing to the engine.
void fxm_render_track(EffectsManager* fm, int track, float* io, int frames, int channels) {
    if (!fm || track < 0 || track >= fm->track_count) return;
    fxm_render_chain(fm, &fm->tracks[track], false, track, io, frames, channels);
}

// Resets one exclusively owned chain without allocating or changing its authored parameters.
static void chain_reset_render_state(FxChain* chain) {
    for (int i = 0; i < chain->count; ++i) {
        FxInstance* instance = &chain->items[i];
        for (uint32_t p = 0; p < instance->param_count; ++p) {
            instance->param_current[p] = instance->param_values[p];
            if (instance->vt.set_param) instance->vt.set_param(instance->handle, p, instance->param_current[p]);
        }
        if (instance->vt.reset) instance->vt.reset(instance->handle);
    }
}

// Clears all render-owned delay, dynamics, and filter histories at an explicit transport discontinuity.
void fxm_reset_render_state(EffectsManager* fm) {
    if (!fm) return;
    fm->block_prepared = false;
    chain_clear_timing(&fm->master, fm->max_channels);
    for (int t = 0; t < fm->track_count; ++t) chain_clear_timing(&fm->tracks[t], fm->max_channels);
    chain_reset_render_state(&fm->master);
    for (int t = 0; t < fm->track_count; ++t) chain_reset_render_state(&fm->tracks[t]);
}

// Prepares a complete restored instance on a disposable manager before engine publication.
bool fxm_restore_instance(EffectsManager* fm, int track_index, int position, const FxMasterInstanceInfo* info) {
    if (!fm || !info || !info->id || info->id == UINT32_MAX || info->param_count > FX_MAX_PARAMS ||
        track_index < -1 || track_index >= fm->track_count) return false;
    FxChain* chain = track_index < 0 ? &fm->master : &fm->tracks[track_index];
    if (position < 0 || position > chain->count) return false;
    for (int t = -1; t < fm->track_count; ++t) {
        FxChain* existing = t < 0 ? &fm->master : &fm->tracks[t];
        for (int i = 0; i < existing->count; ++i) if (existing->items[i].id == info->id) return false;
    }
    for (uint32_t p = 0; p < info->param_count; ++p)
        if (!isfinite(info->params[p]) || !isfinite(info->param_beats[p]) ||
            info->param_mode[p] < FX_PARAM_MODE_NATIVE || info->param_mode[p] > FX_PARAM_MODE_BEAT_RATE ||
            (info->param_mode[p] != FX_PARAM_MODE_NATIVE && info->param_beats[p] <= 0)) return false;
    FxInstId generated = track_index < 0 ? fxm_master_add(fm, info->type) : fxm_track_add(fm, track_index, info->type);
    if (!generated) return false;
    FxInstance* added = &chain->items[chain->count - 1];
    if (added->param_count != info->param_count) return false;
    added->id = info->id;
    if (fm->next_inst_id <= info->id) fm->next_inst_id = info->id + 1;
    for (uint32_t p = 0; p < info->param_count; ++p) {
        bool ok = track_index < 0 ?
            fxm_master_set_param_with_mode(fm, info->id, p, info->params[p], info->param_mode[p], info->param_beats[p]) :
            fxm_track_set_param_with_mode(fm, track_index, info->id, p, info->params[p], info->param_mode[p], info->param_beats[p]);
        if (!ok) return false;
    }
    return track_index < 0 ?
        fxm_master_set_enabled(fm, info->id, info->enabled) && fxm_master_reorder(fm, info->id, position) :
        fxm_track_set_enabled(fm, track_index, info->id, info->enabled) && fxm_track_reorder(fm, track_index, info->id, position);
}
