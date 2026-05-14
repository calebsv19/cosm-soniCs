#include "engine/engine.h"
#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    "tmp/clip_overlap_priority_a.wav",
    "tmp/clip_overlap_priority_b.wav",
    "tmp/clip_overlap_priority_c.wav",
};

static void write_u16_le(FILE* fp, uint16_t value) {
    unsigned char bytes[2];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    fwrite(bytes, 1, sizeof(bytes), fp);
}

static void write_u32_le(FILE* fp, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    fwrite(bytes, 1, sizeof(bytes), fp);
}

static void write_test_wav_or_fail(const char* path, int sample_rate, uint32_t frames) {
    const uint16_t channels = 1;
    const uint16_t bits_per_sample = 16;
    const uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    const uint32_t byte_rate = (uint32_t)sample_rate * (uint32_t)block_align;
    const uint32_t data_size = frames * (uint32_t)block_align;
    const uint32_t riff_size = 36u + data_size;
    FILE* fp = NULL;

    if (mkdir("tmp", 0755) != 0 && errno != EEXIST) {
        fail("failed to create tmp directory");
    }

    fp = fopen(path, "wb");
    if (!fp) {
        fail("failed to create wav fixture");
    }

    fwrite("RIFF", 1, 4, fp);
    write_u32_le(fp, riff_size);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    write_u32_le(fp, 16u);
    write_u16_le(fp, 1u);
    write_u16_le(fp, channels);
    write_u32_le(fp, (uint32_t)sample_rate);
    write_u32_le(fp, byte_rate);
    write_u16_le(fp, block_align);
    write_u16_le(fp, bits_per_sample);
    fwrite("data", 1, 4, fp);
    write_u32_le(fp, data_size);

    for (uint32_t i = 0; i < frames; ++i) {
        write_u16_le(fp, 0u);
    }

    fclose(fp);
}

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
    for (size_t i = 0; i < sizeof(kClipPaths) / sizeof(kClipPaths[0]); ++i) {
        write_test_wav_or_fail(kClipPaths[i], cfg.sample_rate, (uint32_t)(cfg.sample_rate * 10));
    }

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
    struct EngineSamplerSource* drop_sampler = overlap_track->clips[drop_clip_index].sampler;

    int final_drop_index = -1;
    expect(engine_track_apply_no_overlap(engine, overlap_track_index, drop_sampler, &final_drop_index),
           "resolve overlapping clips failed");
    expect(final_drop_index >= 0, "no-overlap returned invalid index");

    tracks = engine_get_tracks(engine);
    overlap_track = &tracks[overlap_track_index];
    expect(overlap_track->clip_count == 3, "clip count incorrect after resolve");

    const EngineClip* a = &overlap_track->clips[0];
    const EngineClip* b = &overlap_track->clips[1];
    const EngineClip* c = &overlap_track->clips[2];

    expect(a->timeline_start_frames == 0, "left clip start incorrect");
    expect(a->duration_frames == quarter, "left clip duration incorrect");
    expect(b->timeline_start_frames == quarter, "middle clip start incorrect");
    expect(b->duration_frames == half, "middle clip duration incorrect");
    expect(c->timeline_start_frames == quarter * 3, "right clip start incorrect");
    expect(c->duration_frames == quarter, "right clip duration incorrect");

    expect(a->timeline_start_frames + a->duration_frames <= b->timeline_start_frames, "left overlaps middle");
    expect(b->timeline_start_frames + b->duration_frames <= c->timeline_start_frames, "middle overlaps right");

    expect(a->fade_in_frames <= a->duration_frames, "left fade too long");
    expect(c->fade_in_frames <= c->duration_frames, "right fade too long");

    // --- Left overlap regression ---
    int simple_track = engine_add_track(engine);
    expect(simple_track >= 0, "failed to add simple overlap track");

    int base_index = -1;
    expect(engine_add_clip_to_track(engine, simple_track, kClipPaths[0], quarter, &base_index),
           "failed to add base clip (simple)");
    tracks = engine_get_tracks(engine);
    const EngineTrack* simple_track_ref = &tracks[simple_track];
    int base_clip_index = find_clip_index_by_start(simple_track_ref, quarter);
    expect(base_clip_index >= 0, "could not locate base clip");
    uint64_t total_frames_simple = engine_clip_get_total_frames(engine, simple_track, base_clip_index);
    uint64_t simple_half = total_frames_simple / 2;
    expect(engine_clip_set_region(engine, simple_track, base_clip_index, 0, simple_half),
           "failed to size base clip");

    int new_simple_index = -1;
    expect(engine_add_clip_to_track(engine, simple_track, kClipPaths[0], 0, &new_simple_index),
           "failed to add new simple clip");
    tracks = engine_get_tracks(engine);
    simple_track_ref = &tracks[simple_track];
    int resolved_new_index = find_clip_index_by_start(simple_track_ref, 0);
    expect(resolved_new_index >= 0, "unable to locate new simple clip");
    expect(engine_clip_set_region(engine, simple_track, resolved_new_index, 0, simple_half),
           "failed to size new simple clip");
    tracks = engine_get_tracks(engine);
    simple_track_ref = &tracks[simple_track];
    struct EngineSamplerSource* simple_sampler = simple_track_ref->clips[resolved_new_index].sampler;

    expect(engine_track_apply_no_overlap(engine, simple_track, simple_sampler, NULL),
           "simple overlap resolve failed");

    tracks = engine_get_tracks(engine);
    simple_track_ref = &tracks[simple_track];
    expect(simple_track_ref->clip_count == 2, "simple overlap: unexpected clip count");
    const EngineClip* simple_first = &simple_track_ref->clips[0];
    const EngineClip* simple_second = &simple_track_ref->clips[1];
    if (simple_first->timeline_start_frames > simple_second->timeline_start_frames) {
        const EngineClip* tmp = simple_first;
        simple_first = simple_second;
        simple_second = tmp;
    }
    expect(simple_first->timeline_start_frames == 0, "simple overlap: new clip start incorrect");
    expect(simple_first->duration_frames == simple_half, "simple overlap: new clip duration incorrect");
    expect(simple_second->timeline_start_frames == simple_half,
           "simple overlap: trimmed clip start incorrect");
    expect(simple_second->duration_frames == simple_half / 2,
           "simple overlap: trimmed clip duration incorrect");

    // --- Right-only overlap ---
    int single_track = engine_add_track(engine);
    expect(single_track >= 0, "failed to add single overlap track");
    uint64_t base_start = half;
    int base_single = -1;
    expect(engine_add_clip_to_track(engine, single_track, kClipPaths[0], base_start, &base_single),
           "failed to add base clip (single)");
    tracks = engine_get_tracks(engine);
    const EngineTrack* single_track_ref = &tracks[single_track];
    int base_single_idx = find_clip_index_by_start(single_track_ref, base_start);
    expect(base_single_idx >= 0, "single overlap: base clip missing");
    expect(engine_clip_set_region(engine, single_track, base_single_idx, 0, half),
           "single overlap: failed to size base clip");

    int new_single_idx = -1;
    expect(engine_add_clip_to_track(engine, single_track, kClipPaths[0], quarter, &new_single_idx),
           "single overlap: failed to add inserted clip");
    tracks = engine_get_tracks(engine);
    single_track_ref = &tracks[single_track];
    int inserted_single_idx = find_clip_index_by_start(single_track_ref, quarter);
    expect(inserted_single_idx >= 0, "single overlap: inserted clip missing");
    expect(engine_clip_set_region(engine, single_track, inserted_single_idx, 0, half),
           "single overlap: failed to size inserted clip");

    struct EngineSamplerSource* inserted_single_sampler = single_track_ref->clips[inserted_single_idx].sampler;
    expect(engine_track_apply_no_overlap(engine, single_track, inserted_single_sampler, NULL),
           "single overlap: resolver failure");

    tracks = engine_get_tracks(engine);
    single_track_ref = &tracks[single_track];
    expect(single_track_ref->clip_count == 2, "single overlap: unexpected clip count");
    const EngineClip* inserted_single = &single_track_ref->clips[0];
    const EngineClip* trimmed_single = &single_track_ref->clips[1];
    if (inserted_single->timeline_start_frames > trimmed_single->timeline_start_frames) {
        const EngineClip* tmp = inserted_single;
        inserted_single = trimmed_single;
        trimmed_single = tmp;
    }
    expect(inserted_single->timeline_start_frames == quarter,
           "single overlap: inserted start incorrect");
    expect(inserted_single->duration_frames == half,
           "single overlap: inserted duration incorrect");
    uint64_t expected_trim_start = quarter + half;
    expect(trimmed_single->timeline_start_frames == expected_trim_start,
           "single overlap: trimmed start incorrect");
    uint64_t expected_trim_duration = (base_start + half) - expected_trim_start;
    expect(trimmed_single->duration_frames == expected_trim_duration,
           "single overlap: trimmed duration incorrect");

    // --- Multi-clip overlap ---
    int multi_track = engine_add_track(engine);
    expect(multi_track >= 0, "failed to add multi overlap track");
    int left_idx = -1;
    expect(engine_add_clip_to_track(engine, multi_track, kClipPaths[0], 0, &left_idx),
           "multi overlap: failed to add first clip");
    tracks = engine_get_tracks(engine);
    const EngineTrack* multi_track_ref = &tracks[multi_track];
    int left_handle = find_clip_index_by_start(multi_track_ref, 0);
    expect(left_handle >= 0, "multi overlap: missing left clip");
    uint64_t total_frames_multi = engine_clip_get_total_frames(engine, multi_track, left_handle);
    uint64_t five_seconds = total_frames_multi / 2;
    expect(engine_clip_set_region(engine, multi_track, left_handle, 0, five_seconds),
           "multi overlap: failed to size left clip");

    int right_idx = -1;
    uint64_t six_second_frame = five_seconds + (total_frames_multi / 10);
    expect(engine_add_clip_to_track(engine, multi_track, kClipPaths[0], six_second_frame, &right_idx),
           "multi overlap: failed to add right clip");
    tracks = engine_get_tracks(engine);
    multi_track_ref = &tracks[multi_track];
    int right_handle = find_clip_index_by_start(multi_track_ref, six_second_frame);
    expect(right_handle >= 0, "multi overlap: missing right clip");
    expect(engine_clip_set_region(engine, multi_track, right_handle, 0, five_seconds),
           "multi overlap: failed to size right clip");

    int insert_idx = -1;
    uint64_t three_second_frame = five_seconds / 2 + (total_frames_multi / 10);
    expect(engine_add_clip_to_track(engine, multi_track, kClipPaths[0], three_second_frame, &insert_idx),
           "multi overlap: failed to add middle clip");
    tracks = engine_get_tracks(engine);
    multi_track_ref = &tracks[multi_track];
    int middle_handle = find_clip_index_by_start(multi_track_ref, three_second_frame);
    expect(middle_handle >= 0, "multi overlap: missing inserted clip");
    expect(engine_clip_set_region(engine, multi_track, middle_handle, 0, five_seconds),
           "multi overlap: failed to size inserted clip");
    struct EngineSamplerSource* multi_sampler = multi_track_ref->clips[middle_handle].sampler;

    expect(engine_track_apply_no_overlap(engine, multi_track, multi_sampler, NULL),
           "multi overlap: resolver failure");

    tracks = engine_get_tracks(engine);
    multi_track_ref = &tracks[multi_track];
    expect(multi_track_ref->clip_count == 3, "multi overlap: unexpected clip count");
    const EngineClip* first = &multi_track_ref->clips[0];
    const EngineClip* second = &multi_track_ref->clips[1];
    const EngineClip* third = &multi_track_ref->clips[2];
    expect(first->timeline_start_frames == 0, "multi overlap: left start incorrect");
    expect(first->timeline_start_frames + first->duration_frames <= second->timeline_start_frames,
           "multi overlap: left overlaps inserted");
    expect(second->timeline_start_frames == three_second_frame,
           "multi overlap: inserted start incorrect");
    expect(second->duration_frames == five_seconds, "multi overlap: inserted duration incorrect");
    expect(third->timeline_start_frames == three_second_frame + five_seconds,
           "multi overlap: right start incorrect");
    expect(third->duration_frames == five_seconds - (third->timeline_start_frames - (six_second_frame)),
           "multi overlap: right duration incorrect");

    int final_count = track->clip_count;
    engine_destroy(engine);
    for (size_t i = 0; i < sizeof(kClipPaths) / sizeof(kClipPaths[0]); ++i) {
        (void)unlink(kClipPaths[i]);
    }
    printf("clip_overlap_priority_test: success (clips=%d)\n", final_count);
    return 0;
}
