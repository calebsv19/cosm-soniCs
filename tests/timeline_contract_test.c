#include "audio/media_clip.h"
#include "config.h"
#include "engine/engine.h"
#include "engine/timeline_contract.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const char* message) {
    fprintf(stderr, "timeline_contract_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

static void expect_u64(uint64_t actual, uint64_t expected, const char* message) {
    if (actual != expected) {
        fprintf(stderr,
                "timeline_contract_test: %s (actual=%llu expected=%llu)\n",
                message,
                (unsigned long long)actual,
                (unsigned long long)expected);
        exit(1);
    }
}

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

static void write_wav_or_fail(const char* path, int sample_rate, uint32_t frames) {
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
        write_u16_le(fp, (uint16_t)0);
    }

    fclose(fp);
}

static int find_clip_by_start(const EngineTrack* track, uint64_t start_frame) {
    if (!track) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].timeline_start_frames == start_frame) {
            return i;
        }
    }
    return -1;
}

static void test_100_bpm_frame_mapping(void) {
    TempoMap map;
    tempo_map_init(&map, 48000.0);
    TempoEvent event = {.beat = 0.0, .bpm = 100.0};
    expect(tempo_map_set_events(&map, &event, 1), "failed to set 100 BPM tempo map");

    expect_u64(daw_timeline_frames_from_beats(&map, 1.0), 28800u, "one beat at 100 BPM should be 28800 frames");
    expect_u64(daw_timeline_frames_from_beats(&map, 4.0), 115200u, "four beats at 100 BPM should be 115200 frames");
    double beats = daw_timeline_beats_from_frames(&map, 115200u);
    expect(fabs(beats - 4.0) < 0.000001, "frames should convert back to four beats");

    tempo_map_free(&map);
}

static void test_imported_loop_duration_matches_100_bpm_grid(void) {
    const char* path = "tmp/timeline_contract_100bpm_4beats_44100.wav";
    const uint32_t source_frames = (uint32_t)llround(44100.0 * 2.4);
    AudioMediaClip clip = {0};

    write_wav_or_fail(path, 44100, source_frames);
    expect(audio_media_clip_load(path, 48000, &clip), "failed to load generated 100 BPM loop");
    expect_u64(clip.frame_count, 115200u, "44100 Hz four-beat loop should resample to 115200 frames at 48000 Hz");
    expect(clip.sample_rate == 48000, "resampled loop sample rate mismatch");
    audio_media_clip_free(&clip);
    (void)unlink(path);
}

static void test_engine_cache_reuses_imported_source(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    const char* path = "tmp/timeline_contract_cache.wav";
    write_wav_or_fail(path, cfg.sample_rate, (uint32_t)(cfg.sample_rate / 2));

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed");
    int first = -1;
    int second = -1;
    expect(engine_add_clip_to_track_with_id(engine, 0, path, "cache-test-source", 0, &first),
           "failed to add first cached clip");
    expect(engine_add_clip_to_track_with_id(engine, 0, path, "cache-test-source", (uint64_t)cfg.sample_rate, &second),
           "failed to add second cached clip");

    const EngineTrack* tracks = engine_get_tracks(engine);
    expect(tracks != NULL, "missing engine tracks");
    expect(first >= 0 && second >= 0, "invalid cached clip indices");
    expect(tracks[0].clips[first].media == tracks[0].clips[second].media,
           "clips with same media id should share cached media");

    engine_destroy(engine);
    (void)unlink(path);
}

static void test_overlap_right_trim_preserves_original_end(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    const char* path = "tmp/timeline_contract_overlap_right.wav";
    const uint64_t one = (uint64_t)cfg.sample_rate;
    const uint64_t two = one * 2u;
    write_wav_or_fail(path, cfg.sample_rate, (uint32_t)(one * 8u));

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed");
    int track_index = engine_add_track(engine);
    expect(track_index >= 0, "failed to add overlap track");

    int base_index = -1;
    int insert_index = -1;
    expect(engine_add_clip_to_track(engine, track_index, path, two, &base_index), "failed to add right-trim base");
    expect(engine_clip_set_region(engine, track_index, base_index, 0, two), "failed to size right-trim base");
    expect(engine_add_clip_to_track(engine, track_index, path, one, &insert_index), "failed to add right-trim insert");
    expect(engine_clip_set_region(engine, track_index, insert_index, 0, two), "failed to size right-trim insert");

    const EngineTrack* tracks = engine_get_tracks(engine);
    const EngineTrack* track = &tracks[track_index];
    int anchor_index = find_clip_by_start(track, one);
    expect(anchor_index >= 0, "missing right-trim anchor");
    struct EngineSamplerSource* anchor = track->clips[anchor_index].sampler;
    expect(engine_track_apply_no_overlap(engine, track_index, anchor, NULL), "right-trim overlap resolution failed");

    tracks = engine_get_tracks(engine);
    track = &tracks[track_index];
    int inserted_idx = find_clip_by_start(track, one);
    int tail_idx = find_clip_by_start(track, one * 3u);
    if (tail_idx < 0) {
        fprintf(stderr, "timeline_contract_test: right-trim clip dump count=%d\n", track->clip_count);
        for (int i = 0; i < track->clip_count; ++i) {
            fprintf(stderr,
                    "  clip[%d] start=%llu duration=%llu offset=%llu\n",
                    i,
                    (unsigned long long)track->clips[i].timeline_start_frames,
                    (unsigned long long)track->clips[i].duration_frames,
                    (unsigned long long)track->clips[i].offset_frames);
        }
    }
    expect(inserted_idx >= 0, "right-trim inserted clip missing");
    expect(tail_idx >= 0, "right-trim tail clip missing");
    expect_u64(track->clips[inserted_idx].duration_frames, two, "right-trim inserted duration mismatch");
    expect_u64(track->clips[tail_idx].duration_frames, one, "right-trim tail duration should preserve original end");
    expect_u64(track->clips[tail_idx].offset_frames, one, "right-trim tail offset mismatch");

    engine_destroy(engine);
    (void)unlink(path);
}

