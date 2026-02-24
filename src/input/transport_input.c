#include "input/transport_input.h"

#include "app_state.h"
#include "input/input_manager.h"
#include "ui/transport.h"
#include "ui/timeline_view.h"
#include "ui/layout.h"
#include "input/timeline/timeline_geometry.h"
#include "ui/effects_panel.h"
#include "session/project_manager.h"
#include "effects/param_utils.h"
#include "time/tempo.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static float clamp_scalar(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static uint64_t transport_total_frames(const AppState* state) {
    if (!state || !state->engine) {
        return 0;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t max_frames = 0;
    if (!tracks || track_count <= 0) {
        return 0;
    }
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) continue;
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) continue;
            uint64_t start = clip->timeline_start_frames;
            uint64_t length = clip->duration_frames;
            if (length == 0) {
                length = engine_clip_get_total_frames(state->engine, t, i);
            }
            uint64_t end = start + length;
            if (end > max_frames) {
                max_frames = end;
            }
        }
    }
    return max_frames;
}

static float transport_total_seconds(const AppState* state) {
    const EngineRuntimeConfig* cfg = state && state->engine ? engine_get_config(state->engine) : NULL;
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return 0.0f;
    }
    uint64_t frames = transport_total_frames(state);
    if (frames == 0) {
        return 0.0f;
    }
    return (float)frames / (float)sample_rate;
}

static float timeline_window_max_start(const AppState* state) {
    if (!state) {
        return 0.0f;
    }
    float visible = clamp_scalar(state->timeline_visible_seconds,
                                 TIMELINE_MIN_VISIBLE_SECONDS,
                                 TIMELINE_MAX_VISIBLE_SECONDS);
    float total_seconds = transport_total_seconds(state);
    if (total_seconds > visible) {
        return total_seconds - visible;
    }
    return 0.0f;
}

static void clamp_timeline_window(AppState* state) {
    if (!state) {
        return;
    }
    float visible = clamp_scalar(state->timeline_visible_seconds,
                                 TIMELINE_MIN_VISIBLE_SECONDS,
                                 TIMELINE_MAX_VISIBLE_SECONDS);
    state->timeline_visible_seconds = visible;
    float min_start = 0.0f;
    float max_start = 0.0f;
    timeline_get_scroll_bounds(state, visible, &min_start, &max_start);
    state->timeline_window_start_seconds = clamp_scalar(state->timeline_window_start_seconds, min_start, max_start);
}

