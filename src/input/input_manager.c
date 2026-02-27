#include "input/input_manager.h"

#include "app_state.h"
#include "engine/engine.h"
#include "input/library_input.h"
#include "input/timeline_input.h"
#include "input/transport_input.h"
#include "input/inspector_input.h"
#include "input/effects_panel_input.h"
#include "input/timeline_selection.h"
#include "session.h"
#include "ui/layout.h"
#include "ui/library_browser.h"
#include "ui/effects_panel.h"
#include "ui/panes.h"
#include "ui/transport.h"
#include "ui/shared_theme_font_adapter.h"
#include "session/project_manager.h"
#include "undo/undo_manager.h"
#include "core/loop/daw_render_invalidation.h"

#include <SDL2/SDL.h>

static void handle_transport_controls(AppState* state, bool was_down, bool is_down) {
    if (!state || !state->engine) {
        return;
    }
    if (!was_down && is_down) {
        if (transport_ui_click_play(&state->transport_ui, state->mouse_x, state->mouse_y)) {
            engine_transport_play(state->engine);
        } else if (transport_ui_click_stop(&state->transport_ui, state->mouse_x, state->mouse_y)) {
            engine_transport_stop(state->engine);
        }
    }
}

static void project_prompt_stop(AppState* state) {
    if (!state) return;
    state->project_prompt.active = false;
    state->project_prompt.buffer[0] = '\0';
    state->project_prompt.cursor = 0;
    SDL_StopTextInput();
}

static bool project_prompt_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !event || !state->project_prompt.active) {
        return false;
    }
    ProjectSavePrompt* prompt = &state->project_prompt;
    switch (event->type) {
    case SDL_TEXTINPUT: {
        const char* txt = event->text.text;
        int len = (int)strlen(prompt->buffer);
        int cur = prompt->cursor;
        if (cur < 0) cur = 0;
        if (cur > len) cur = len;
        for (const char* p = txt; *p; ++p) {
            if ((int)strlen(prompt->buffer) >= (int)sizeof(prompt->buffer) - 1) {
                break;
            }
            // insert at cursor
            memmove(prompt->buffer + cur + 1, prompt->buffer + cur, strlen(prompt->buffer + cur) + 1);
            prompt->buffer[cur] = *p;
            cur++;
        }
        prompt->cursor = cur;
        return true;
    }
    case SDL_KEYDOWN: {
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_BACKSPACE) {
            int len = (int)strlen(prompt->buffer);
            int cur = prompt->cursor;
            if (cur > 0 && len > 0) {
                memmove(prompt->buffer + cur - 1, prompt->buffer + cur, (size_t)(len - cur + 1));
                prompt->cursor = cur - 1;
            }
            return true;
        } else if (key == SDLK_LEFT) {
            if (prompt->cursor > 0) prompt->cursor--;
            return true;
        } else if (key == SDLK_RIGHT) {
            int len = (int)strlen(prompt->buffer);
            if (prompt->cursor < len) prompt->cursor++;
            return true;
        } else if (key == SDLK_ESCAPE) {
            project_prompt_stop(state);
            return true;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            const char* name = prompt->buffer[0] ? prompt->buffer : "project";
            project_manager_save(state, name, true);
            project_prompt_stop(state);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

static void project_load_close(AppState* state) {
    if (!state) return;
    state->project_load.active = false;
    state->project_load.count = 0;
    state->project_load.selected_index = -1;
}

static void project_load_clamp_scroll(ProjectLoadModal* modal, int item_height, int view_height) {
    if (!modal) return;
    float max_scroll = (float)(modal->count * item_height - view_height);
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (modal->scroll_offset < 0.0f) modal->scroll_offset = 0.0f;
    if (modal->scroll_offset > max_scroll) modal->scroll_offset = max_scroll;
}

// Clears meter histories after a forced seek when the debug toggle is enabled.
void input_manager_reset_meter_history_on_seek(AppState* state) {
    if (!state || !state->reset_meter_history_on_seek) {
        return;
    }
    effects_panel_reset_meter_history(state);
    engine_spectrogram_clear_history(state->engine);
}

static bool project_load_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !state->project_load.active || !event) {
        return false;
    }
    ProjectLoadModal* modal = &state->project_load;

    int width = state->window_width > 0 ? state->window_width : 800;
    int height = state->window_height > 0 ? state->window_height : 600;
    SDL_Rect box = {
        (width - 720) / 2,
        (height - 420) / 2,
        720,
        420
    };
    SDL_Rect list_rect = {
        box.x + 16,
        box.y + 56,
        box.w / 2 - 32,
        box.h - 96
    };
    SDL_Rect info_rect = {
        box.x + box.w / 2 + 8,
        box.y + 56,
        box.w / 2 - 24,
        box.h - 126
    };
    SDL_Rect load_button = {
        info_rect.x,
        box.y + box.h - 52,
        120,
        36
    };
    SDL_Rect cancel_button = {
        load_button.x + load_button.w + 12,
        load_button.y,
        120,
        36
    };

    int item_h = 28;
    project_load_clamp_scroll(modal, item_h, list_rect.h);

    switch (event->type) {
    case SDL_MOUSEWHEEL: {
        modal->scroll_offset -= (float)event->wheel.y * (float)item_h * 2.0f;
        project_load_clamp_scroll(modal, item_h, list_rect.h);
        return true;
    }
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_Point p = {event->button.x, event->button.y};
            Uint32 now = SDL_GetTicks();
            if (SDL_PointInRect(&p, &list_rect)) {
                int local_y = p.y - list_rect.y;
                int idx = (int)((local_y + (int)modal->scroll_offset) / item_h);
                if (idx >= 0 && idx < modal->count) {
                    if (modal->last_click_index == idx && (now - modal->last_click_ticks) <= 350) {
                        // Double click -> load
                        if (project_manager_load(state, modal->entries[idx].path)) {
                            project_manager_post_load(state);
                        }
                        project_load_close(state);
                        return true;
                    }
                    modal->selected_index = idx;
                    modal->last_click_index = idx;
                    modal->last_click_ticks = now;
                }
                return true;
            }
            if (SDL_PointInRect(&p, &load_button)) {
                int sel = modal->selected_index;
                if (sel >= 0 && sel < modal->count) {
                    if (project_manager_load(state, modal->entries[sel].path)) {
                        project_manager_post_load(state);
                    }
                    project_load_close(state);
                }
                return true;
            }
            if (SDL_PointInRect(&p, &cancel_button)) {
                project_load_close(state);
                return true;
            }
        }
        break;
    case SDL_KEYDOWN:
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            project_load_close(state);
            return true;
        } else if (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER) {
            int sel = modal->selected_index;
            if (sel >= 0 && sel < modal->count) {
                if (project_manager_load(state, modal->entries[sel].path)) {
                    project_manager_post_load(state);
                }
                project_load_close(state);
            }
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

static void seek_to_seconds(AppState* state, float seconds, bool resume_playback) {
    if (!state || !state->engine) {
        return;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return;
    }
    if (seconds < 0.0f) seconds = 0.0f;

    uint64_t total_frames = 0;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (tracks && track_count > 0) {
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
                if (end > total_frames) {
                    total_frames = end;
                }
            }
        }
    }
    float max_seconds = total_frames > 0 ? (float)total_frames / (float)sample_rate : 0.0f;
    if (max_seconds > 0.0f && seconds > max_seconds) {
        seconds = max_seconds;
    }
    uint64_t frame = (uint64_t)llroundf(seconds * (float)sample_rate);
    bool was_playing = engine_transport_is_playing(state->engine);
    input_manager_reset_meter_history_on_seek(state);
    engine_transport_seek(state->engine, frame);
    if (resume_playback && was_playing) {
        engine_transport_play(state->engine);
    }
}

