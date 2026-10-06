#include "input/timeline/timeline_input_keyboard.h"

#include "app/audio_recording.h"
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
    SDL_Keymod mods = (SDL_Keymod)event->key.keysym.mod;

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
        if (key == SDLK_r && event->key.repeat == 0) {
            if (daw_audio_recording_is_active(&state->audio_recording)) {
                DawAudioRecordingResult result;
                (void)daw_audio_recording_finish_timeline_capture(state, &result);
                if (result.inserted) {
                    const EngineTrack* tracks = engine_get_tracks(state->engine);
                    int track_count = engine_get_track_count(state->engine);
                    if (tracks &&
                        result.track_index >= 0 &&
                        result.track_index < track_count &&
                        result.clip_index >= 0 &&
                        result.clip_index < tracks[result.track_index].clip_count) {
                        inspector_input_show(state,
                                             result.track_index,
                                             result.clip_index,
                                             &tracks[result.track_index].clips[result.clip_index]);
                    }
                    effects_panel_sync_from_engine(state);
                }
            } else {
                (void)daw_audio_recording_begin_timeline_capture(state);
            }
            return true;
        }
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
    bool duplicate_trigger = (key == SDLK_d) && (mods & (KMOD_CTRL | KMOD_GUI));
    // A held command key must not create repeated edits or replace the clipboard again.
    if (event->key.repeat && (copy_trigger || paste_trigger || duplicate_trigger)) return true;
    if (copy_trigger) {
        timeline_clipboard_copy(state);
        return true;
    } else if (paste_trigger) {
        timeline_clipboard_paste(state);
        return true;
    }
    if (!duplicate_trigger) {
        return false;
    }

    if (timeline_selection_duplicate(state)) effects_panel_sync_from_engine(state);
    return true;
}
