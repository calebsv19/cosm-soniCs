#include "input/inspector_input.h"

#include "app_state.h"
#include "engine.h"
#include "engine/sampler.h"
#include "input/input_manager.h"
#include "ui/clip_inspector.h"

#include <SDL2/SDL.h>
#include <string.h>

#define INSPECTOR_GAIN_MIN 0.0f
#define INSPECTOR_GAIN_MAX 4.0f

static EngineClip* inspector_get_clip_mutable(AppState* state) {
    if (!state || !state->engine) {
        return NULL;
    }
    EngineTrack* tracks = (EngineTrack*)engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || state->inspector.track_index < 0 || state->inspector.track_index >= track_count) {
        return NULL;
    }
    EngineTrack* track = &tracks[state->inspector.track_index];
    if (!track || state->inspector.clip_index < 0 || state->inspector.clip_index >= track->clip_count) {
        return NULL;
    }
    return &track->clips[state->inspector.clip_index];
}

static const EngineClip* inspector_get_clip_const(const AppState* state) {
    if (!state || !state->engine) {
        return NULL;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || state->inspector.track_index < 0 || state->inspector.track_index >= track_count) {
        return NULL;
    }
    const EngineTrack* track = &tracks[state->inspector.track_index];
    if (!track || state->inspector.clip_index < 0 || state->inspector.clip_index >= track->clip_count) {
        return NULL;
    }
    return &track->clips[state->inspector.clip_index];
}

static void inspector_stop_text_input(AppState* state) {
    if (!state) {
        return;
    }
    if (state->inspector.editing_name) {
        SDL_StopTextInput();
    }
    state->inspector.editing_name = false;
}

static void inspector_update_gain(AppState* state, float new_gain) {
    if (!state || !state->engine) {
        return;
    }
    if (new_gain < INSPECTOR_GAIN_MIN) new_gain = INSPECTOR_GAIN_MIN;
    if (new_gain > INSPECTOR_GAIN_MAX) new_gain = INSPECTOR_GAIN_MAX;
    state->inspector.gain = new_gain;
    engine_clip_set_gain(state->engine, state->inspector.track_index, state->inspector.clip_index, new_gain);
}

static void inspector_update_gain_from_mouse(AppState* state, int mouse_x) {
    if (!state) {
        return;
    }
    ClipInspectorLayout layout;
    clip_inspector_compute_layout(state, &layout);
    SDL_Rect track = layout.gain_track_rect;
    if (track.w <= 0) {
        return;
    }
    float t = (float)(mouse_x - track.x) / (float)track.w;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float gain = INSPECTOR_GAIN_MIN + t * (INSPECTOR_GAIN_MAX - INSPECTOR_GAIN_MIN);
    inspector_update_gain(state, gain);
}

void inspector_input_init(AppState* state) {
    if (!state) {
        return;
    }
    state->inspector.visible = false;
    state->inspector.track_index = -1;
    state->inspector.clip_index = -1;
    state->inspector.name[0] = '\0';
    state->inspector.gain = 1.0f;
    state->inspector.editing_name = false;
    state->inspector.name_cursor = 0;
    state->inspector.adjusting_gain = false;
}

void inspector_input_show(AppState* state, int track_index, int clip_index, const EngineClip* clip) {
    if (!state || !clip) {
        return;
    }
    state->inspector.visible = true;
    state->inspector.track_index = track_index;
    state->inspector.clip_index = clip_index;
    strncpy(state->inspector.name, clip->name, sizeof(state->inspector.name) - 1);
    state->inspector.name[sizeof(state->inspector.name) - 1] = '\0';
    state->inspector.gain = clip->gain;
    state->inspector.editing_name = false;
    state->inspector.name_cursor = (int)strlen(state->inspector.name);
    state->inspector.adjusting_gain = false;
    SDL_StopTextInput();
}

void inspector_input_set_clip(AppState* state, int track_index, int clip_index) {
    if (!state) {
        return;
    }
    state->inspector.track_index = track_index;
    state->inspector.clip_index = clip_index;
}

void inspector_input_commit_if_editing(AppState* state) {
    if (!state) {
        return;
    }
    if (state->inspector.editing_name) {
        EngineClip* clip = inspector_get_clip_mutable(state);
        if (clip && state->engine) {
            engine_clip_set_name(state->engine, state->inspector.track_index, state->inspector.clip_index, state->inspector.name);
        }
        inspector_stop_text_input(state);
    }
}