static void handle_keyboard_shortcuts(InputManager* manager, AppState* state) {
    if (!manager || !state || !state->engine) {
        return;
    }
    bool inspector_text_focus = inspector_input_has_text_focus(state);
    if (state->tempo_ui.editing || library_input_is_editing(state) || state->track_name_editor.editing) {
        return;
    }

    const Uint8* keys = SDL_GetKeyboardState(NULL);
    SDL_Keymod mods = SDL_GetModState();
    bool ctrl_or_cmd = (mods & (KMOD_CTRL | KMOD_GUI)) != 0;
    bool shift_held = (mods & KMOD_SHIFT) != 0;
    if (ctrl_or_cmd && keys[SDL_SCANCODE_Z]) {
        if (shift_held) {
            undo_manager_redo(&state->undo, state);
        } else {
            undo_manager_undo(&state->undo, state);
        }
        return;
    }

    {
        bool theme_next_now = ctrl_or_cmd && shift_held && keys[SDL_SCANCODE_T];
        if (theme_next_now && !manager->previous_theme_next) {
            if (daw_shared_theme_cycle_next()) {
                daw_shared_theme_save_persisted();
                ui_apply_shared_theme(state);
                daw_invalidate_all(state->panes,
                                   state->pane_count,
                                   DAW_RENDER_INVALIDATION_THEME | DAW_RENDER_INVALIDATION_BACKGROUND);
                daw_request_full_redraw(DAW_RENDER_INVALIDATION_THEME | DAW_RENDER_INVALIDATION_BACKGROUND);
            }
        }
        manager->previous_theme_next = theme_next_now;
    }

    {
        bool theme_prev_now = ctrl_or_cmd && shift_held && keys[SDL_SCANCODE_Y];
        if (theme_prev_now && !manager->previous_theme_prev) {
            if (daw_shared_theme_cycle_prev()) {
                daw_shared_theme_save_persisted();
                ui_apply_shared_theme(state);
                daw_invalidate_all(state->panes,
                                   state->pane_count,
                                   DAW_RENDER_INVALIDATION_THEME | DAW_RENDER_INVALIDATION_BACKGROUND);
                daw_request_full_redraw(DAW_RENDER_INVALIDATION_THEME | DAW_RENDER_INVALIDATION_BACKGROUND);
            }
        }
        manager->previous_theme_prev = theme_prev_now;
    }

    bool space_now = keys[SDL_SCANCODE_SPACE] != 0;
    if (!inspector_text_focus && space_now && !manager->previous_space) {
        bool shift_down = (SDL_GetModState() & KMOD_SHIFT) != 0;
        if (shift_down) {
            bool was_playing = engine_transport_is_playing(state->engine);
            uint64_t target_frame = 0;
            if (state->loop_enabled && state->loop_end_frame > state->loop_start_frame) {
                target_frame = state->loop_start_frame;
            }
            input_manager_reset_meter_history_on_seek(state);
            engine_transport_seek(state->engine, target_frame);
            if (was_playing) {
                engine_transport_play(state->engine);
            }
        } else {
            bool was_playing = engine_transport_is_playing(state->engine);
            if (was_playing) {
                engine_transport_stop(state->engine);
            } else {
                engine_transport_play(state->engine);
            }
        }
    }
    manager->previous_space = space_now;
    bool l_now = keys[SDL_SCANCODE_L] != 0;
    if (l_now && !manager->previous_l) {
        bool new_state = !state->loop_enabled;
        if (new_state && state->loop_end_frame <= state->loop_start_frame) {
            const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
            int sample_rate = cfg ? cfg->sample_rate : 0;
            uint64_t default_len = sample_rate > 0 ? (uint64_t)sample_rate : 48000;
            if (default_len == 0) {
                default_len = 48000;
            }
            state->loop_end_frame = state->loop_start_frame + default_len;
        }
        state->loop_enabled = new_state;
        state->loop_restart_pending = false;
        engine_transport_set_loop(state->engine, state->loop_enabled, state->loop_start_frame, state->loop_end_frame);
    }
    manager->previous_l = l_now;

    bool delete_now = keys[SDL_SCANCODE_DELETE] != 0 || keys[SDL_SCANCODE_BACKSPACE] != 0;
    if (!inspector_text_focus && delete_now && !manager->previous_delete) {
        timeline_selection_delete(state);
    }
    manager->previous_delete = delete_now;

    bool c_now = keys[SDL_SCANCODE_C] != 0;
    manager->previous_c = c_now;
    manager->previous_c = c_now;

    bool enter_now = keys[SDL_SCANCODE_RETURN] != 0 || keys[SDL_SCANCODE_KP_ENTER] != 0;
    bool shift_down_now = (SDL_GetModState() & KMOD_SHIFT) != 0;
    if (enter_now && !manager->previous_enter) {
        if (shift_down_now) {
            // Shift+Enter: jump to project start (frame 0)
            seek_to_seconds(state, 0.0f, true);
        } else {
            // Enter: jump to window start
            seek_to_seconds(state, state->timeline_window_start_seconds, true);
        }
    }
    manager->previous_enter = enter_now;

    bool b_now = keys[SDL_SCANCODE_B] != 0;
    if (b_now && !manager->previous_b) {
        state->bounce_requested = true;
    }
    manager->previous_b = b_now;

    bool s_now = keys[SDL_SCANCODE_S] != 0;
    if (s_now && !manager->previous_s) {
        const char* session_path = "config/last_session.json";
        if (!session_save_to_file(state, session_path)) {
            SDL_Log("Save failed to %s", session_path);
        } else {
            SDL_Log("Session saved to %s", session_path);
        }
    }
    manager->previous_s = s_now;

    bool f7_now = keys[SDL_SCANCODE_F7] != 0;
    if (f7_now && !manager->previous_f7) {
        state->engine_logging_enabled = !state->engine_logging_enabled;
        state->runtime_cfg.enable_engine_logs = state->engine_logging_enabled;
        engine_set_logging(state->engine,
                           state->engine_logging_enabled,
                           state->cache_logging_enabled,
                           state->timing_logging_enabled);
    }
    manager->previous_f7 = f7_now;

    bool f8_now = keys[SDL_SCANCODE_F8] != 0;
    if (f8_now && !manager->previous_f8) {
        state->cache_logging_enabled = !state->cache_logging_enabled;
        state->runtime_cfg.enable_cache_logs = state->cache_logging_enabled;
        engine_set_logging(state->engine,
                           state->engine_logging_enabled,
                           state->cache_logging_enabled,
                           state->timing_logging_enabled);
    }
    manager->previous_f8 = f8_now;

    bool f9_now = keys[SDL_SCANCODE_F9] != 0;
    if (f9_now && !manager->previous_f9) {
        state->timing_logging_enabled = !state->timing_logging_enabled;
        state->runtime_cfg.enable_timing_logs = state->timing_logging_enabled;
        engine_set_logging(state->engine,
                           state->engine_logging_enabled,
                           state->cache_logging_enabled,
                           state->timing_logging_enabled);
    }
    manager->previous_f9 = f9_now;
}