static void test_overlap_split_and_cover_contracts(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    const char* path = "tmp/timeline_contract_overlap_split.wav";
    const uint64_t one = (uint64_t)cfg.sample_rate;
    const uint64_t two = one * 2u;
    const uint64_t four = one * 4u;
    write_wav_or_fail(path, cfg.sample_rate, (uint32_t)(one * 8u));

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed");
    int split_track = engine_add_track(engine);
    expect(split_track >= 0, "failed to add split track");

    int long_index = -1;
    int insert_index = -1;
    expect(engine_add_clip_to_track(engine, split_track, path, 0, &long_index), "failed to add split base");
    expect(engine_clip_set_region(engine, split_track, long_index, 0, four), "failed to size split base");
    expect(engine_add_clip_to_track(engine, split_track, path, one, &insert_index), "failed to add split insert");
    expect(engine_clip_set_region(engine, split_track, insert_index, 0, one), "failed to size split insert");

    const EngineTrack* tracks = engine_get_tracks(engine);
    const EngineTrack* track = &tracks[split_track];
    int anchor_index = find_clip_by_start(track, one);
    expect(anchor_index >= 0, "missing split anchor");
    struct EngineSamplerSource* anchor = track->clips[anchor_index].sampler;
    expect(engine_track_apply_no_overlap(engine, split_track, anchor, NULL), "split overlap resolution failed");

    tracks = engine_get_tracks(engine);
    track = &tracks[split_track];
    expect(track->clip_count == 3, "split should leave left, insert, and right tail");
    int left_idx = find_clip_by_start(track, 0);
    int insert_idx = find_clip_by_start(track, one);
    int right_idx = find_clip_by_start(track, two);
    expect(left_idx >= 0 && insert_idx >= 0 && right_idx >= 0, "split clip starts missing");
    expect_u64(track->clips[left_idx].duration_frames, one, "split left duration mismatch");
    expect_u64(track->clips[insert_idx].duration_frames, one, "split insert duration mismatch");
    expect_u64(track->clips[right_idx].duration_frames, two, "split right duration mismatch");
    expect_u64(track->clips[right_idx].offset_frames, two, "split right source offset mismatch");

    int cover_track = engine_add_track(engine);
    expect(cover_track >= 0, "failed to add cover track");
    int small_index = -1;
    int cover_index = -1;
    expect(engine_add_clip_to_track(engine, cover_track, path, one, &small_index), "failed to add covered clip");
    expect(engine_clip_set_region(engine, cover_track, small_index, 0, one), "failed to size covered clip");
    expect(engine_add_clip_to_track(engine, cover_track, path, 0, &cover_index), "failed to add cover clip");
    expect(engine_clip_set_region(engine, cover_track, cover_index, 0, four), "failed to size cover clip");

    tracks = engine_get_tracks(engine);
    track = &tracks[cover_track];
    anchor_index = find_clip_by_start(track, 0);
    expect(anchor_index >= 0, "missing cover anchor");
    anchor = track->clips[anchor_index].sampler;
    expect(engine_track_apply_no_overlap(engine, cover_track, anchor, NULL), "cover overlap resolution failed");

    tracks = engine_get_tracks(engine);
    track = &tracks[cover_track];
    expect(track->clip_count == 1, "cover should remove fully covered clip");
    expect_u64(track->clips[0].timeline_start_frames, 0, "cover start mismatch");
    expect_u64(track->clips[0].duration_frames, four, "cover duration mismatch");

    engine_destroy(engine);
    (void)unlink(path);
}

int main(void) {
    test_100_bpm_frame_mapping();
    test_imported_loop_duration_matches_100_bpm_grid();
    test_engine_cache_reuses_imported_source();
    test_overlap_right_trim_preserves_original_end();
    test_overlap_split_and_cover_contracts();
    printf("timeline_contract_test: success\n");
    return 0;
}
