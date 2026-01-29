#include "input/timeline/timeline_input_keyboard.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "input/inspector_input.h"
#include "input/timeline_input.h"
#include "input/timeline_drag.h"
#include "input/timeline/timeline_clipboard.h"
#include "input/timeline_selection.h"
#include "input/tempo_overlay_input.h"
#include "time/tempo.h"
#include "ui/effects_panel.h"
#include "undo/undo_manager.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEMPO_OVERLAY_MIN_BPM 20.0
#define TEMPO_OVERLAY_MAX_BPM 200.0

static int tempo_overlay_find_event_index(const TempoMap* map, double beat) {
    if (!map || map->event_count <= 0) {
        return -1;
    }
    int best = 0;
    double best_delta = fabs(map->events[0].beat - beat);
    for (int i = 1; i < map->event_count; ++i) {
        double delta = fabs(map->events[i].beat - beat);
        if (delta < best_delta) {
            best_delta = delta;
            best = i;
        }
    }
    return best;
}

static bool tempo_overlay_remove_event(TempoMap* map, int index) {
    if (!map || !map->events || map->event_count <= 1) {
        return false;
    }
    if (index <= 0 || index >= map->event_count) {
        return false;
    }
    int new_count = map->event_count - 1;
    TempoEvent* events = (TempoEvent*)calloc((size_t)new_count, sizeof(TempoEvent));
    if (!events) {
        return false;
    }
    int dst = 0;
    for (int i = 0; i < map->event_count; ++i) {
        if (i == index) {
            continue;
        }
        events[dst++] = map->events[i];
    }
    bool ok = tempo_map_set_events(map, events, new_count);
    free(events);
    return ok;
}

