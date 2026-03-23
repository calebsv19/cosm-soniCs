#include "export/daw_pack_export.h"
#include "app_state.h"
#include "config.h"
#include "core_pack.h"
#include "time/tempo.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DawPackHeaderCanonical {
    uint32_t version;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t samples_per_pixel;
    uint64_t point_count;
    uint64_t start_frame;
    uint64_t end_frame;
    uint64_t project_duration_frames;
} DawPackHeaderCanonical;

static void fail(const char* msg) {
    fprintf(stderr, "daw_pack_contract_parity_test: %s\n", msg);
    exit(1);
}

static void expect(bool cond, const char* msg) {
    if (!cond) {
        fail(msg);
    }
}

static bool chunk_type_exists(const CorePackReader* reader, const char type[4]) {
    CorePackChunkInfo chunk = {0};
    return core_pack_reader_find_chunk(reader, type, 0, &chunk).code == CORE_OK;
}

static void expect_json_contains(const char* json, const char* needle) {
    expect(json != NULL, "json payload missing");
    if (!strstr(json, needle)) {
        fprintf(stderr, "missing json field: %s\n", needle);
        fail("json contract mismatch");
    }
}

int main(void) {
    const char* pack_path = "/tmp/daw_pack_contract_parity_test.pack";

    AppState state;
    memset(&state, 0, sizeof(state));

    config_set_defaults(&state.runtime_cfg);
    state.runtime_cfg.sample_rate = 48000;

    state.tempo = tempo_state_default((double)state.runtime_cfg.sample_rate);
    tempo_map_init(&state.tempo_map, (double)state.runtime_cfg.sample_rate);
    time_signature_map_init(&state.time_signature_map);

    state.timeline_visible_seconds = 10.0f;
    state.timeline_window_start_seconds = 0.0f;
    state.timeline_vertical_scale = 1.0f;
    state.timeline_follow_mode = TIMELINE_FOLLOW_JUMP;

    float samples[] = {
        -1.0f, -0.9f,
        -0.2f, -0.1f,
         0.2f,  0.3f,
         0.8f,  1.0f,
    };
    EngineBounceBuffer bounce = {
        .data = samples,
        .frame_count = 4,
        .channels = 2,
        .sample_rate = 48000,
    };

    expect(daw_pack_export_from_bounce(pack_path,
                                       &state,
                                       &bounce,
                                       0,
                                       bounce.frame_count,
                                       bounce.frame_count),
           "daw_pack_export_from_bounce failed");

    CorePackReader reader = {0};
    expect(core_pack_reader_open(pack_path, &reader).code == CORE_OK, "failed to open pack");

    expect(chunk_type_exists(&reader, "DAWH"), "missing DAWH chunk");
    expect(chunk_type_exists(&reader, "WMIN"), "missing WMIN chunk");
    expect(chunk_type_exists(&reader, "WMAX"), "missing WMAX chunk");
    expect(chunk_type_exists(&reader, "MRKS"), "missing MRKS chunk");
    expect(chunk_type_exists(&reader, "JSON"), "missing JSON chunk");

    CorePackChunkInfo dawh = {0};
    expect(core_pack_reader_find_chunk(&reader, "DAWH", 0, &dawh).code == CORE_OK, "find DAWH failed");
    expect(dawh.size == sizeof(DawPackHeaderCanonical), "unexpected DAWH size");

    DawPackHeaderCanonical header = {0};
    expect(core_pack_reader_read_chunk_data(&reader, &dawh, &header, sizeof(header)).code == CORE_OK,
           "read DAWH failed");
    expect(header.version == 1u, "unexpected DAWH version");
    expect(header.sample_rate == 48000u, "unexpected DAWH sample_rate");
    expect(header.channels == 2u, "unexpected DAWH channels");

    CorePackChunkInfo json_chunk = {0};
    expect(core_pack_reader_find_chunk(&reader, "JSON", 0, &json_chunk).code == CORE_OK,
           "find JSON failed");
    expect(json_chunk.size > 0, "empty JSON chunk");

    char* json = (char*)calloc((size_t)json_chunk.size + 1u, 1u);
    expect(json != NULL, "json alloc failed");
    expect(core_pack_reader_read_chunk_data(&reader, &json_chunk, json, json_chunk.size).code == CORE_OK,
           "read JSON failed");
    json[json_chunk.size] = '\0';

    expect_json_contains(json, "\"core_dataset\"");
    expect_json_contains(json, "\"profile\":\"daw_session_dataset_v1\"");
    expect_json_contains(json, "\"dataset_schema\":\"daw.session_snapshot\"");
    expect_json_contains(json, "\"schema_family\":\"daw.session\"");
    expect_json_contains(json, "\"schema_variant\":\"snapshot.v1\"");
    expect_json_contains(json, "\"dataset_contract_version\":1");
    expect_json_contains(json, "\"session_document_version\"");
    expect_json_contains(json, "\"name\":\"daw_timeline_v1\"");
    expect_json_contains(json, "\"name\":\"daw_selection_v1\"");

    free(json);
    expect(core_pack_reader_close(&reader).code == CORE_OK, "close pack reader failed");

    tempo_map_free(&state.tempo_map);
    time_signature_map_free(&state.time_signature_map);
    remove(pack_path);

    puts("daw_pack_contract_parity_test: success");
    return 0;
}