static float playhead_seconds(const AppState* state) {
    if (!state || !state->engine) {
        return 0.0f;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
    if (sample_rate <= 0) {
        return 0.0f;
    }
    uint64_t frame = engine_get_transport_frame(state->engine);
    return (float)((double)frame / (double)sample_rate);
}

// Returns the transport playhead position in beats using the tempo map.
static double playhead_beats(const AppState* state) {
    if (!state || !state->engine) {
        return 0.0;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
    if (sample_rate <= 0) {
        return 0.0;
    }
    uint64_t frame = engine_get_transport_frame(state->engine);
    double seconds = (double)frame / (double)sample_rate;
    return tempo_map_seconds_to_beats(&state->tempo_map, seconds);
}

// Syncs the legacy tempo state to the active tempo/time signature at the playhead.
static void sync_tempo_state_to_playhead(AppState* state) {
    if (!state) {
        return;
    }
    double beats = playhead_beats(state);
    const TempoEvent* tempo_evt = tempo_map_event_at_beat(&state->tempo_map, beats);
    const TimeSignatureEvent* sig_evt = time_signature_map_event_at_beat(&state->time_signature_map, beats);
    if (tempo_evt) {
        state->tempo.bpm = tempo_evt->bpm;
    }
    if (sig_evt) {
        state->tempo.ts_num = sig_evt->ts_num;
        state->tempo.ts_den = sig_evt->ts_den;
    }
    const EngineRuntimeConfig* cfg = state->engine ? engine_get_config(state->engine) : NULL;
    state->tempo.sample_rate = cfg ? cfg->sample_rate : state->runtime_cfg.sample_rate;
    tempo_state_clamp(&state->tempo);
    if (state->engine) {
        engine_set_tempo_state(state->engine, &state->tempo);
    }
}

static void zoom_keep_playhead(AppState* state, float old_visible, float new_visible) {
    if (!state) return;
    if (new_visible < TIMELINE_MIN_VISIBLE_SECONDS) new_visible = TIMELINE_MIN_VISIBLE_SECONDS;
    if (new_visible > TIMELINE_MAX_VISIBLE_SECONDS) new_visible = TIMELINE_MAX_VISIBLE_SECONDS;
    float ph_sec = playhead_seconds(state);
    float ratio = 0.0f;
    if (old_visible > 1e-6f) {
        ratio = (ph_sec - state->timeline_window_start_seconds) / old_visible;
    }
    state->timeline_visible_seconds = new_visible;
    float new_start = ph_sec - ratio * new_visible;
    state->timeline_window_start_seconds = new_start;
    clamp_timeline_window(state);
}

static float snap_window_start(const AppState* state, float seconds, float max_start) {
    if (!state || !state->timeline_view_in_beats) {
        if (seconds < 0.0f) seconds = 0.0f;
        if (seconds > max_start) seconds = max_start;
        return seconds;
    }
    if (state->tempo_map.event_count <= 0) {
        if (seconds < 0.0f) seconds = 0.0f;
        if (seconds > max_start) seconds = max_start;
        return seconds;
    }
    double start_beats = tempo_map_seconds_to_beats(&state->tempo_map, (double)seconds);
    double end_beats =
        tempo_map_seconds_to_beats(&state->tempo_map, (double)seconds + (double)state->timeline_visible_seconds);
    double visible_beats = end_beats - start_beats;
    double subdivision = 1.0;
    if (visible_beats <= 2.0) {
        subdivision = 1.0 / 16.0;
    } else if (visible_beats <= 4.0) {
        subdivision = 1.0 / 8.0;
    } else if (visible_beats <= 8.0) {
        subdivision = 1.0 / 4.0;
    } else if (visible_beats <= 16.0) {
        subdivision = 1.0 / 2.0;
    } else {
        subdivision = 1.0;
    }
    double snapped_beats = floor(start_beats / subdivision + 0.5) * subdivision;
    double snapped_sec = tempo_map_beats_to_seconds(&state->tempo_map, snapped_beats);
    if (snapped_sec < 0.0) snapped_sec = 0.0;
    if (snapped_sec > (double)max_start) snapped_sec = (double)max_start;
    return (float)snapped_sec;
}

static void open_project_prompt(AppState* state) {
    if (!state) return;
    state->project_prompt.active = true;
    state->project_prompt.buffer[0] = '\0';
    state->project_prompt.cursor = 0;
    SDL_StartTextInput();
}

// Clears editing state while keeping the current focus.
static void tempo_finish_edit(AppState* state) {
    if (!state) return;
    if (state->tempo_ui.editing) {
        SDL_StopTextInput();
    }
    state->tempo_ui.editing = false;
    state->tempo_ui.buffer[0] = '\0';
    state->tempo_ui.cursor = 0;
    state->tempo_ui.ts_buffer[0] = '\0';
    state->tempo_ui.ts_cursor = 0;
}

static void tempo_cancel_edit(AppState* state) {
    if (!state) return;
    tempo_finish_edit(state);
    state->tempo_ui.focus = TEMPO_FOCUS_NONE;
    state->tempo_ui.ts_part = TEMPO_TS_PART_NONE;
}

static void tempo_focus(AppState* state, TempoFocus focus, bool editing) {
    if (!state) return;
    tempo_finish_edit(state);
    state->tempo_ui.focus = focus;
    if (focus == TEMPO_FOCUS_BPM) {
        state->tempo_ui.ts_part = TEMPO_TS_PART_NONE;
    }
    if (editing) {
        sync_tempo_state_to_playhead(state);
        state->tempo_ui.editing = true;
        snprintf(state->tempo_ui.buffer, sizeof(state->tempo_ui.buffer), "%.0f", state->tempo.bpm);
        state->tempo_ui.cursor = (int)strlen(state->tempo_ui.buffer);
        SDL_StartTextInput();
    } else {
        state->tempo_ui.editing = false;
        state->tempo_ui.buffer[0] = '\0';
        state->tempo_ui.cursor = 0;
    }
}

static void resync_tempo_synced_fx(AppState* state);

// Focuses a time signature part and optionally begins editing it.
static void tempo_focus_time_signature_part(AppState* state, TempoTSPart part, bool editing) {
    if (!state) {
        return;
    }
    tempo_finish_edit(state);
    state->tempo_ui.focus = TEMPO_FOCUS_TS;
    state->tempo_ui.ts_part = part;
    if (editing) {
        sync_tempo_state_to_playhead(state);
        state->tempo_ui.editing = true;
        int value = (part == TEMPO_TS_PART_DEN) ? state->tempo.ts_den : state->tempo.ts_num;
        snprintf(state->tempo_ui.ts_buffer, sizeof(state->tempo_ui.ts_buffer), "%d", value);
        state->tempo_ui.ts_cursor = (int)strlen(state->tempo_ui.ts_buffer);
        SDL_StartTextInput();
    } else {
        state->tempo_ui.editing = false;
        state->tempo_ui.ts_buffer[0] = '\0';
        state->tempo_ui.ts_cursor = 0;
    }
}

// Returns the active edit buffer and cursor for tempo/time signature edits.
static void tempo_edit_target(AppState* state, char** out_buffer, int** out_cursor, size_t* out_capacity) {
    if (!state || !out_buffer || !out_cursor || !out_capacity) {
        return;
    }
    if (state->tempo_ui.focus == TEMPO_FOCUS_TS) {
        *out_buffer = state->tempo_ui.ts_buffer;
        *out_cursor = &state->tempo_ui.ts_cursor;
        *out_capacity = sizeof(state->tempo_ui.ts_buffer);
    } else {
        *out_buffer = state->tempo_ui.buffer;
        *out_cursor = &state->tempo_ui.cursor;
        *out_capacity = sizeof(state->tempo_ui.buffer);
    }
}

// Applies a tempo change at the playhead position.
static void tempo_apply_at_playhead(AppState* state, double bpm) {
    if (!state) {
        return;
    }
    double beats = playhead_beats(state);
    if (tempo_map_upsert_event(&state->tempo_map, beats, bpm)) {
        sync_tempo_state_to_playhead(state);
        resync_tempo_synced_fx(state);
    }
}

// Applies a time signature change at the playhead position.
static void time_signature_apply_at_playhead(AppState* state, int ts_num, int ts_den) {
    if (!state) {
        return;
    }
    double beats = playhead_beats(state);
    if (time_signature_map_upsert_event(&state->time_signature_map, beats, ts_num, ts_den)) {
        sync_tempo_state_to_playhead(state);
    }
}

// Applies a time signature edit from the current buffer.
// Applies a time signature edit from the current buffer, returning true on success.
static bool time_signature_apply_buffer(AppState* state) {
    if (!state) {
        return false;
    }
    if (state->tempo_ui.focus != TEMPO_FOCUS_TS || state->tempo_ui.ts_part == TEMPO_TS_PART_NONE) {
        return false;
    }
    if (state->tempo_ui.ts_buffer[0] == '\0') {
        return false;
    }
    char* end = NULL;
    long value = strtol(state->tempo_ui.ts_buffer, &end, 10);
    if (!end || *end != '\0' || value <= 0) {
        return false;
    }
    sync_tempo_state_to_playhead(state);
    int ts_num = state->tempo.ts_num;
    int ts_den = state->tempo.ts_den;
    if (state->tempo_ui.ts_part == TEMPO_TS_PART_NUM) {
        ts_num = (int)value;
    } else {
        ts_den = (int)value;
    }
    time_signature_apply_at_playhead(state, ts_num, ts_den);
    return true;
}

static void tempo_apply_buffer(AppState* state) {
    if (!state) return;
    if (!state->tempo_ui.editing) return;
    double val = atof(state->tempo_ui.buffer);
    if (val > 0.0) {
        tempo_apply_at_playhead(state, val);
    }
    tempo_finish_edit(state);
}

static void resync_tempo_synced_fx(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    FxMasterSnapshot master = {0};
    if (engine_fx_master_snapshot(state->engine, &master)) {
        for (int i = 0; i < master.count && i < FX_MASTER_MAX; ++i) {
            const FxMasterInstanceInfo* inst = &master.items[i];
            const EffectParamSpec* specs = NULL;
            uint32_t spec_count = 0;
            engine_fx_registry_get_param_specs(state->engine, inst->type, &specs, &spec_count);
            uint32_t pc = inst->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : inst->param_count;
            for (uint32_t p = 0; p < pc; ++p) {
                FxParamMode mode = inst->param_mode[p];
                if (mode == FX_PARAM_MODE_NATIVE) {
                    continue;
                }
                const EffectParamSpec* spec = (specs && p < spec_count) ? &specs[p] : NULL;
                if (!fx_param_spec_is_syncable(spec)) {
                    continue;
                }
                float beat_value = inst->param_beats[p];
                engine_fx_master_set_param_with_mode(state->engine, inst->id, p, inst->params[p], mode, beat_value);
            }
        }
    }

    int track_count = engine_get_track_count(state->engine);
    for (int t = 0; t < track_count; ++t) {
        FxMasterSnapshot snap = {0};
        if (!engine_fx_track_snapshot(state->engine, t, &snap)) {
            continue;
        }
        for (int i = 0; i < snap.count && i < FX_MASTER_MAX; ++i) {
            const FxMasterInstanceInfo* inst = &snap.items[i];
            const EffectParamSpec* specs = NULL;
            uint32_t spec_count = 0;
            engine_fx_registry_get_param_specs(state->engine, inst->type, &specs, &spec_count);
            uint32_t pc = inst->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : inst->param_count;
            for (uint32_t p = 0; p < pc; ++p) {
                FxParamMode mode = inst->param_mode[p];
                if (mode == FX_PARAM_MODE_NATIVE) {
                    continue;
                }
                const EffectParamSpec* spec = (specs && p < spec_count) ? &specs[p] : NULL;
                if (!fx_param_spec_is_syncable(spec)) {
                    continue;
                }
                float beat_value = inst->param_beats[p];
                engine_fx_track_set_param_with_mode(state->engine, t, inst->id, p, inst->params[p], mode, beat_value);
            }
        }
    }
    effects_panel_sync_from_engine(state);
}

static void transport_seek_to(AppState* state, float t) {
    if (!state || !state->engine) {
        return;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return;
    }
    uint64_t total_frames = transport_total_frames(state);
    if (total_frames == 0) {
        total_frames = (uint64_t)(state->timeline_visible_seconds * (float)sample_rate);
    }
    if (total_frames < 1) {
        total_frames = 1;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint64_t frame = (uint64_t)llroundf(t * (float)total_frames);
    input_manager_reset_meter_history_on_seek(state);
    engine_transport_seek(state->engine, frame);
}

void transport_input_init(InputManager* manager) {
    if (!manager) {
        return;
    }
    manager->prev_horiz_slider_down = false;
    manager->prev_vert_slider_down = false;
}

void transport_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event) {
        return;
    }

    TransportUI* transport = &state->transport_ui;
    transport_ui_sync(transport, state);

    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_Point p = {event->button.x, event->button.y};
            Uint32 now = SDL_GetTicks();
            if (SDL_PointInRect(&p, &transport->bpm_rect)) {
                bool is_double = (now - state->tempo_ui.last_click_ticks) <= 300
                    && state->tempo_ui.last_click_focus == TEMPO_FOCUS_BPM;
                state->tempo_ui.last_click_ticks = now;
                state->tempo_ui.last_click_focus = TEMPO_FOCUS_BPM;
                state->tempo_ui.last_click_ts_part = TEMPO_TS_PART_NONE;
                tempo_focus(state, TEMPO_FOCUS_BPM, is_double);
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->ts_num_rect) || SDL_PointInRect(&p, &transport->ts_den_rect)) {
                TempoTSPart part = SDL_PointInRect(&p, &transport->ts_den_rect)
                                       ? TEMPO_TS_PART_DEN
                                       : TEMPO_TS_PART_NUM;
                bool is_double = (now - state->tempo_ui.last_click_ticks) <= 300
                    && state->tempo_ui.last_click_focus == TEMPO_FOCUS_TS
                    && state->tempo_ui.last_click_ts_part == part;
                state->tempo_ui.last_click_ticks = now;
                state->tempo_ui.last_click_focus = TEMPO_FOCUS_TS;
                state->tempo_ui.last_click_ts_part = part;
                tempo_focus_time_signature_part(state, part, is_double);
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->beat_toggle_rect)) {
                state->timeline_view_in_beats = !state->timeline_view_in_beats;
                break;
            }
            // Clicked elsewhere: drop tempo focus/edit if active.
            if (state->tempo_ui.focus != TEMPO_FOCUS_NONE || state->tempo_ui.editing) {
                tempo_cancel_edit(state);
            }
            if (SDL_PointInRect(&p, &transport->save_rect)) {
                if (state->project.has_name) {
                    project_manager_save(state, state->project.name, true);
                } else {
                    open_project_prompt(state);
                }
                break;
            }
            if (SDL_PointInRect(&p, &transport->load_rect)) {
                bool shift_down = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (shift_down) {
                    if (project_manager_new(state)) {
                        project_manager_post_load(state);
                    }
                } else {
                    state->project_load.active = true;
                    state->project_load.scroll_offset = 0.0f;
                    state->project_load.selected_index = -1;
                    state->project_load.last_click_index = -1;
                    state->project_load.last_click_ticks = 0;
                    int count = 0;
                    project_manager_list(state->project_load.entries, (int)(sizeof(state->project_load.entries) / sizeof(state->project_load.entries[0])), &count);
                    state->project_load.count = count;
                    if (count > 0) {
                        int match = -1;
                        for (int i = 0; i < count; ++i) {
                            if (state->project.path[0] &&
                                strcmp(state->project.path, state->project_load.entries[i].path) == 0) {
                                match = i;
                                break;
                            }
                        }
                        state->project_load.selected_index = match >= 0 ? match : 0;
                    } else {
                        SDL_Log("No project to load (config/projects)");
                    }
                }
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->grid_rect)) {
                state->timeline_show_all_grid_lines = !state->timeline_show_all_grid_lines;
                break;
            }
            if (SDL_PointInRect(&p, &transport->fit_width_rect)) {
                const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
                int sample_rate = cfg ? cfg->sample_rate : 0;
                uint64_t total = transport_total_frames(state);
                float seconds = TIMELINE_DEFAULT_VISIBLE_SECONDS;
                if (sample_rate > 0 && total > 0) {
                    seconds = (float)total / (float)sample_rate;
                    seconds *= 1.05f;
                }
                seconds = clamp_scalar(seconds, TIMELINE_MIN_VISIBLE_SECONDS, TIMELINE_MAX_VISIBLE_SECONDS);
                state->timeline_visible_seconds = seconds;
                clamp_timeline_window(state);
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->fit_height_rect)) {
                const Pane* timeline = ui_layout_get_pane(state, 1);
                int track_count = engine_get_track_count(state->engine);
                if (timeline && track_count > 0) {
                    float header_padding = 20.0f + (float)(track_count - 1) * 12.0f + 24.0f;
                    float available = (float)timeline->rect.h - header_padding;
                    if (available < (float)track_count * 32.0f) {
                        available = (float)track_count * 32.0f;
                    }
                    float per_track = available / (float)track_count;
                    float target_scale = per_track / (float)TIMELINE_BASE_TRACK_HEIGHT;
                    target_scale = clamp_scalar(target_scale, TIMELINE_MIN_VERTICAL_SCALE, TIMELINE_MAX_VERTICAL_SCALE);
                    state->timeline_vertical_scale = target_scale;
                    transport_ui_sync(transport, state);
                }
                break;
            }
            if (SDL_PointInRect(&p, &transport->seek_track_rect) || SDL_PointInRect(&p, &transport->seek_handle_rect)) {
                float t = (float)(p.x - transport->seek_track_rect.x) / (float)transport->seek_track_rect.w;
                transport->adjusting_seek = true;
                transport_seek_to(state, t);
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->window_track_rect) || SDL_PointInRect(&p, &transport->window_handle_rect)) {
                manager->prev_window_slider_down = true;
                transport->adjusting_window = true;
                state->timeline_follow_override = true;
                float t = (float)(p.x - transport->window_track_rect.x) / (float)transport->window_track_rect.w;
                t = clamp_scalar(t, 0.0f, 1.0f);
                float max_start = timeline_window_max_start(state);
                float target = t * max_start;
                state->timeline_window_start_seconds = snap_window_start(state, target, max_start);
                clamp_timeline_window(state);
                transport_ui_sync(transport, state);
                break;
            }
            if (SDL_PointInRect(&p, &transport->horiz_track_rect)) {
                manager->prev_horiz_slider_down = true;
                float t = (float)(p.x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
                t = clamp_scalar(t, 0.0f, 1.0f);
                float old_vis = state->timeline_visible_seconds;
                float new_vis = TIMELINE_MIN_VISIBLE_SECONDS +
                    t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
                zoom_keep_playhead(state, old_vis, new_vis);
                transport_ui_sync(transport, state);
            } else if (SDL_PointInRect(&p, &transport->vert_track_rect)) {
                manager->prev_vert_slider_down = true;
                float t = (float)(p.x - transport->vert_track_rect.x) / (float)transport->vert_track_rect.w;
                t = clamp_scalar(t, 0.0f, 1.0f);
                state->timeline_vertical_scale = TIMELINE_MIN_VERTICAL_SCALE +
                    t * (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
                transport_ui_sync(transport, state);
            }
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            manager->prev_horiz_slider_down = false;
            manager->prev_vert_slider_down = false;
            manager->prev_window_slider_down = false;
            transport->adjusting_seek = false;
            transport->adjusting_window = false;
        }
        break;
    case SDL_MOUSEMOTION:
        if (manager->prev_horiz_slider_down && transport->horiz_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            float old_vis = state->timeline_visible_seconds;
            float new_vis = TIMELINE_MIN_VISIBLE_SECONDS +
                t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
            zoom_keep_playhead(state, old_vis, new_vis);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_window_slider_down && transport->window_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->window_track_rect.x) / (float)transport->window_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            float max_start = timeline_window_max_start(state);
            float target = t * max_start;
            state->timeline_window_start_seconds = snap_window_start(state, target, max_start);
            clamp_timeline_window(state);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_vert_slider_down && transport->vert_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->vert_track_rect.x) / (float)transport->vert_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_vertical_scale = TIMELINE_MIN_VERTICAL_SCALE +
                t * (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
            transport_ui_sync(transport, state);
        }
        if (transport->adjusting_seek && transport->seek_track_rect.w > 0) {
            float t = (float)(event->motion.x - transport->seek_track_rect.x) / (float)transport->seek_track_rect.w;
            transport_seek_to(state, t);
            transport_ui_sync(transport, state);
        }
        break;
    case SDL_TEXTINPUT:
        if (state->tempo_ui.editing) {
            char* buffer = NULL;
            int* cursor = NULL;
            size_t cap = 0;
            tempo_edit_target(state, &buffer, &cursor, &cap);
            if (!buffer || !cursor || cap == 0) {
                break;
            }
            int len = (int)strlen(buffer);
            int cur = *cursor;
            if (cur < 0) cur = 0;
            if (cur > len) cur = len;
            for (const char* p = event->text.text; *p; ++p) {
                if (state->tempo_ui.focus == TEMPO_FOCUS_TS) {
                    if (!isdigit((unsigned char)*p)) {
                        continue;
                    }
                } else if (state->tempo_ui.focus == TEMPO_FOCUS_BPM) {
                    if (!isdigit((unsigned char)*p) && *p != '.') {
                        continue;
                    }
                    if (*p == '.' && strchr(buffer, '.') != NULL) {
                        continue;
                    }
                }
                if ((int)strlen(buffer) >= (int)cap - 1) {
                    break;
                }
                memmove(buffer + cur + 1,
                        buffer + cur,
                        strlen(buffer + cur) + 1);
                buffer[cur] = *p;
                cur++;
            }
            *cursor = cur;
        }
        break;
    case SDL_KEYDOWN:
        if (state->tempo_ui.editing) {
            SDL_Keycode key = event->key.keysym.sym;
            char* buffer = NULL;
            int* cursor = NULL;
            size_t cap = 0;
            tempo_edit_target(state, &buffer, &cursor, &cap);
            if (!buffer || !cursor || cap == 0) {
                break;
            }
            if (key == SDLK_BACKSPACE) {
                int len = (int)strlen(buffer);
                int cur = *cursor;
                if (cur > 0 && len > 0) {
                    memmove(buffer + cur - 1,
                            buffer + cur,
                            (size_t)(len - cur + 1));
                    *cursor = cur - 1;
                }
            } else if (key == SDLK_LEFT) {
                if (*cursor > 0) (*cursor)--;
            } else if (key == SDLK_RIGHT) {
                int len = (int)strlen(buffer);
                if (*cursor < len) (*cursor)++;
            } else if (key == SDLK_ESCAPE) {
                tempo_cancel_edit(state);
            } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (state->tempo_ui.focus == TEMPO_FOCUS_TS) {
                    if (time_signature_apply_buffer(state)) {
                        tempo_finish_edit(state);
                    }
                } else {
                    tempo_apply_buffer(state);
                }
            }
            break;
        }
        if (state->tempo_ui.focus == TEMPO_FOCUS_BPM) {
            SDL_Keycode key = event->key.keysym.sym;
            int step = (SDL_GetModState() & KMOD_SHIFT) ? 5 : 1;
            if (key == SDLK_UP) {
                sync_tempo_state_to_playhead(state);
                tempo_apply_at_playhead(state, state->tempo.bpm + step);
                break;
            } else if (key == SDLK_DOWN) {
                sync_tempo_state_to_playhead(state);
                tempo_apply_at_playhead(state, state->tempo.bpm - step);
                break;
            }
        } else if (state->tempo_ui.focus == TEMPO_FOCUS_TS) {
            SDL_Keycode key = event->key.keysym.sym;
            bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            TempoTSPart part = state->tempo_ui.ts_part;
            if (part == TEMPO_TS_PART_NONE) {
                part = TEMPO_TS_PART_NUM;
            }
            if (shift) {
                part = (part == TEMPO_TS_PART_DEN) ? TEMPO_TS_PART_NUM : TEMPO_TS_PART_DEN;
            }
            sync_tempo_state_to_playhead(state);
            int ts_num = state->tempo.ts_num;
            int ts_den = state->tempo.ts_den;
            if (key == SDLK_UP) {
                if (part == TEMPO_TS_PART_DEN) {
                    ts_den += 1;
                } else {
                    ts_num += 1;
                }
                time_signature_apply_at_playhead(state, ts_num, ts_den);
                break;
            } else if (key == SDLK_DOWN) {
                if (part == TEMPO_TS_PART_DEN) {
                    ts_den = (ts_den > 1) ? (ts_den - 1) : ts_den;
                } else {
                    ts_num = (ts_num > 1) ? (ts_num - 1) : ts_num;
                }
                time_signature_apply_at_playhead(state, ts_num, ts_den);
                break;
            }
        }
        break;
    default:
        break;
    }
}