void inspector_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event) {
        return;
    }

    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT && state->inspector.visible) {
            ClipInspectorLayout layout;
            clip_inspector_compute_layout(state, &layout);
            SDL_Point p = {event->button.x, event->button.y};
            if (SDL_PointInRect(&p, &layout.panel_rect)) {
                if (SDL_PointInRect(&p, &layout.name_rect)) {
                    EngineClip* clip = inspector_get_clip_mutable(state);
                    if (clip) {
                        strncpy(state->inspector.name, clip->name, sizeof(state->inspector.name) - 1);
                        state->inspector.name[sizeof(state->inspector.name) - 1] = '\0';
                        state->inspector.name_cursor = (int)strlen(state->inspector.name);
                        state->inspector.editing_name = true;
                        SDL_StartTextInput();
                    }
                } else {
                    inspector_input_commit_if_editing(state);
                }

                if (SDL_PointInRect(&p, &layout.gain_track_rect)) {
                    state->inspector.adjusting_gain = true;
                    inspector_update_gain_from_mouse(state, p.x);
                }
            }
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            state->inspector.adjusting_gain = false;
        }
        break;
    case SDL_MOUSEMOTION:
        if (state->inspector.adjusting_gain) {
            inspector_update_gain_from_mouse(state, event->motion.x);
        }
        break;
    case SDL_TEXTINPUT:
        if (state->inspector.editing_name) {
            size_t current_len = strlen(state->inspector.name);
            size_t incoming = strlen(event->text.text);
            size_t max_len = sizeof(state->inspector.name) - 1;
            if (incoming > 0 && current_len < max_len) {
                size_t copy = incoming;
                if (current_len + copy > max_len) {
                    copy = max_len - current_len;
                }
                strncat(state->inspector.name, event->text.text, copy);
                state->inspector.name_cursor = (int)strlen(state->inspector.name);
            }
        }
        break;
    case SDL_KEYDOWN:
        if (state->inspector.editing_name) {
            SDL_Keycode key = event->key.keysym.sym;
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                inspector_input_commit_if_editing(state);
            } else if (key == SDLK_ESCAPE) {
                const EngineClip* clip = inspector_get_clip_const(state);
                if (clip) {
                    strncpy(state->inspector.name, clip->name, sizeof(state->inspector.name) - 1);
                    state->inspector.name[sizeof(state->inspector.name) - 1] = '\0';
                } else {
                    state->inspector.name[0] = '\0';
                }
                state->inspector.name_cursor = (int)strlen(state->inspector.name);
                inspector_stop_text_input(state);
            } else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
                size_t len = strlen(state->inspector.name);
                if (len > 0) {
                    state->inspector.name[len - 1] = '\0';
                    state->inspector.name_cursor = (int)strlen(state->inspector.name);
                }
            }
        } else if (state->inspector.visible) {
            SDL_Keycode key = event->key.keysym.sym;
            if (key == SDLK_UP) {
                inspector_update_gain(state, state->inspector.gain + 0.05f);
            } else if (key == SDLK_DOWN) {
                inspector_update_gain(state, state->inspector.gain - 0.05f);
            }
        }
        break;
    default:
        break;
    }
}

void inspector_input_handle_gain_drag(AppState* state, int mouse_x) {
    inspector_update_gain_from_mouse(state, mouse_x);
}

void inspector_input_stop_gain_drag(AppState* state) {
    if (!state) {
        return;
    }
    state->inspector.adjusting_gain = false;
}

void inspector_input_sync(AppState* state) {
    if (!state) {
        return;
    }
    if (!state->inspector.visible) {
        inspector_stop_text_input(state);
        state->inspector.adjusting_gain = false;
        return;
    }
    EngineClip* clip = inspector_get_clip_mutable(state);
    if (!clip) {
        inspector_input_init(state);
        return;
    }
    if (!state->inspector.editing_name) {
        strncpy(state->inspector.name, clip->name, sizeof(state->inspector.name) - 1);
        state->inspector.name[sizeof(state->inspector.name) - 1] = '\0';
        state->inspector.name_cursor = (int)strlen(state->inspector.name);
    }
    if (!state->inspector.adjusting_gain) {
        state->inspector.gain = clip->gain;
    }
}

