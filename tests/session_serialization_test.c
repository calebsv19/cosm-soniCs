#include "session.h"
#include "engine/engine.h"

#include <SDL2/SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* kTestOutputPath = "build/tests/sample_session.json";

typedef struct LibraryBrowser LibraryBrowser;

const EngineRuntimeConfig* engine_get_config(const Engine* engine) {
    (void)engine;
    return NULL;
}

bool engine_transport_is_playing(const Engine* engine) {
    (void)engine;
    return false;
}

uint64_t engine_get_transport_frame(const Engine* engine) {
    (void)engine;
    return 0;
}

const EngineTrack* engine_get_tracks(const Engine* engine) {
    (void)engine;
    return NULL;
}

int engine_get_track_count(const Engine* engine) {
    (void)engine;
    return 0;
}

Engine* engine_create(const EngineRuntimeConfig* cfg) {
    (void)cfg;
    return (Engine*)0x1;
}

void engine_destroy(Engine* engine) {
    (void)engine;
}

void engine_stop(Engine* engine) {
    (void)engine;
}

bool engine_transport_set_loop(Engine* engine, bool enabled, uint64_t start_frame, uint64_t end_frame) {
    (void)engine;
    (void)enabled;
    (void)start_frame;
    (void)end_frame;
    return true;
}

bool engine_transport_stop(Engine* engine) {
    (void)engine;
    return true;
}

bool engine_transport_seek(Engine* engine, uint64_t frame) {
    (void)engine;
    (void)frame;
    return true;
}

int engine_add_track(Engine* engine) {
    static int next_track = 0;
    (void)engine;
    return next_track++;
}

bool engine_track_set_name(Engine* engine, int track_index, const char* name) {
    (void)engine;
    (void)track_index;
    (void)name;
    return true;
}

bool engine_track_set_gain(Engine* engine, int track_index, float gain) {
    (void)engine;
    (void)track_index;
    (void)gain;
    return true;
}

bool engine_track_set_muted(Engine* engine, int track_index, bool muted) {
    (void)engine;
    (void)track_index;
    (void)muted;
    return true;
}

bool engine_track_set_solo(Engine* engine, int track_index, bool solo) {
    (void)engine;
    (void)track_index;
    (void)solo;
    return true;
}

bool engine_add_clip_to_track(Engine* engine, int track_index, const char* filepath, uint64_t start_frame, int* out_clip_index) {
    static int next_clip = 0;
    (void)engine;
    (void)track_index;
    (void)filepath;
    (void)start_frame;
    if (out_clip_index) {
        *out_clip_index = next_clip++;
    }
    return true;
}

bool engine_clip_set_region(Engine* engine, int track_index, int clip_index, uint64_t offset_frames, uint64_t duration_frames) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)offset_frames;
    (void)duration_frames;
    return true;
}

bool engine_clip_set_gain(Engine* engine, int track_index, int clip_index, float gain) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)gain;
    return true;
}

bool engine_clip_set_name(Engine* engine, int track_index, int clip_index, const char* name) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)name;
    return true;
}

bool engine_clip_set_fades(Engine* engine, int track_index, int clip_index, uint64_t fade_in_frames, uint64_t fade_out_frames) {
    (void)engine;
    (void)track_index;
    (void)clip_index;
    (void)fade_in_frames;
    (void)fade_out_frames;
    return true;
}

bool engine_remove_track(Engine* engine, int track_index) {
    (void)engine;
    (void)track_index;
    return true;
}

bool engine_fx_master_snapshot(const Engine* engine, FxMasterSnapshot* out_snapshot) {
    (void)engine;
    if (out_snapshot) {
        SDL_zero(*out_snapshot);
    }
    return true;
}

FxInstId engine_fx_master_add(Engine* engine, FxTypeId type) {
    (void)engine;
    (void)type;
    return (FxInstId)1;
}

bool engine_fx_master_remove(Engine* engine, FxInstId id) {
    (void)engine;
    (void)id;
    return true;
}

bool engine_fx_master_set_param(Engine* engine, FxInstId id, uint32_t param_index, float value) {
    (void)engine;
    (void)id;
    (void)param_index;
    (void)value;
    return true;
}

bool engine_fx_master_set_enabled(Engine* engine, FxInstId id, bool enabled) {
    (void)engine;
    (void)id;
    (void)enabled;
    return true;
}

void library_browser_init(LibraryBrowser* browser, const char* directory) {
    (void)browser;
    (void)directory;
}

void library_browser_scan(LibraryBrowser* browser) {
    (void)browser;
}