bool timeline_input_keyboard_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event || !state->engine) {
        return false;
    }

    TrackNameEditor* editor = &state->track_name_editor;
    if (event->type == SDL_TEXTINPUT && editor->editing) {
        size_t len = strlen(editor->buffer);
        size_t free_space = sizeof(editor->buffer) - 1 - len;
        if (free_space > 0) {
            size_t incoming = strlen(event->text.text);
            if (incoming > free_space) {
                incoming = free_space;
            }
            int cursor = editor->cursor;
            if (cursor < 0) cursor = 0;
            if (cursor > (int)len) cursor = (int)len;
            // Make room for incoming text at cursor
            memmove(editor->buffer + cursor + incoming, editor->buffer + cursor, len - cursor + 1);
            memcpy(editor->buffer + cursor, event->text.text, incoming);
            editor->cursor = cursor + (int)incoming;
        }
        return true;
    }

    if (event->type != SDL_KEYDOWN) {
        return false;
    }

    SDL_Keycode key = event->key.keysym.sym;
    SDL_Keymod mods = SDL_GetModState();

    if (state->timeline_tempo_overlay_enabled &&
        state->tempo_overlay_ui.event_index >= 0 &&
        (key == SDLK_UP || key == SDLK_DOWN)) {
        int index = state->tempo_overlay_ui.event_index;
        if (index >= 0 && index < state->tempo_map.event_count) {
            double beat = state->tempo_map.events[index].beat;
            double bpm = state->tempo_map.events[index].bpm;
            double step = (mods & KMOD_SHIFT) ? 10.0 : 1.0;
            if (key == SDLK_DOWN) {
                step = -step;
            }
            bpm += step;
            if (bpm < TEMPO_OVERLAY_MIN_BPM) bpm = TEMPO_OVERLAY_MIN_BPM;
            if (bpm > TEMPO_OVERLAY_MAX_BPM) bpm = TEMPO_OVERLAY_MAX_BPM;
            if (tempo_overlay_begin_edit(state)) {
                if (tempo_map_update_event(&state->tempo_map, index, beat, bpm)) {
                    state->tempo_overlay_ui.event_index = tempo_overlay_find_event_index(&state->tempo_map, beat);
                }
                tempo_overlay_commit_edit(state);
                return true;
            }
        }
    }

    if (editor->editing) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            track_name_editor_stop(state, true);
        } else if (key == SDLK_ESCAPE) {
            track_name_editor_stop(state, false);
        } else if (key == SDLK_BACKSPACE) {
            size_t len = strlen(editor->buffer);
            if (len > 0 && editor->cursor > 0) {
                int cur = editor->cursor;
                memmove(editor->buffer + cur - 1, editor->buffer + cur, len - (size_t)cur + 1);
                editor->cursor = cur - 1;
            }
        } else if (key == SDLK_DELETE) {
            size_t len = strlen(editor->buffer);
            int cur = editor->cursor;
            if (len > 0 && cur >= 0 && cur < (int)len) {
                memmove(editor->buffer + cur, editor->buffer + cur + 1, len - (size_t)cur);
            }
        } else if (key == SDLK_LEFT) {
            if (editor->cursor > 0) {
                editor->cursor -= 1;
            }
        } else if (key == SDLK_RIGHT) {
            int len = (int)strlen(editor->buffer);
            if (editor->cursor < len) {
                editor->cursor += 1;
            }
        }
        return true;
    }

    if ((key == SDLK_DELETE || key == SDLK_BACKSPACE) && state->timeline_tempo_overlay_enabled) {
        int index = state->tempo_overlay_ui.event_index;
        if (index > 0 && index < state->tempo_map.event_count) {
            if (tempo_overlay_begin_edit(state)) {
                if (tempo_overlay_remove_event(&state->tempo_map, index)) {
                    int next_index = index - 1;
                    if (next_index < 0) {
                        next_index = 0;
                    }
                    if (next_index >= state->tempo_map.event_count) {
                        next_index = state->tempo_map.event_count - 1;
                    }
                    state->tempo_overlay_ui.event_index = (state->tempo_map.event_count > 0) ? next_index : -1;
                }
                tempo_overlay_commit_edit(state);
            }
            return true;
        }
    }

    if ((key == SDLK_DELETE || key == SDLK_BACKSPACE) && state->timeline_automation_mode) {
        if (state->automation_ui.point_index >= 0) {
            engine_clip_remove_automation_point(state->engine,
                                                state->automation_ui.track_index,
                                                state->automation_ui.clip_index,
                                                state->automation_ui.target,
                                                state->automation_ui.point_index);
            state->automation_ui.point_index = -1;
            return true;
        }
    }

    if ((mods & (KMOD_CTRL | KMOD_GUI | KMOD_ALT)) == 0) {
        if (key == SDLK_a) {
            state->timeline_automation_mode = !state->timeline_automation_mode;
            return true;
        }
        if (key == SDLK_g) {
            state->timeline_snap_enabled = !state->timeline_snap_enabled;
            return true;
        }
    }

    bool copy_trigger = (key == SDLK_c) && (mods & (KMOD_CTRL | KMOD_GUI));
    bool paste_trigger = (key == SDLK_v) && (mods & (KMOD_CTRL | KMOD_GUI));
    if (copy_trigger) {
        timeline_clipboard_copy(state);
        return true;
    } else if (paste_trigger) {
        timeline_clipboard_paste(state);
        return true;
    }
    bool duplicate_trigger = (key == SDLK_d) && (mods & (KMOD_CTRL | KMOD_GUI));
    if (!duplicate_trigger) {
        return false;
    }

    TimelineSelectionEntry originals[TIMELINE_MAX_SELECTION];
    EngineSamplerSource* original_samplers[TIMELINE_MAX_SELECTION];
    int original_count = 0;
    int anchor_original_index = -1;

    const EngineTrack* tracks_snapshot = engine_get_tracks(state->engine);
    int track_count_snapshot = engine_get_track_count(state->engine);

    if (state->selection_count > 0) {
        int count = state->selection_count;
        if (count > TIMELINE_MAX_SELECTION) {
            count = TIMELINE_MAX_SELECTION;
        }
        for (int i = 0; i < count; ++i) {
            TimelineSelectionEntry entry = state->selection[i];
            originals[original_count] = entry;
            EngineSamplerSource* sampler = NULL;
            if (tracks_snapshot && entry.track_index >= 0 && entry.track_index < track_count_snapshot) {
                const EngineTrack* track = &tracks_snapshot[entry.track_index];
                if (track && entry.clip_index >= 0 && entry.clip_index < track->clip_count) {
                    sampler = track->clips[entry.clip_index].sampler;
                }
            }
            original_samplers[original_count] = sampler;
            if (anchor_original_index < 0 &&
                entry.track_index == state->selected_track_index &&
                entry.clip_index == state->selected_clip_index) {
                anchor_original_index = original_count;
            }
            original_count++;
        }
    } else if (state->selected_track_index >= 0 && state->selected_clip_index >= 0) {
        TimelineSelectionEntry entry = {
            .track_index = state->selected_track_index,
            .clip_index = state->selected_clip_index
        };
        originals[0] = entry;
        EngineSamplerSource* sampler = NULL;
        if (tracks_snapshot && entry.track_index >= 0 && entry.track_index < track_count_snapshot) {
            const EngineTrack* track = &tracks_snapshot[entry.track_index];
            if (track && entry.clip_index >= 0 && entry.clip_index < track->clip_count) {
                sampler = track->clips[entry.clip_index].sampler;
            }
        }
        original_samplers[0] = sampler;
        anchor_original_index = 0;
        original_count = 1;
    }

    if (anchor_original_index < 0 && original_count > 0) {
        anchor_original_index = 0;
    }

    if (original_count > 0) {
        const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
        uint64_t offset = cfg ? (uint64_t)cfg->block_size : 0;
        if (!cfg || offset == 0) {
            offset = 0;
        }

        TimelineSelectionEntry new_selection[TIMELINE_MAX_SELECTION];
        int new_count = 0;
        int anchor_new_index = -1;

        for (int i = 0; i < original_count; ++i) {
            EngineSamplerSource* sampler = original_samplers[i];
            if (!sampler) {
                continue;
            }
            int track_index = -1;
            int clip_index = -1;
            if (!timeline_find_clip_by_sampler(state, sampler, &track_index, &clip_index)) {
                continue;
            }

            int duplicate_index = -1;
            if (engine_duplicate_clip(state->engine, track_index, clip_index, offset, &duplicate_index)) {
                if (duplicate_index >= 0 && new_count < TIMELINE_MAX_SELECTION) {
                    const EngineTrack* tracks = engine_get_tracks(state->engine);
                    if (tracks && track_index >= 0 && track_index < engine_get_track_count(state->engine)) {
                        const EngineTrack* track = &tracks[track_index];
                        if (track && duplicate_index < track->clip_count) {
                            const EngineClip* clip = &track->clips[duplicate_index];
                            UndoCommand cmd = {0};
                            cmd.type = UNDO_CMD_CLIP_ADD_REMOVE;
                            cmd.data.clip_add_remove.added = true;
                            cmd.data.clip_add_remove.track_index = track_index;
                            cmd.data.clip_add_remove.sampler = clip->sampler;
                            memset(&cmd.data.clip_add_remove.clip, 0, sizeof(cmd.data.clip_add_remove.clip));
                            const char* media_id = engine_clip_get_media_id(clip);
                            const char* media_path = engine_clip_get_media_path(clip);
                            strncpy(cmd.data.clip_add_remove.clip.media_id, media_id ? media_id : "",
                                    sizeof(cmd.data.clip_add_remove.clip.media_id) - 1);
                            cmd.data.clip_add_remove.clip.media_id[sizeof(cmd.data.clip_add_remove.clip.media_id) - 1] = '\0';
                            strncpy(cmd.data.clip_add_remove.clip.media_path, media_path ? media_path : "",
                                    sizeof(cmd.data.clip_add_remove.clip.media_path) - 1);
                            cmd.data.clip_add_remove.clip.media_path[sizeof(cmd.data.clip_add_remove.clip.media_path) - 1] = '\0';
                            strncpy(cmd.data.clip_add_remove.clip.name, clip->name,
                                    sizeof(cmd.data.clip_add_remove.clip.name) - 1);
                            cmd.data.clip_add_remove.clip.name[sizeof(cmd.data.clip_add_remove.clip.name) - 1] = '\0';
                            cmd.data.clip_add_remove.clip.start_frame = clip->timeline_start_frames;
                            cmd.data.clip_add_remove.clip.duration_frames = clip->duration_frames;
                            cmd.data.clip_add_remove.clip.offset_frames = clip->offset_frames;
                            cmd.data.clip_add_remove.clip.fade_in_frames = clip->fade_in_frames;
                            cmd.data.clip_add_remove.clip.fade_out_frames = clip->fade_out_frames;
                            cmd.data.clip_add_remove.clip.fade_in_curve = clip->fade_in_curve;
                            cmd.data.clip_add_remove.clip.fade_out_curve = clip->fade_out_curve;
                            cmd.data.clip_add_remove.clip.gain = clip->gain;
                            cmd.data.clip_add_remove.clip.selected = false;
                            if (cmd.data.clip_add_remove.clip.duration_frames == 0 && clip->sampler) {
                                cmd.data.clip_add_remove.clip.duration_frames = engine_sampler_get_frame_count(clip->sampler);
                            }
                            undo_manager_push(&state->undo, &cmd);
                        }
                    }
                    new_selection[new_count].track_index = track_index;
                    new_selection[new_count].clip_index = duplicate_index;
                    if (i == anchor_original_index) {
                        anchor_new_index = new_count;
                    }
                    new_count++;
                }
            }
        }

        if (new_count > 0) {
            if (anchor_new_index > 0 && anchor_new_index < new_count) {
                TimelineSelectionEntry tmp = new_selection[0];
                new_selection[0] = new_selection[anchor_new_index];
                new_selection[anchor_new_index] = tmp;
            }

            timeline_selection_clear(state);
            for (int i = 0; i < new_count; ++i) {
                timeline_selection_add(state, new_selection[i].track_index, new_selection[i].clip_index);
            }

            state->active_track_index = new_selection[0].track_index;
            state->selected_track_index = new_selection[0].track_index;
            state->selected_clip_index = new_selection[0].clip_index;
            state->timeline_drop_track_index = new_selection[0].track_index;

            const EngineTrack* updated_tracks = engine_get_tracks(state->engine);
            int updated_count = engine_get_track_count(state->engine);
            if (updated_tracks &&
                new_selection[0].track_index >= 0 &&
                new_selection[0].track_index < updated_count) {
                const EngineTrack* anchor_track = &updated_tracks[new_selection[0].track_index];
                if (anchor_track &&
                    new_selection[0].clip_index >= 0 &&
                    new_selection[0].clip_index < anchor_track->clip_count) {
                    const EngineClip* anchor_clip = &anchor_track->clips[new_selection[0].clip_index];
                    inspector_input_show(state, new_selection[0].track_index, new_selection[0].clip_index, anchor_clip);
                } else {
                    inspector_input_init(state);
                }
            } else {
                inspector_input_init(state);
            }
            effects_panel_sync_from_engine(state);
        }
    }

    return true;
}
