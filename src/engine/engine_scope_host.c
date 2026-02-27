#include "engine/engine_internal.h"
#include "core/loop/daw_mainthread_messages.h"

#include <stdlib.h>
#include <string.h>

// Initializes a scope bank with empty taps and ready ring buffers.
static bool engine_scope_init_bank(EngineFxScopeBank* bank) {
    if (!bank) {
        return false;
    }
    SDL_zero(*bank);
    for (int i = 0; i < FX_MASTER_MAX; ++i) {
        if (!ringbuf_init(&bank->taps[i].buffer, ENGINE_SCOPE_STREAM_CAPACITY_BYTES)) {
            for (int j = 0; j < i; ++j) {
                ringbuf_free(&bank->taps[j].buffer);
            }
            return false;
        }
        bank->taps[i].id = 0;
        bank->taps[i].kind = ENGINE_SCOPE_STREAM_NONE;
    }
    return true;
}

// Releases ring buffers held by a scope bank.
static void engine_scope_free_bank(EngineFxScopeBank* bank) {
    if (!bank) {
        return;
    }
    for (int i = 0; i < FX_MASTER_MAX; ++i) {
        ringbuf_free(&bank->taps[i].buffer);
    }
    SDL_zero(*bank);
}

// Finds or allocates a scope tap for a given FX instance.
static EngineFxScopeTap* engine_scope_get_tap(EngineFxScopeBank* bank, FxInstId id, EngineScopeStreamKind kind) {
    if (!bank || id == 0 || kind == ENGINE_SCOPE_STREAM_NONE) {
        return NULL;
    }
    for (int i = 0; i < bank->count; ++i) {
        if (bank->taps[i].id == id && bank->taps[i].kind == kind) {
            return &bank->taps[i];
        }
    }
    EngineFxScopeTap* tap = NULL;
    if (bank->count < FX_MASTER_MAX) {
        tap = &bank->taps[bank->count++];
    } else {
        tap = &bank->taps[FX_MASTER_MAX - 1];
    }
    if (tap) {
        tap->id = id;
        tap->kind = kind;
        ringbuf_reset(&tap->buffer);
    }
    return tap;
}

// Initializes the scope host for master and track FX instances.
static bool engine_scope_init(EngineScopeHost* host, int track_capacity) {
    if (!host || track_capacity < 0) {
        return false;
    }
    SDL_zero(*host);
    if (!engine_scope_init_bank(&host->master)) {
        return false;
    }
    if (track_capacity == 0) {
        return true;
    }
    host->tracks = (EngineFxScopeBank*)calloc((size_t)track_capacity, sizeof(EngineFxScopeBank));
    if (!host->tracks) {
        engine_scope_free_bank(&host->master);
        return false;
    }
    host->track_capacity = track_capacity;
    for (int i = 0; i < track_capacity; ++i) {
        if (!engine_scope_init_bank(&host->tracks[i])) {
            for (int j = 0; j < i; ++j) {
                engine_scope_free_bank(&host->tracks[j]);
            }
            free(host->tracks);
            host->tracks = NULL;
            host->track_capacity = 0;
            engine_scope_free_bank(&host->master);
            return false;
        }
    }
    return true;
}

// Releases storage owned by the scope host.
static void engine_scope_free(EngineScopeHost* host) {
    if (!host) {
        return;
    }
    engine_scope_free_bank(&host->master);
    if (host->tracks) {
        for (int i = 0; i < host->track_capacity; ++i) {
            engine_scope_free_bank(&host->tracks[i]);
        }
        free(host->tracks);
    }
    SDL_zero(*host);
}

// Writes a scalar sample into a scope tap ring buffer.
static bool engine_scope_write_scalar(EngineScopeHost* host,
                                      bool is_master,
                                      int track_index,
                                      FxInstId id,
                                      EngineScopeStreamKind kind,
                                      float value) {
    if (!host || id == 0) {
        return false;
    }
    EngineFxScopeBank* bank = NULL;
    if (is_master) {
        bank = &host->master;
    } else {
        if (!host->tracks || track_index < 0 || track_index >= host->track_capacity) {
            return false;
        }
        bank = &host->tracks[track_index];
    }
    EngineFxScopeTap* tap = engine_scope_get_tap(bank, id, kind);
    if (!tap) {
        return false;
    }
    return ringbuf_write(&tap->buffer, &value, sizeof(value)) == sizeof(value);
}

