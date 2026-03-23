#include "export/daw_pack_export.h"

#include "app_state.h"
#include "core_data.h"
#include "core_pack.h"
#include "session.h"
#include "time/tempo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define DAW_PACK_VERSION 1u
#define DAW_PACK_SAMPLES_PER_PIXEL 256u

// Clamps a float sample to waveform visualization range.
static float clamp_unit(float v) {
    if (v < -1.0f) {
        return -1.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

// Converts a beat value into a non-negative frame index using the tempo map.
static uint64_t beat_to_frame(const TempoMap* tempo_map, double beat) {
    if (!tempo_map) {
        return 0;
    }
    double sample_pos = tempo_map_beats_to_samples(tempo_map, beat);
    if (!(sample_pos > 0.0)) {
        return 0;
    }
    return (uint64_t)llround(sample_pos);
}

// Appends one marker record to the contiguous marker array.
static bool push_marker(DawPackMarker* markers, size_t marker_capacity, size_t* marker_count, const DawPackMarker* marker) {
    if (!markers || !marker_count || !marker || *marker_count >= marker_capacity) {
        return false;
    }
    markers[*marker_count] = *marker;
    *marker_count += 1;
    return true;
}

// Sorts markers by frame so downstream rendering can stream left-to-right.
static int marker_compare_by_frame(const void* a, const void* b) {
    const DawPackMarker* ma = (const DawPackMarker*)a;
    const DawPackMarker* mb = (const DawPackMarker*)b;
    if (ma->frame < mb->frame) {
        return -1;
    }
    if (ma->frame > mb->frame) {
        return 1;
    }
    return 0;
}

static const char* table_type_name(CoreTableColumnType type) {
    switch (type) {
        case CORE_TABLE_COL_F32: return "f32";
        case CORE_TABLE_COL_F64: return "f64";
        case CORE_TABLE_COL_I64: return "i64";
        case CORE_TABLE_COL_U32: return "u32";
        case CORE_TABLE_COL_BOOL: return "bool";
        default: return "unknown";
    }
}

static bool append_core_dataset_items_json(cJSON* items, const CoreDataset* dataset) {
    if (!items || !dataset) {
        return false;
    }

    for (size_t i = 0; i < dataset->item_count; ++i) {
        const CoreDataItem* item = &dataset->items[i];
        cJSON* entry = cJSON_CreateObject();
        if (!entry) {
            return false;
        }
        cJSON_AddStringToObject(entry, "name", item->name ? item->name : "unnamed");

        if (item->kind == CORE_DATA_TABLE_TYPED) {
            cJSON* cols = cJSON_CreateArray();
            cJSON* row0 = cJSON_CreateObject();
            if (!cols || !row0) {
                cJSON_Delete(entry);
                cJSON_Delete(cols);
                cJSON_Delete(row0);
                return false;
            }

            cJSON_AddStringToObject(entry, "kind", "table_typed");
            cJSON_AddNumberToObject(entry, "rows", item->as.table_typed.row_count);
            cJSON_AddNumberToObject(entry, "columns", item->as.table_typed.column_count);

            for (uint32_t c = 0; c < item->as.table_typed.column_count; ++c) {
                const CoreTableColumnTyped* col = &item->as.table_typed.columns[c];
                cJSON* cdesc = cJSON_CreateObject();
                if (!cdesc) {
                    cJSON_Delete(entry);
                    cJSON_Delete(cols);
                    cJSON_Delete(row0);
                    return false;
                }
                cJSON_AddStringToObject(cdesc, "name", col->name ? col->name : "col");
                cJSON_AddStringToObject(cdesc, "type", table_type_name(col->type));
                cJSON_AddItemToArray(cols, cdesc);

                if (item->as.table_typed.row_count > 0 && col->name) {
                    switch (col->type) {
                        case CORE_TABLE_COL_F32:
                            cJSON_AddNumberToObject(row0, col->name, (double)col->as.f32_values[0]);
                            break;
                        case CORE_TABLE_COL_F64:
                            cJSON_AddNumberToObject(row0, col->name, col->as.f64_values[0]);
                            break;
                        case CORE_TABLE_COL_I64:
                            cJSON_AddNumberToObject(row0, col->name, (double)col->as.i64_values[0]);
                            break;
                        case CORE_TABLE_COL_U32:
                            cJSON_AddNumberToObject(row0, col->name, (double)col->as.u32_values[0]);
                            break;
                        case CORE_TABLE_COL_BOOL:
                            cJSON_AddBoolToObject(row0, col->name, col->as.bool_values[0] ? 1 : 0);
                            break;
                        default:
                            break;
                    }
                }
            }

            cJSON_AddItemToObject(entry, "schema", cols);
            cJSON_AddItemToObject(entry, "row0", row0);
        } else if (item->kind == CORE_DATA_SCALAR_F64) {
            cJSON_AddStringToObject(entry, "kind", "scalar_f64");
            cJSON_AddNumberToObject(entry, "value", item->as.scalar_f64);
        } else {
            cJSON_AddStringToObject(entry, "kind", "unsupported");
        }

        cJSON_AddItemToArray(items, entry);
    }

    return true;
}

static cJSON* build_core_dataset_snapshot_json(const AppState* state) {
    SessionDocument doc;
    CoreDataset dataset;
    cJSON* root = NULL;
    cJSON* metadata = NULL;
    cJSON* items = NULL;
    CoreResult r;
    uint32_t clip_count = 0u;
    uint32_t lane_count = 0u;
    uint32_t point_count = 0u;

    if (!state) {
        return NULL;
    }

    session_document_init(&doc);
    if (!session_document_capture(state, &doc)) {
        session_document_free(&doc);
        return NULL;
    }

    core_dataset_init(&dataset);
    r = core_dataset_add_metadata_string(&dataset, "profile", "daw_session_dataset_v1");
    if (r.code != CORE_OK) goto fail;
    r = core_dataset_add_metadata_string(&dataset, "dataset_schema", "daw.session_snapshot");
    if (r.code != CORE_OK) goto fail;
    r = core_dataset_add_metadata_string(&dataset, "schema_family", "daw.session");
    if (r.code != CORE_OK) goto fail;
    r = core_dataset_add_metadata_string(&dataset, "schema_variant", "snapshot.v1");
    if (r.code != CORE_OK) goto fail;
    r = core_dataset_add_metadata_i64(&dataset, "dataset_contract_version", 1);
    if (r.code != CORE_OK) goto fail;
    r = core_dataset_add_metadata_i64(&dataset, "session_document_version", doc.version);
    if (r.code != CORE_OK) goto fail;

    for (int t = 0; t < doc.track_count; ++t) {
        const SessionTrack* track = &doc.tracks[t];
        if (!track) continue;
        for (int c = 0; c < track->clip_count; ++c) {
            const SessionClip* clip = &track->clips[c];
            clip_count += 1u;
            if (!clip) continue;
            lane_count += (uint32_t)(clip->automation_lane_count > 0 ? clip->automation_lane_count : 0);
            for (int l = 0; l < clip->automation_lane_count; ++l) {
                const SessionAutomationLane* lane = &clip->automation_lanes[l];
                if (lane && lane->point_count > 0) {
                    point_count += (uint32_t)lane->point_count;
                }
            }
        }
    }

    {
        const char* cols[] = {
            "track_count", "clip_count", "automation_lane_count", "automation_point_count",
            "tempo_event_count", "time_signature_event_count",
            "transport_playing", "transport_frame",
            "loop_enabled", "loop_start_frame", "loop_end_frame",
            "timeline_visible_seconds", "timeline_window_start_seconds", "timeline_vertical_scale",
            "timeline_view_in_beats", "timeline_follow_mode", "timeline_playhead_frame"
        };
        CoreTableColumnType types[] = {
            CORE_TABLE_COL_U32, CORE_TABLE_COL_U32, CORE_TABLE_COL_U32, CORE_TABLE_COL_U32,
            CORE_TABLE_COL_U32, CORE_TABLE_COL_U32,
            CORE_TABLE_COL_BOOL, CORE_TABLE_COL_I64,
            CORE_TABLE_COL_BOOL, CORE_TABLE_COL_I64, CORE_TABLE_COL_I64,
            CORE_TABLE_COL_F64, CORE_TABLE_COL_F64, CORE_TABLE_COL_F64,
            CORE_TABLE_COL_BOOL, CORE_TABLE_COL_I64, CORE_TABLE_COL_I64
        };
        uint32_t track_count_col[] = {(uint32_t)(doc.track_count > 0 ? doc.track_count : 0)};
        uint32_t clip_count_col[] = {clip_count};
        uint32_t lane_count_col[] = {lane_count};
        uint32_t point_count_col[] = {point_count};
        uint32_t tempo_count_col[] = {(uint32_t)(doc.tempo_event_count > 0 ? doc.tempo_event_count : 0)};
        uint32_t ts_count_col[] = {(uint32_t)(doc.time_signature_event_count > 0 ? doc.time_signature_event_count : 0)};
        bool transport_playing_col[] = {doc.transport_playing};
        int64_t transport_frame_col[] = {(int64_t)doc.transport_frame};
        bool loop_enabled_col[] = {doc.loop.enabled};
        int64_t loop_start_col[] = {(int64_t)doc.loop.start_frame};
        int64_t loop_end_col[] = {(int64_t)doc.loop.end_frame};
        double visible_col[] = {(double)doc.timeline.visible_seconds};
        double window_start_col[] = {(double)doc.timeline.window_start_seconds};
        double vertical_col[] = {(double)doc.timeline.vertical_scale};
        bool view_in_beats_col[] = {doc.timeline.view_in_beats};
        int64_t follow_col[] = {(int64_t)doc.timeline.follow_mode};
        int64_t playhead_col[] = {(int64_t)doc.timeline.playhead_frame};
        const void* data[] = {
            track_count_col, clip_count_col, lane_count_col, point_count_col,
            tempo_count_col, ts_count_col,
            transport_playing_col, transport_frame_col,
            loop_enabled_col, loop_start_col, loop_end_col,
            visible_col, window_start_col, vertical_col,
            view_in_beats_col, follow_col, playhead_col
        };
        r = core_dataset_add_table_typed(&dataset,
                                         "daw_timeline_v1",
                                         cols,
                                         types,
                                         (uint32_t)(sizeof(cols) / sizeof(cols[0])),
                                         1u,
                                         data);
        if (r.code != CORE_OK) goto fail;
    }

    {
        const char* cols[] = {
            "selected_track_index", "selected_clip_index", "library_selected_index"
        };
        CoreTableColumnType types[] = {
            CORE_TABLE_COL_I64, CORE_TABLE_COL_I64, CORE_TABLE_COL_I64
        };
        int64_t selected_track_col[] = {(int64_t)doc.selected_track_index};
        int64_t selected_clip_col[] = {(int64_t)doc.selected_clip_index};
        int64_t library_selected_col[] = {(int64_t)doc.library.selected_index};
        const void* data[] = {
            selected_track_col, selected_clip_col, library_selected_col
        };
        r = core_dataset_add_table_typed(&dataset,
                                         "daw_selection_v1",
                                         cols,
                                         types,
                                         (uint32_t)(sizeof(cols) / sizeof(cols[0])),
                                         1u,
                                         data);
        if (r.code != CORE_OK) goto fail;
    }

    if (clip_count > 0u) {
        int64_t* track_index_col = (int64_t*)calloc(clip_count, sizeof(int64_t));
        int64_t* clip_index_col = (int64_t*)calloc(clip_count, sizeof(int64_t));
        int64_t* start_col = (int64_t*)calloc(clip_count, sizeof(int64_t));
        int64_t* duration_col = (int64_t*)calloc(clip_count, sizeof(int64_t));
        int64_t* offset_col = (int64_t*)calloc(clip_count, sizeof(int64_t));
        int64_t* fade_in_col = (int64_t*)calloc(clip_count, sizeof(int64_t));
        int64_t* fade_out_col = (int64_t*)calloc(clip_count, sizeof(int64_t));
        double* gain_col = (double*)calloc(clip_count, sizeof(double));
        bool* selected_col = (bool*)calloc(clip_count, sizeof(bool));
        uint32_t* clip_lane_count_col = (uint32_t*)calloc(clip_count, sizeof(uint32_t));
        uint32_t row = 0u;
        if (!track_index_col || !clip_index_col || !start_col || !duration_col || !offset_col ||
            !fade_in_col || !fade_out_col || !gain_col || !selected_col || !clip_lane_count_col) {
            free(track_index_col); free(clip_index_col); free(start_col); free(duration_col); free(offset_col);
            free(fade_in_col); free(fade_out_col); free(gain_col); free(selected_col); free(clip_lane_count_col);
            goto fail;
        }

        for (int t = 0; t < doc.track_count; ++t) {
            const SessionTrack* track = &doc.tracks[t];
            for (int c = 0; c < track->clip_count; ++c) {
                const SessionClip* clip = &track->clips[c];
                track_index_col[row] = (int64_t)t;
                clip_index_col[row] = (int64_t)c;
                start_col[row] = (int64_t)clip->start_frame;
                duration_col[row] = (int64_t)clip->duration_frames;
                offset_col[row] = (int64_t)clip->offset_frames;
                fade_in_col[row] = (int64_t)clip->fade_in_frames;
                fade_out_col[row] = (int64_t)clip->fade_out_frames;
                gain_col[row] = (double)clip->gain;
                selected_col[row] = clip->selected;
                clip_lane_count_col[row] = (uint32_t)(clip->automation_lane_count > 0 ? clip->automation_lane_count : 0);
                row += 1u;
            }
        }

        {
            const char* cols[] = {
                "track_index", "clip_index",
                "start_frame", "duration_frames", "offset_frames",
                "fade_in_frames", "fade_out_frames",
                "gain", "selected", "automation_lane_count"
            };
            CoreTableColumnType types[] = {
                CORE_TABLE_COL_I64, CORE_TABLE_COL_I64,
                CORE_TABLE_COL_I64, CORE_TABLE_COL_I64, CORE_TABLE_COL_I64,
                CORE_TABLE_COL_I64, CORE_TABLE_COL_I64,
                CORE_TABLE_COL_F64, CORE_TABLE_COL_BOOL, CORE_TABLE_COL_U32
            };
            const void* data[] = {
                track_index_col, clip_index_col,
                start_col, duration_col, offset_col,
                fade_in_col, fade_out_col,
                gain_col, selected_col, clip_lane_count_col
            };
            r = core_dataset_add_table_typed(&dataset,
                                             "daw_clip_summary_v1",
                                             cols,
                                             types,
                                             (uint32_t)(sizeof(cols) / sizeof(cols[0])),
                                             clip_count,
                                             data);
        }

        free(track_index_col); free(clip_index_col); free(start_col); free(duration_col); free(offset_col);
        free(fade_in_col); free(fade_out_col); free(gain_col); free(selected_col); free(clip_lane_count_col);
        if (r.code != CORE_OK) goto fail;
    }

    if (lane_count > 0u) {
        int64_t* track_index_col = (int64_t*)calloc(lane_count, sizeof(int64_t));
        int64_t* clip_index_col = (int64_t*)calloc(lane_count, sizeof(int64_t));
        int64_t* lane_index_col = (int64_t*)calloc(lane_count, sizeof(int64_t));
        int64_t* target_col = (int64_t*)calloc(lane_count, sizeof(int64_t));
        uint32_t* point_count_col = (uint32_t*)calloc(lane_count, sizeof(uint32_t));
        int64_t* first_frame_col = (int64_t*)calloc(lane_count, sizeof(int64_t));
        int64_t* last_frame_col = (int64_t*)calloc(lane_count, sizeof(int64_t));
        double* min_value_col = (double*)calloc(lane_count, sizeof(double));
        double* max_value_col = (double*)calloc(lane_count, sizeof(double));
        uint32_t row = 0u;
        if (!track_index_col || !clip_index_col || !lane_index_col || !target_col || !point_count_col ||
            !first_frame_col || !last_frame_col || !min_value_col || !max_value_col) {
            free(track_index_col); free(clip_index_col); free(lane_index_col); free(target_col); free(point_count_col);
            free(first_frame_col); free(last_frame_col); free(min_value_col); free(max_value_col);
            goto fail;
        }

        for (int t = 0; t < doc.track_count; ++t) {
            const SessionTrack* track = &doc.tracks[t];
            for (int c = 0; c < track->clip_count; ++c) {
                const SessionClip* clip = &track->clips[c];
                for (int l = 0; l < clip->automation_lane_count; ++l) {
                    const SessionAutomationLane* lane = &clip->automation_lanes[l];
                    double min_v = 0.0;
                    double max_v = 0.0;
                    int64_t first = 0;
                    int64_t last = 0;
                    if (lane->point_count > 0 && lane->points) {
                        min_v = lane->points[0].value;
                        max_v = lane->points[0].value;
                        first = (int64_t)lane->points[0].frame;
                        last = (int64_t)lane->points[lane->point_count - 1].frame;
                        for (int p = 1; p < lane->point_count; ++p) {
                            double v = (double)lane->points[p].value;
                            if (v < min_v) min_v = v;
                            if (v > max_v) max_v = v;
                        }
                    }

                    track_index_col[row] = (int64_t)t;
                    clip_index_col[row] = (int64_t)c;
                    lane_index_col[row] = (int64_t)l;
                    target_col[row] = (int64_t)lane->target;
                    point_count_col[row] = (uint32_t)(lane->point_count > 0 ? lane->point_count : 0);
                    first_frame_col[row] = first;
                    last_frame_col[row] = last;
                    min_value_col[row] = min_v;
                    max_value_col[row] = max_v;
                    row += 1u;
                }
            }
        }

        {
            const char* cols[] = {
                "track_index", "clip_index", "lane_index", "target",
                "point_count", "first_frame", "last_frame",
                "min_value", "max_value"
            };
            CoreTableColumnType types[] = {
                CORE_TABLE_COL_I64, CORE_TABLE_COL_I64, CORE_TABLE_COL_I64, CORE_TABLE_COL_I64,
                CORE_TABLE_COL_U32, CORE_TABLE_COL_I64, CORE_TABLE_COL_I64,
                CORE_TABLE_COL_F64, CORE_TABLE_COL_F64
            };
            const void* data[] = {
                track_index_col, clip_index_col, lane_index_col, target_col,
                point_count_col, first_frame_col, last_frame_col,
                min_value_col, max_value_col
            };
            r = core_dataset_add_table_typed(&dataset,
                                             "daw_automation_summary_v1",
                                             cols,
                                             types,
                                             (uint32_t)(sizeof(cols) / sizeof(cols[0])),
                                             lane_count,
                                             data);
        }

        free(track_index_col); free(clip_index_col); free(lane_index_col); free(target_col); free(point_count_col);
        free(first_frame_col); free(last_frame_col); free(min_value_col); free(max_value_col);
        if (r.code != CORE_OK) goto fail;
    }

    root = cJSON_CreateObject();
    metadata = cJSON_CreateObject();
    items = cJSON_CreateArray();
    if (!root || !metadata || !items) {
        cJSON_Delete(root);
        cJSON_Delete(metadata);
        cJSON_Delete(items);
        goto fail;
    }

    {
        const CoreMetadataItem* profile = core_dataset_find_metadata(&dataset, "profile");
        const CoreMetadataItem* schema = core_dataset_find_metadata(&dataset, "dataset_schema");
        const CoreMetadataItem* version = core_dataset_find_metadata(&dataset, "dataset_contract_version");
        cJSON_AddStringToObject(root,
                                "profile",
                                (profile && profile->type == CORE_META_STRING && profile->as.string_value)
                                    ? profile->as.string_value
                                    : "daw_session_dataset_v1");
        cJSON_AddStringToObject(root,
                                "dataset_schema",
                                (schema && schema->type == CORE_META_STRING && schema->as.string_value)
                                    ? schema->as.string_value
                                    : "daw.session_snapshot");
        cJSON_AddNumberToObject(root,
                                "dataset_contract_version",
                                (version && version->type == CORE_META_I64) ? (double)version->as.i64_value : 1.0);
    }

    for (size_t i = 0; i < dataset.metadata_count; ++i) {
        const CoreMetadataItem* m = &dataset.metadata[i];
        if (!m->key) continue;
        switch (m->type) {
            case CORE_META_STRING:
                cJSON_AddStringToObject(metadata, m->key, m->as.string_value ? m->as.string_value : "");
                break;
            case CORE_META_F64:
                cJSON_AddNumberToObject(metadata, m->key, m->as.f64_value);
                break;
            case CORE_META_I64:
                cJSON_AddNumberToObject(metadata, m->key, (double)m->as.i64_value);
                break;
            case CORE_META_BOOL:
                cJSON_AddBoolToObject(metadata, m->key, m->as.bool_value ? 1 : 0);
                break;
            default:
                break;
        }
    }
    cJSON_AddItemToObject(root, "metadata", metadata);

    if (!append_core_dataset_items_json(items, &dataset)) {
        cJSON_Delete(root);
        root = NULL;
        goto fail;
    }
    cJSON_AddItemToObject(root, "items", items);

    core_dataset_free(&dataset);
    session_document_free(&doc);
    return root;

fail:
    core_dataset_free(&dataset);
    session_document_free(&doc);
    cJSON_Delete(root);
    return NULL;
}

// Builds min/max waveform envelopes from interleaved bounced samples.
static bool build_waveform_envelope(const EngineBounceBuffer* bounce,
                                    uint32_t samples_per_pixel,
                                    float** out_mins,
                                    float** out_maxs,
                                    uint64_t* out_point_count) {
    if (!bounce || !bounce->data || bounce->frame_count == 0 || bounce->channels <= 0 ||
        !out_mins || !out_maxs || !out_point_count || samples_per_pixel == 0) {
        return false;
    }

    uint64_t point_count = (bounce->frame_count + (uint64_t)samples_per_pixel - 1u) / (uint64_t)samples_per_pixel;
    if (point_count == 0) {
        point_count = 1;
    }

    float* mins = (float*)calloc((size_t)point_count, sizeof(float));
    float* maxs = (float*)calloc((size_t)point_count, sizeof(float));
    if (!mins || !maxs) {
        free(mins);
        free(maxs);
        return false;
    }

    for (uint64_t b = 0; b < point_count; ++b) {
        uint64_t start = b * (uint64_t)samples_per_pixel;
        uint64_t end = start + (uint64_t)samples_per_pixel;
        if (end > bounce->frame_count) {
            end = bounce->frame_count;
        }

        float min_v = 1.0f;
        float max_v = -1.0f;
        if (start >= end) {
            min_v = 0.0f;
            max_v = 0.0f;
        } else {
            for (uint64_t frame = start; frame < end; ++frame) {
                uint64_t base = frame * (uint64_t)bounce->channels;
                float sum = 0.0f;
                for (int ch = 0; ch < bounce->channels; ++ch) {
                    sum += bounce->data[base + (uint64_t)ch];
                }
                float mono = sum / (float)bounce->channels;
                if (mono < min_v) {
                    min_v = mono;
                }
                if (mono > max_v) {
                    max_v = mono;
                }
            }
        }
        mins[b] = clamp_unit(min_v);
        maxs[b] = clamp_unit(max_v);
    }

    *out_mins = mins;
    *out_maxs = maxs;
    *out_point_count = point_count;
    return true;
}

// Builds tempo/time-signature/loop/playhead markers from current app state.
static bool build_markers(const AppState* state, DawPackMarker** out_markers, size_t* out_marker_count) {
    if (!state || !out_markers || !out_marker_count) {
        return false;
    }

    size_t cap = (size_t)(state->tempo_map.event_count > 0 ? state->tempo_map.event_count : 0) +
                 (size_t)(state->time_signature_map.event_count > 0 ? state->time_signature_map.event_count : 0) +
                 3u;
    DawPackMarker* markers = (DawPackMarker*)calloc(cap > 0 ? cap : 1u, sizeof(DawPackMarker));
    if (!markers) {
        return false;
    }

    size_t count = 0;
    for (int i = 0; i < state->tempo_map.event_count; ++i) {
        const TempoEvent* ev = &state->tempo_map.events[i];
        DawPackMarker m = {0};
        m.frame = beat_to_frame(&state->tempo_map, ev->beat);
        m.beat = ev->beat;
        m.kind = (uint32_t)DAW_PACK_MARKER_TEMPO;
        m.value_a = ev->bpm;
        if (!push_marker(markers, cap, &count, &m)) {
            free(markers);
            return false;
        }
    }

    for (int i = 0; i < state->time_signature_map.event_count; ++i) {
        const TimeSignatureEvent* ev = &state->time_signature_map.events[i];
        DawPackMarker m = {0};
        m.frame = beat_to_frame(&state->tempo_map, ev->beat);
        m.beat = ev->beat;
        m.kind = (uint32_t)DAW_PACK_MARKER_TIME_SIGNATURE;
        m.value_a = (double)ev->ts_num;
        m.value_b = (double)ev->ts_den;
        if (!push_marker(markers, cap, &count, &m)) {
            free(markers);
            return false;
        }
    }

    if (state->loop_enabled && state->loop_end_frame > state->loop_start_frame) {
        DawPackMarker loop_start = {0};
        loop_start.frame = state->loop_start_frame;
        loop_start.beat = tempo_map_samples_to_beats(&state->tempo_map, (int64_t)state->loop_start_frame);
        loop_start.kind = (uint32_t)DAW_PACK_MARKER_LOOP_START;
        if (!push_marker(markers, cap, &count, &loop_start)) {
            free(markers);
            return false;
        }

        DawPackMarker loop_end = {0};
        loop_end.frame = state->loop_end_frame;
        loop_end.beat = tempo_map_samples_to_beats(&state->tempo_map, (int64_t)state->loop_end_frame);
        loop_end.kind = (uint32_t)DAW_PACK_MARKER_LOOP_END;
        if (!push_marker(markers, cap, &count, &loop_end)) {
            free(markers);
            return false;
        }
    }

    DawPackMarker playhead = {0};
    playhead.frame = state->engine ? engine_get_transport_frame(state->engine) : 0;
    playhead.beat = tempo_map_samples_to_beats(&state->tempo_map, (int64_t)playhead.frame);
    playhead.kind = (uint32_t)DAW_PACK_MARKER_PLAYHEAD;
    if (!push_marker(markers, cap, &count, &playhead)) {
        free(markers);
        return false;
    }

    qsort(markers, count, sizeof(DawPackMarker), marker_compare_by_frame);
    *out_markers = markers;
    *out_marker_count = count;
    return true;
}

bool daw_pack_path_from_wav(const char* wav_path, char* out_pack_path, size_t out_pack_path_len) {
    if (!wav_path || !out_pack_path || out_pack_path_len == 0) {
        return false;
    }
    size_t in_len = strlen(wav_path);
    const char* ext = strrchr(wav_path, '.');
    if (ext && strcmp(ext, ".wav") == 0) {
        size_t stem_len = (size_t)(ext - wav_path);
        if (stem_len + 5 > out_pack_path_len) {
            return false;
        }
        memcpy(out_pack_path, wav_path, stem_len);
        memcpy(out_pack_path + stem_len, ".pack", 6);
        return true;
    }

    if (in_len + 5 >= out_pack_path_len) {
        return false;
    }
    memcpy(out_pack_path, wav_path, in_len);
    memcpy(out_pack_path + in_len, ".pack", 6);
    return true;
}

bool daw_pack_export_from_bounce(const char* pack_path,
                                 const AppState* state,
                                 const EngineBounceBuffer* bounce,
                                 uint64_t start_frame,
                                 uint64_t end_frame,
                                 uint64_t project_duration_frames) {
    if (!pack_path || !state || !bounce || !bounce->data || bounce->frame_count == 0 || bounce->channels <= 0) {
        return false;
    }

    float* mins = NULL;
    float* maxs = NULL;
    uint64_t point_count = 0;
    if (!build_waveform_envelope(bounce, DAW_PACK_SAMPLES_PER_PIXEL, &mins, &maxs, &point_count)) {
        return false;
    }

    DawPackMarker* markers = NULL;
    size_t marker_count = 0;
    if (!build_markers(state, &markers, &marker_count)) {
        free(mins);
        free(maxs);
        return false;
    }

    DawPackHeader header = {0};
    header.version = DAW_PACK_VERSION;
    header.sample_rate = (uint32_t)bounce->sample_rate;
    header.channels = (uint32_t)bounce->channels;
    header.samples_per_pixel = DAW_PACK_SAMPLES_PER_PIXEL;
    header.point_count = point_count;
    header.start_frame = start_frame;
    header.end_frame = end_frame;
    header.project_duration_frames = project_duration_frames;

    cJSON* root = cJSON_CreateObject();
    cJSON* dataset_root = NULL;
    char* json_text = NULL;
    uint64_t json_size = 0;
    if (!root) {
        free(markers);
        free(mins);
        free(maxs);
        return false;
    }
    cJSON_AddStringToObject(root, "profile", "daw");
    cJSON_AddNumberToObject(root, "version", (double)DAW_PACK_VERSION);
    cJSON_AddNumberToObject(root, "sample_rate", bounce->sample_rate);
    cJSON_AddNumberToObject(root, "channels", bounce->channels);
    cJSON_AddNumberToObject(root, "start_frame", (double)start_frame);
    cJSON_AddNumberToObject(root, "end_frame", (double)end_frame);
    cJSON_AddNumberToObject(root, "point_count", (double)point_count);
    cJSON_AddNumberToObject(root, "marker_count", (double)marker_count);

    dataset_root = build_core_dataset_snapshot_json(state);
    if (dataset_root) {
        cJSON_AddItemToObject(root, "core_dataset", dataset_root);
    }

    json_text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_text) {
        free(markers);
        free(mins);
        free(maxs);
        return false;
    }
    json_size = (uint64_t)strlen(json_text);

    uint64_t envelope_size = point_count * (uint64_t)sizeof(float);
    uint64_t marker_size = (uint64_t)marker_count * (uint64_t)sizeof(DawPackMarker);

    CorePackWriter writer = {0};
    CoreResult r = core_pack_writer_open(pack_path, &writer);
    if (r.code != CORE_OK) {
        free(markers);
        free(mins);
        free(maxs);
        return false;
    }

    bool ok = true;
    r = core_pack_writer_add_chunk(&writer, "DAWH", &header, (uint64_t)sizeof(header));
    if (r.code != CORE_OK) {
        ok = false;
    }
    if (ok) {
        r = core_pack_writer_add_chunk(&writer, "WMIN", mins, envelope_size);
        if (r.code != CORE_OK) {
            ok = false;
        }
    }
    if (ok) {
        r = core_pack_writer_add_chunk(&writer, "WMAX", maxs, envelope_size);
        if (r.code != CORE_OK) {
            ok = false;
        }
    }
    if (ok) {
        r = core_pack_writer_add_chunk(&writer, "MRKS", markers, marker_size);
        if (r.code != CORE_OK) {
            ok = false;
        }
    }
    if (ok) {
        r = core_pack_writer_add_chunk(&writer, "JSON", json_text, json_size);
        if (r.code != CORE_OK) {
            ok = false;
        }
    }

    CoreResult close_r = core_pack_writer_close(&writer);
    if (close_r.code != CORE_OK) {
        ok = false;
    }

    free(markers);
    free(mins);
    free(maxs);
    free(json_text);
    return ok;
}
