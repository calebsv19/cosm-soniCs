#include "engine/engine.h"
#include "config.h"
#include "app_state.h"
#include "input/timeline_drag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* message) {
    fprintf(stderr, "clip_overlap_priority_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

static const char* const kClipPaths[] = {
    "assets/audio/test.wav",
    "assets/audio/sample-9s.wav",
    "assets/audio/sample-12s.wav",
};

static int find_clip_index_by_id(const EngineTrack* track, uint64_t creation_index) {
    if (!track) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].creation_index == creation_index) {
            return i;
        }
    }
    return -1;
}

static int find_clip_index_by_start(const EngineTrack* track, uint64_t start_frames) {
    if (!track) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].timeline_start_frames == start_frames) {
            return i;
        }
    }
    return -1;
}

int main(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);

    Engine* engine = engine_create(&cfg);
    if (!engine) {
        fail("engine_create failed");
    }

    expect(engine_get_track_count(engine) > 0, "engine has no default track");
    const int track_index = 0;

    uint64_t creation_order[3] = {0};

    int clip_index = -1;
    expect(engine_add_clip_to_track(engine, track_index, kClipPaths[0], 0, &clip_index), "failed to add first clip");
    const EngineTrack* tracks = engine_get_tracks(engine);
    expect(tracks != NULL, "engine_get_tracks failed");
    const EngineTrack* track = &tracks[track_index];
    expect(track->clip_count == 1, "expected one clip after first add");
    creation_order[0] = track->clips[0].creation_index;

    expect(engine_add_clip_to_track(engine, track_index, kClipPaths[1], 0, &clip_index), "failed to add second clip");
    tracks = engine_get_tracks(engine);
    track = &tracks[track_index];
    expect(track->clip_count == 2, "expected two clips after second add");
    expect(track->clips[0].timeline_start_frames == track->clips[1].timeline_start_frames, "clips should share start frame");
    creation_order[1] = track->clips[1].creation_index;
    expect(track->clips[0].creation_index < track->clips[1].creation_index, "newer clip should have higher creation index");

    int original_index = find_clip_index_by_id(track, creation_order[0]);
    expect(original_index >= 0, "failed to locate original clip");
    expect(engine_clip_set_timeline_start(engine, track_index, original_index, 9600, NULL), "failed to move clip 0");
    tracks = engine_get_tracks(engine);
    track = &tracks[track_index];
    original_index = find_clip_index_by_id(track, creation_order[0]);
    expect(original_index >= 0, "failed to locate moved clip");
    expect(engine_clip_set_timeline_start(engine, track_index, original_index, 0, NULL), "failed to return clip 0 to start");
    tracks = engine_get_tracks(engine);
    track = &tracks[track_index];
    expect(track->clip_count == 2, "clip count changed unexpectedly");
    expect(track->clips[0].creation_index < track->clips[1].creation_index, "creation order changed after move");

    expect(engine_add_clip_to_track(engine, track_index, kClipPaths[2], 0, &clip_index), "failed to add third clip");
    tracks = engine_get_tracks(engine);
    track = &tracks[track_index];
    expect(track->clip_count == 3, "expected three clips after third add");
    creation_order[2] = track->clips[2].creation_index;
    expect(creation_order[1] < creation_order[2], "third clip creation index ordering incorrect");

    for (int i = 0; i < track->clip_count - 1; ++i) {
        expect(track->clips[i].creation_index < track->clips[i + 1].creation_index,
               "creation indices not strictly increasing");
    }

    
    // --- Overlap trimming regression ---
    AppState state;
    memset(&state, 0, sizeof(state));
    state.engine = engine;

    int overlap_track_index = engine_add_track(engine);
    expect(overlap_track_index >= 0, "failed to add overlap test track");

    int added_index = -1;
    expect(engine_add_clip_to_track(engine, overlap_track_index, kClipPaths[0], 0, &added_index),
           "failed to add left clip");
    tracks = engine_get_tracks(engine);
    const EngineTrack* overlap_track = &tracks[overlap_track_index];
    int left_clip_index = find_clip_index_by_start(overlap_track, 0);
    expect(left_clip_index >= 0, "unable to locate left clip by start");
    uint64_t total_frames = engine_clip_get_total_frames(engine, overlap_track_index, left_clip_index);
    expect(total_frames > 8, "test clip too short");
    uint64_t quarter = total_frames / 4;
    expect(quarter > 0, "quarter length invalid");
    uint64_t half = quarter * 2;
    expect(engine_clip_set_region(engine, overlap_track_index, left_clip_index, 0, half),
           "failed to shape left clip");

    expect(engine_add_clip_to_track(engine, overlap_track_index, kClipPaths[0], half, &added_index),
           "failed to add right clip");
    tracks = engine_get_tracks(engine);
    overlap_track = &tracks[overlap_track_index];
    int right_clip_index = find_clip_index_by_start(overlap_track, half);
    expect(right_clip_index >= 0, "unable to locate right clip by start");
    expect(engine_clip_set_region(engine, overlap_track_index, right_clip_index, 0, half),
           "failed to shape right clip");

    expect(engine_add_clip_to_track(engine, overlap_track_index, kClipPaths[0], quarter, &added_index),
           "failed to add drop clip");
    tracks = engine_get_tracks(engine);
    overlap_track = &tracks[overlap_track_index];
    int drop_clip_index = find_clip_index_by_start(overlap_track, quarter);
    expect(drop_clip_index >= 0, "unable to locate drop clip by start");
    expect(engine_clip_set_region(engine, overlap_track_index, drop_clip_index, 0, half),
           "failed to size drop clip");

    overlap_track = &tracks[overlap_track_index];
    expect(overlap_track->clip_count == 3, "unexpected clip count before resolve");
    EngineSamplerSource* drop_sampler = overlap_track->clips[drop_clip_index].sampler;

    int final_drop_index = timeline_resolve_overlapping_clips(&state, overlap_track_index, drop_sampler);
    expect(final_drop_index >= 0, "resolve overlapping clips failed");

    tracks = engine_get_tracks(engine);
    overlap_track = &tracks[overlap_track_index];
    expect(overlap_track->clip_count == 3, "clip count incorrect after resolve");

    fprintf(stderr, "after resolve: count=%d\n", overlap_track->clip_count);
    for (int i = 0; i < overlap_track->clip_count; ++i) {
        const EngineClip* clip = &overlap_track->clips[i];
        fprintf(stderr, " clip %d start=%llu dur=%llu offset=%llu name=%s\n", i, (unsigned long long)clip->timeline_start_frames, (unsigned long long)clip->duration_frames, (unsigned long long)clip->offset_frames, clip->name);
    }
    const EngineClip* a = &overlap_track->clips[0];
    const EngineClip* b = &overlap_track->clips[1];
    const EngineClip* c = &overlap_track->clips[2];

    expect(a->timeline_start_frames == 0, "left clip start incorrect");
    fprintf(stderr, "durations: left=%llu middle=%llu right=%llu quarter=%llu half=%llu\n", (unsigned long long)a->duration_frames, (unsigned long long)b->duration_frames, (unsigned long long)c->duration_frames, (unsigned long long)quarter, (unsigned long long)half);
    expect(a->duration_frames == quarter, "left clip duration incorrect");
    expect(b->timeline_start_frames == quarter, "middle clip start incorrect");
    expect(b->duration_frames == half, "middle clip duration incorrect");
    expect(c->timeline_start_frames == quarter * 3, "right clip start incorrect");
    expect(c->duration_frames == quarter, "right clip duration incorrect");

    expect(a->timeline_start_frames + a->duration_frames <= b->timeline_start_frames, "left overlaps middle");
    expect(b->timeline_start_frames + b->duration_frames <= c->timeline_start_frames, "middle overlaps right");

    expect(a->fade_in_frames <= a->duration_frames, "left fade too long");
    expect(c->fade_in_frames <= c->duration_frames, "right fade too long");

    int final_count = track->clip_count;
    engine_destroy(engine);
    printf("clip_overlap_priority_test: success (clips=%d)\n", final_count);
    return 0;
}