// Reads buffered samples for a scope tap into the caller's buffer.
static int engine_scope_read_samples(const EngineScopeHost* host,
                                     bool is_master,
                                     int track_index,
                                     FxInstId id,
                                     EngineScopeStreamKind kind,
                                     float* out_samples,
                                     int max_samples) {
    if (!host || !out_samples || max_samples <= 0 || id == 0) {
        return 0;
    }
    const EngineFxScopeBank* bank = NULL;
    if (is_master) {
        bank = &host->master;
    } else {
        if (!host->tracks || track_index < 0 || track_index >= host->track_capacity) {
            return 0;
        }
        bank = &host->tracks[track_index];
    }
    for (int i = 0; i < bank->count; ++i) {
        const EngineFxScopeTap* tap = &bank->taps[i];
        if (tap->id == id && tap->kind == kind) {
            size_t available_bytes = ringbuf_available_read(&tap->buffer);
            size_t max_bytes = (size_t)max_samples * sizeof(float);
            if (available_bytes > max_bytes) {
                available_bytes = max_bytes;
            }
            size_t read = ringbuf_read((RingBuffer*)&tap->buffer, out_samples, available_bytes);
            return (int)(read / sizeof(float));
        }
    }
    return 0;
}

bool engine_scope_get_stream_desc(EngineScopeStreamKind kind, EngineScopeStreamDesc* out_desc) {
    if (!out_desc) {
        return false;
    }
    SDL_zero(*out_desc);
    switch (kind) {
        case ENGINE_SCOPE_STREAM_GAIN_REDUCTION:
            out_desc->kind = kind;
            out_desc->format = ENGINE_SCOPE_FORMAT_SCALAR;
            out_desc->name = "Gain Reduction";
            out_desc->stride = 1;
            out_desc->update_rate_hz = 60;
            return true;
        default:
            return false;
    }
}

int engine_get_fx_scope_samples(const Engine* engine,
                                bool is_master,
                                int track_index,
                                FxInstId id,
                                EngineScopeStreamKind kind,
                                float* out_samples,
                                int max_samples) {
    if (!engine) {
        return 0;
    }
    return engine_scope_read_samples(&engine->scope_host, is_master, track_index, id, kind, out_samples, max_samples);
}

void engine_scope_write_gr(Engine* engine, bool is_master, int track_index, FxInstId id, float gr_db) {
    if (!engine) {
        return;
    }
    engine_scope_write_scalar(&engine->scope_host,
                              is_master,
                              track_index,
                              id,
                              ENGINE_SCOPE_STREAM_GAIN_REDUCTION,
                              gr_db);
}

bool engine_scope_ensure_track_capacity(Engine* engine, int required_tracks) {
    if (!engine || required_tracks <= engine->scope_host.track_capacity) {
        return true;
    }
    int new_capacity = engine->scope_host.track_capacity;
    if (new_capacity < 1) {
        new_capacity = 1;
    }
    while (new_capacity < required_tracks) {
        new_capacity *= 2;
    }
    EngineFxScopeBank* resized = (EngineFxScopeBank*)realloc(engine->scope_host.tracks,
                                                             sizeof(EngineFxScopeBank) * (size_t)new_capacity);
    if (!resized) {
        return false;
    }
    engine->scope_host.tracks = resized;
    for (int i = engine->scope_host.track_capacity; i < new_capacity; ++i) {
        if (!engine_scope_init_bank(&engine->scope_host.tracks[i])) {
            return false;
        }
    }
    engine->scope_host.track_capacity = new_capacity;
    return true;
}

// Routes scope tap samples from the effects manager into the scope host.
static void engine_fx_scope_tap_callback(void* user,
                                         bool is_master,
                                         int track_index,
                                         FxInstId id,
                                         FxTypeId type,
                                         float value) {
    (void)type;
    Engine* engine = (Engine*)user;
    if (!engine) {
        return;
    }
    engine_scope_write_gr(engine, is_master, track_index, id, value);
    (void)daw_mainthread_message_post(DAW_MAINTHREAD_MSG_ENGINE_FX_SCOPE,
                                      (uint64_t)id,
                                      engine);
}

void engine_register_fx_scope_tap(Engine* engine) {
    if (!engine || !engine->fxm_mutex) {
        return;
    }
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        fxm_set_scope_tap_callback(engine->fxm, engine_fx_scope_tap_callback, engine);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
}

// Initializes scope host storage for a newly created engine.
bool engine_scope_host_init(Engine* engine, int track_capacity) {
    if (!engine) {
        return false;
    }
    return engine_scope_init(&engine->scope_host, track_capacity);
}

// Releases scope host storage for a destroyed engine.
void engine_scope_host_free(Engine* engine) {
    if (!engine) {
        return;
    }
    engine_scope_free(&engine->scope_host);
}

void engine_scope_reset_track_bank(Engine* engine, int track_index) {
    if (!engine || !engine->scope_host.tracks) {
        return;
    }
    if (track_index < 0 || track_index >= engine->scope_host.track_capacity) {
        return;
    }
    EngineFxScopeBank* bank = &engine->scope_host.tracks[track_index];
    bank->count = 0;
    for (int i = 0; i < FX_MASTER_MAX; ++i) {
        bank->taps[i].id = 0;
        bank->taps[i].kind = ENGINE_SCOPE_STREAM_NONE;
        ringbuf_reset(&bank->taps[i].buffer);
    }
}