void transport_input_update(InputManager* manager, AppState* state) {
    if (!manager || !state) {
        return;
    }
    TransportUI* transport = &state->transport_ui;
    transport_ui_sync(transport, state);
    if (manager->prev_horiz_slider_down || manager->prev_vert_slider_down || manager->prev_window_slider_down || transport->adjusting_seek || transport->adjusting_window) {
        int mouse_x, mouse_y;
        Uint32 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
        (void)buttons;
        if (manager->prev_horiz_slider_down && transport->horiz_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->horiz_track_rect.x) / (float)transport->horiz_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_visible_seconds = TIMELINE_MIN_VISIBLE_SECONDS +
                t * (TIMELINE_MAX_VISIBLE_SECONDS - TIMELINE_MIN_VISIBLE_SECONDS);
            clamp_timeline_window(state);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_window_slider_down && transport->window_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->window_track_rect.x) / (float)transport->window_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            float max_start = timeline_window_max_start(state);
            float target = t * max_start;
            state->timeline_window_start_seconds = snap_window_start(state, target, max_start);
            clamp_timeline_window(state);
            transport_ui_sync(transport, state);
        }
        if (manager->prev_vert_slider_down && transport->vert_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->vert_track_rect.x) / (float)transport->vert_track_rect.w;
            t = clamp_scalar(t, 0.0f, 1.0f);
            state->timeline_vertical_scale = TIMELINE_MIN_VERTICAL_SCALE +
                t * (TIMELINE_MAX_VERTICAL_SCALE - TIMELINE_MIN_VERTICAL_SCALE);
            transport_ui_sync(transport, state);
        }
        if (transport->adjusting_seek && transport->seek_track_rect.w > 0) {
            float t = (float)(mouse_x - transport->seek_track_rect.x) / (float)transport->seek_track_rect.w;
            transport_seek_to(state, t);
            transport_ui_sync(transport, state);
        }
    }
}

void transport_input_follow_playhead(InputManager* manager, AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (manager && (manager->prev_window_slider_down || state->transport_ui.adjusting_window)) {
        return;
    }
    if (state->timeline_follow_override) {
        return;
    }
    if (!engine_transport_is_playing(state->engine)) {
        return;
    }
    if (state->timeline_follow_mode != TIMELINE_FOLLOW_JUMP) {
        return;
    }
    float visible = clamp_scalar(state->timeline_visible_seconds,
                                 TIMELINE_MIN_VISIBLE_SECONDS,
                                 TIMELINE_MAX_VISIBLE_SECONDS);
    if (visible <= 0.0f) {
        return;
    }
    float window_start = state->timeline_window_start_seconds;
    float window_end = window_start + visible;
    float ph_sec = playhead_seconds(state);
    if (ph_sec < window_start || ph_sec >= window_end) {
        float new_start = ph_sec;
        float min_start = 0.0f;
        float max_start = 0.0f;
        timeline_get_scroll_bounds(state, visible, &min_start, &max_start);
        state->timeline_window_start_seconds = clamp_scalar(new_start, min_start, max_start);
        clamp_timeline_window(state);
        transport_ui_sync(&state->transport_ui, state);
    }
}