void input_manager_init(InputManager* manager) {
    if (!manager) {
        return;
    }
    manager->previous_buttons = 0;
    manager->current_buttons = 0;
    manager->previous_space = false;
    manager->previous_l = false;
    manager->previous_delete = false;
    manager->previous_enter = false;
    manager->previous_c = false;
    manager->previous_b = false;
    manager->previous_s = false;
    manager->previous_f7 = false;
    manager->previous_f8 = false;
    manager->previous_f9 = false;
    manager->previous_theme_next = false;
    manager->previous_theme_prev = false;
    manager->last_click_ticks = 0;
    manager->last_click_track = -1;
    manager->last_click_clip = -1;
    manager->last_header_click_ticks = 0;
    manager->last_header_click_track = -1;
    manager->prev_horiz_slider_down = false;
    manager->prev_vert_slider_down = false;
    manager->prev_window_slider_down = false;
    manager->last_library_click_ticks = 0;
    manager->last_library_click_index = -1;

    timeline_input_init(manager);
    transport_input_init(manager);
}

void input_manager_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event) {
        return;
    }

    if (state->project_load.active) {
        project_load_handle_event(state, event);
        return;
    }
    if (state->project_prompt.active) {
        project_prompt_handle_event(state, event);
        return;
    }

    if (state->tempo_ui.editing) {
        transport_input_handle_event(manager, state, event);
        return;
    }

    if (library_input_is_editing(state)) {
        timeline_input_handle_event(manager, state, event);
        return;
    }

    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP || event->type == SDL_TEXTINPUT) {
        if (inspector_input_has_text_focus(state)) {
            inspector_input_handle_event(manager, state, event);
            return;
        }
    }

    transport_input_handle_event(manager, state, event);
    inspector_input_handle_event(manager, state, event);
    effects_panel_input_handle_event(manager, state, event);
    timeline_input_handle_event(manager, state, event);
}