int main(void) {
    SessionDocument doc;
    session_document_init(&doc);

    doc.engine.sample_rate = 48000;
    doc.engine.block_size = 128;
    doc.engine.default_fade_in_ms = 5.0f;
    doc.engine.default_fade_out_ms = 15.0f;
    doc.engine.fade_preset_count = 3;
    doc.engine.fade_preset_ms[0] = 0.0f;
    doc.engine.fade_preset_ms[1] = 12.5f;
    doc.engine.fade_preset_ms[2] = 55.0f;
    for (int i = doc.engine.fade_preset_count; i < CONFIG_FADE_PRESET_MAX; ++i) {
        doc.engine.fade_preset_ms[i] = 0.0f;
    }
    doc.engine.enable_engine_logs = true;
    doc.engine.enable_cache_logs = false;
    doc.engine.enable_timing_logs = true;

    doc.loop.enabled = false;
    doc.loop.start_frame = 0;
    doc.loop.end_frame = 0;

    doc.timeline.visible_seconds = 8.0f;
    doc.timeline.vertical_scale = 1.0f;
    doc.timeline.show_all_grid_lines = false;
    doc.timeline.playhead_frame = 0;

    doc.layout.transport_ratio = 0.3f;
    doc.layout.library_ratio = 0.25f;
    doc.layout.mixer_ratio = 0.45f;

    strncpy(doc.library.directory, "assets/audio", sizeof(doc.library.directory) - 1);
    doc.library.directory[sizeof(doc.library.directory) - 1] = '\0';
    doc.library.selected_index = 0;

    doc.transport_playing = false;
    doc.transport_frame = 0;

    doc.track_count = 1;
    doc.tracks = (SessionTrack*)calloc(1, sizeof(SessionTrack));
    if (!doc.tracks) {
        SDL_Log("session_serialization_test: failed to allocate track array");
        session_document_free(&doc);
        return 1;
    }

    SessionTrack* track = &doc.tracks[0];
    strncpy(track->name, "Test Track", sizeof(track->name) - 1);
    track->name[sizeof(track->name) - 1] = '\0';
    track->gain = 0.8f;
    track->muted = false;
    track->solo = false;
    track->clip_count = 1;
    track->clips = (SessionClip*)calloc(1, sizeof(SessionClip));
    if (!track->clips) {
        SDL_Log("session_serialization_test: failed to allocate clip array");
        session_document_free(&doc);
        return 2;
    }

    SessionClip* clip = &track->clips[0];
    strncpy(clip->name, "Test Clip", sizeof(clip->name) - 1);
    clip->name[sizeof(clip->name) - 1] = '\0';
    strncpy(clip->media_path, "assets/audio/test.wav", sizeof(clip->media_path) - 1);
    clip->media_path[sizeof(clip->media_path) - 1] = '\0';
    clip->start_frame = 0;
    clip->duration_frames = 48000;
    clip->offset_frames = 0;
    clip->fade_in_frames = 1200;
    clip->fade_out_frames = 2400;
    clip->gain = 1.0f;
    clip->selected = false;

    SDL_Log("session_serialization_test: writing %s", kTestOutputPath);
    bool ok = session_document_write_file(&doc, kTestOutputPath);
    if (!ok) {
        session_document_free(&doc);
        SDL_Log("session_serialization_test: failed to write session file");
        return 3;
    }

    SessionDocument loaded;
    session_document_init(&loaded);
    if (!session_document_read_file(kTestOutputPath, &loaded)) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: failed to read session file");
        return 4;
    }

    if (loaded.track_count != 1 || loaded.tracks[0].clip_count != 1) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: deserialised counts mismatch");
        return 5;
    }
    if (fabsf(loaded.engine.default_fade_in_ms - doc.engine.default_fade_in_ms) > 0.01f ||
        fabsf(loaded.engine.default_fade_out_ms - doc.engine.default_fade_out_ms) > 0.01f ||
        loaded.engine.fade_preset_count != doc.engine.fade_preset_count) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: engine fade config mismatch");
        return 6;
    }
    for (int i = 0; i < loaded.engine.fade_preset_count; ++i) {
        if (fabsf(loaded.engine.fade_preset_ms[i] - doc.engine.fade_preset_ms[i]) > 0.01f) {
            session_document_free(&doc);
            session_document_free(&loaded);
            SDL_Log("session_serialization_test: fade preset %d mismatch", i);
            return 7;
        }
    }
    if (loaded.engine.enable_engine_logs != doc.engine.enable_engine_logs ||
        loaded.engine.enable_cache_logs != doc.engine.enable_cache_logs ||
        loaded.engine.enable_timing_logs != doc.engine.enable_timing_logs) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: engine logging flags mismatch");
        return 8;
    }
    SessionTrack* lt = &loaded.tracks[0];
    SessionClip* lc = &lt->clips[0];
    if (strcmp(lt->name, "Test Track") != 0 || strcmp(lc->name, "Test Clip") != 0 ||
        strcmp(lc->media_path, "assets/audio/test.wav") != 0 || lc->duration_frames != 48000 ||
        lc->fade_in_frames != 1200 || lc->fade_out_frames != 2400) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: deserialised content mismatch");
        return 9;
    }

    session_document_free(&doc);
    session_document_free(&loaded);

    FILE* file = fopen(kTestOutputPath, "rb");
    if (!file) {
        SDL_Log("session_serialization_test: output file missing");
        return 10;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        SDL_Log("session_serialization_test: failed to seek output file");
        fclose(file);
        return 11;
    }
    long size = ftell(file);
    fclose(file);
    if (size <= 0) {
        SDL_Log("session_serialization_test: output file empty");
        return 12;
    }

    SDL_Log("session_serialization_test: success (%ld bytes)", size);
    return 0;
}