void input_manager_update(InputManager* manager, AppState* state) {
    if (!manager || !state) {
        return;
    }
    if (state->project_prompt.active || state->project_load.active) {
        // Block normal updates while prompt is active.
        return;
    }

    int mouse_x = 0;
    int mouse_y = 0;
    Uint32 prev_buttons = manager->current_buttons;
    Uint32 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

    manager->previous_buttons = prev_buttons;
    manager->current_buttons = buttons;

    ui_layout_handle_pointer(state, prev_buttons, buttons, mouse_x, mouse_y);
    if (state->layout_runtime.drag.active) {
        state->dragging_library = false;
        state->drag_library_index = -1;
    }

    state->mouse_x = mouse_x;
    state->mouse_y = mouse_y;

    pane_manager_update_hover(&state->pane_manager, mouse_x, mouse_y);
    transport_ui_update_hover(&state->transport_ui, mouse_x, mouse_y);
    ui_layout_handle_hover(state, mouse_x, mouse_y);

    bool left_was_down = (manager->previous_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    bool left_is_down = (manager->current_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

    transport_input_update(manager, state);
    transport_input_follow_playhead(manager, state);
    timeline_input_update(manager, state, left_was_down, left_is_down);
    effects_panel_input_update(manager, state, left_was_down, left_is_down);

    handle_transport_controls(state, left_was_down, left_is_down);
    handle_keyboard_shortcuts(manager, state);

    if (state->loop_restart_pending) {
        if (!state->loop_enabled || state->loop_end_frame <= state->loop_start_frame) {
            state->loop_restart_pending = false;
        } else {
            uint64_t frame = engine_get_transport_frame(state->engine);
            if (frame >= state->loop_start_frame) {
                engine_transport_set_loop(state->engine, true, state->loop_start_frame, state->loop_end_frame);
                state->loop_restart_pending = false;
            }
        }
    }

    if (state->inspector.adjusting_gain) {
        inspector_input_handle_gain_drag(state, state->mouse_x);
    }

    inspector_input_sync(state);
}
