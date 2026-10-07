#include "app/media_import.h"
#include "input/input_manager.h"

#include "app/audio_recording.h"
#include "app_state.h"
#include "ui/text_edit.h"
#include "app/workspace_authoring/daw_workspace_authoring_host.h"
#include "engine/engine.h"
#include "input/library_input.h"
#include "input/timeline_input.h"
#include "input/transport_input.h"
#include "ui/transport_controls.h"
#include "input/inspector_input.h"
#include "input/effects_panel_input.h"
#include "input/midi_editor_input.h"
#include "input/midi_instrument_panel_input.h"
#include "input/project_modal_input.h"
#include "input/timeline_selection.h"
#include "session.h"
#include "ui/layout.h"
#include "ui/library_browser.h"
#include "ui/effects_panel.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "ui/panes.h"
#include "ui/transport.h"
#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"
#include "session/project_manager.h"
#include "undo/undo_manager.h"
#include "core/loop/daw_render_invalidation.h"

#include <SDL2/SDL.h>

// Applies each transport pointer-down event once, even when down/up arrive in one loop iteration.
// Clears meter histories after a forced seek when the debug toggle is enabled.
void input_manager_reset_meter_history_on_seek(AppState* state) {
    if (!state || !state->reset_meter_history_on_seek) {
        return;
    }
    effects_panel_reset_meter_history(state);
    engine_spectrogram_clear_history(state->engine);
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
    (void)resume_playback; // Seek preserves the engine's playback mode.
    if (engine_transport_seek(state->engine, frame)) input_manager_reset_meter_history_on_seek(state);
}

static bool input_manager_authoring_text_entry_active(AppState* state) {
    if (!state) {
        return false;
    }
    return project_modal_input_active(state) ||
           state->tempo_ui.editing ||
           library_input_is_editing(state) ||
           state->track_name_editor.editing ||
           inspector_input_has_text_focus(state);
}

static void apply_shared_ui_font(AppState* state) {
    char font_path[256];
    int font_point_size = 9;
    const uint32_t reason_bits =
        DAW_RENDER_INVALIDATION_LAYOUT | DAW_RENDER_INVALIDATION_CONTENT | DAW_RENDER_INVALIDATION_BACKGROUND;
    if (!state) {
        return;
    }
    if (daw_shared_font_resolve_ui_regular(font_path, sizeof(font_path), &font_point_size)) {
        ui_font_set(font_path, font_point_size);
    } else {
        ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf", 9);
    }
    daw_invalidate_all(state->panes, state->pane_count, reason_bits);
    daw_request_full_redraw(reason_bits);
}

static void input_manager_apply_authoring_preview_dirty(AppState* state) {
    const uint32_t theme_bits = DAW_RENDER_INVALIDATION_THEME | DAW_RENDER_INVALIDATION_BACKGROUND;
    if (!state) {
        return;
    }
    if (daw_workspace_authoring_host_take_theme_dirty(&state->workspace_authoring)) {
        ui_apply_shared_theme(state);
        daw_invalidate_all(state->panes, state->pane_count, theme_bits);
        daw_request_full_redraw(theme_bits);
    }
    if (daw_workspace_authoring_host_take_font_dirty(&state->workspace_authoring)) {
        apply_shared_ui_font(state);
    }
    DawWorkspaceAuthoringHostState *host = &state->workspace_authoring;
    if (host->last_event_entered) {
        daw_workspace_authoring_projection_capture(&host->projection,
                                                   state->panes[3].visible,
                                                   state->panes[2].visible,
                                                   state->layout_runtime.transport_ratio,
                                                   state->layout_runtime.library_ratio,
                                                   state->layout_runtime.mixer_ratio);
    }
    DawWorkspaceAuthoringProjectionAction action = DAW_WORKSPACE_AUTHORING_PROJECTION_NONE;
    if (daw_workspace_authoring_projection_take_action(&host->projection, &action)) {
        daw_workspace_authoring_projection_apply_action(&host->projection, action);
    }
    if (host->projection.baseline_valid && (daw_workspace_authoring_host_active(host) || host->last_event_canceled || host->projection_profile_loaded)) {
        if (host->last_event_canceled) {
            daw_workspace_authoring_projection_restore_baseline(&host->projection);
        }
        state->panes[3].visible = host->projection.library_visible != 0u;
        state->panes[2].visible = host->projection.inspector_visible != 0u;
        state->layout_runtime.transport_ratio = host->projection.transport_ratio;
        state->layout_runtime.library_ratio = host->projection.library_ratio;
        state->layout_runtime.mixer_ratio = host->projection.mixer_ratio;
        if (state->window_width > 0 && state->window_height > 0) {
            ui_layout_panes(state, state->window_width, state->window_height);
        }
        daw_invalidate_all(state->panes, state->pane_count,
                           DAW_RENDER_INVALIDATION_LAYOUT | DAW_RENDER_INVALIDATION_OVERLAY);
        daw_request_full_redraw(DAW_RENDER_INVALIDATION_LAYOUT | DAW_RENDER_INVALIDATION_OVERLAY);
        host->projection_profile_loaded = 0u;
    }
}

static void input_manager_save_authoring_accepted_preferences(AppState* state) {
    if (!state || !state->workspace_authoring.last_event_accepted) {
        return;
    }
    (void)daw_shared_theme_save_persisted();
    (void)daw_shared_font_save_persisted();
    (void)daw_shared_font_zoom_save_persisted();
}

static void handle_keyboard_shortcuts(InputManager* manager, AppState* state) {
    if (!manager || !state || !state->engine) {
        return;
    }
    if (state->tempo_ui.editing || library_input_is_editing(state) || state->track_name_editor.editing) {
        return;
    }

    const Uint8* keys = SDL_GetKeyboardState(NULL);
    SDL_Keymod mods = SDL_GetModState();
    bool ctrl_or_cmd = (mods & (KMOD_CTRL | KMOD_GUI)) != 0;
    bool shift_held = (mods & KMOD_SHIFT) != 0;

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

    {
        bool font_zoom_in_now = ctrl_or_cmd && (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS]);
        bool font_zoom_out_now = ctrl_or_cmd && (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]);
        bool font_zoom_reset_now = ctrl_or_cmd && (keys[SDL_SCANCODE_0] || keys[SDL_SCANCODE_KP_0]);
        bool font_zoom_changed = false;
        if (font_zoom_in_now && !manager->previous_font_zoom_in) {
            font_zoom_changed = daw_shared_font_step_by(1) || font_zoom_changed;
        }
        if (font_zoom_out_now && !manager->previous_font_zoom_out) {
            font_zoom_changed = daw_shared_font_step_by(-1) || font_zoom_changed;
        }
        if (font_zoom_reset_now && !manager->previous_font_zoom_reset) {
            font_zoom_changed = daw_shared_font_reset_zoom_step() || font_zoom_changed;
        }
        if (font_zoom_changed) {
            daw_shared_font_zoom_save_persisted();
            apply_shared_ui_font(state);
        }
        manager->previous_font_zoom_in = font_zoom_in_now;
        manager->previous_font_zoom_out = font_zoom_out_now;
        manager->previous_font_zoom_reset = font_zoom_reset_now;
    }

    bool l_now = keys[SDL_SCANCODE_L] != 0;
    if (l_now && !manager->previous_l) {
        bool new_state = !state->loop_enabled;
        uint64_t previous_end = state->loop_end_frame;
        if (new_state && state->loop_end_frame <= state->loop_start_frame) {
            const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
            int sample_rate = cfg ? cfg->sample_rate : 0;
            uint64_t default_len = sample_rate > 0 ? (uint64_t)sample_rate : 48000;
            if (default_len == 0) {
                default_len = 48000;
            }
            state->loop_end_frame = state->loop_start_frame + default_len;
        }
        if (engine_transport_set_loop(state->engine, new_state, state->loop_start_frame, state->loop_end_frame)) {
            state->loop_enabled = new_state;
            state->loop_restart_pending = false;
        } else {
            state->loop_end_frame = previous_end;
        }
    }
    manager->previous_l = l_now;


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
    bool folder_b_now = ctrl_or_cmd && b_now;
    if (midi_editor_input_qwerty_capturing(state) && !ctrl_or_cmd) {
        manager->previous_folder_b = folder_b_now;
        manager->previous_b = b_now;
    } else if (folder_b_now && !manager->previous_folder_b) {
        (void)library_input_open_folder_dialog(state);
    } else if (b_now && !manager->previous_b) {
        state->bounce_requested = true;
        manager->previous_folder_b = folder_b_now;
        manager->previous_b = b_now;
    } else {
        manager->previous_folder_b = folder_b_now;
        manager->previous_b = b_now;
    }

    bool s_now = keys[SDL_SCANCODE_S] != 0;
    if (midi_editor_input_qwerty_capturing(state) && !ctrl_or_cmd) {
        manager->previous_s = s_now;
    } else if (s_now && !manager->previous_s) {
        char session_path[SESSION_PATH_MAX];
        project_manager_last_session_path(state, session_path, sizeof(session_path));
        if (!session_save_to_file(state, session_path)) {
            SDL_Log("Save failed to %s", session_path);
        } else {
            SDL_Log("Session saved to %s", session_path);
        }
        manager->previous_s = s_now;
    } else {
        manager->previous_s = s_now;
    }

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
    manager->previous_folder_b = false;
    manager->previous_s = false;
    manager->previous_f7 = false;
    manager->previous_f8 = false;
    manager->previous_f9 = false;
    manager->previous_theme_next = false;
    manager->previous_theme_prev = false;
    manager->previous_font_zoom_in = false;
    manager->previous_font_zoom_out = false;
    manager->previous_font_zoom_reset = false;
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
    if (event->type == SDL_WINDOWEVENT && (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
        event->window.event == SDL_WINDOWEVENT_HIDDEN)) daw_text_cancel_composition(state);
    daw_project_modal_controls_sync(state);
    if (daw_transport_controls_event(state,event))return;
    if (event->type == SDL_DROPFILE) {
        if (event->drop.file) {
            (void)library_input_handle_drop_file(state, event->drop.file, state->mouse_x, state->mouse_y);
            SDL_free(event->drop.file);
        }
        return;
    }
    if (event->type == SDL_DROPTEXT) {
        if (event->drop.file) {
            SDL_free(event->drop.file);
        }
        return;
    }

    daw_workspace_authoring_host_set_viewport(&state->workspace_authoring,
                                              (uint32_t)(state->window_width > 0 ? state->window_width : 0),
                                              (uint32_t)(state->window_height > 0 ? state->window_height : 0));
    if (daw_workspace_authoring_host_handle_sdl_event(
            &state->workspace_authoring,
            event,
            input_manager_authoring_text_entry_active(state))) {
        daw_transport_controls_sync(&state->transport_ui,state);
        input_manager_apply_authoring_preview_dirty(state);
        input_manager_save_authoring_accepted_preferences(state);
        return;
    }

    if (project_modal_input_handle_event(state, event)) {
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

    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP || event->type == SDL_TEXTINPUT || event->type == SDL_TEXTEDITING) {
        if (inspector_input_has_text_focus(state)) {
            inspector_input_handle_event(manager, state, event);
            return;
        }
    }

    // History responds once to the recorded key edge, never to a later sampled keyboard state.
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_z &&
        (event->key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) &&
        !state->track_name_editor.editing && !state->tempo_ui.editing) {
        if (!event->key.repeat) {
            if (event->key.keysym.mod & KMOD_SHIFT) undo_manager_redo(&state->undo, state);
            else undo_manager_undo(&state->undo, state);
        }
        return;
    }
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE &&
        (event->key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
        daw_media_import_cancel(state);
        return;
    }
    // Space is an edge-triggered action; key repeat and text editing cannot toggle transport.
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_SPACE &&
        !state->track_name_editor.editing) {
        if (!event->key.repeat) {
            if (event->key.keysym.mod & KMOD_SHIFT) {
                uint64_t target = state->loop_enabled && state->loop_end_frame > state->loop_start_frame
                    ? state->loop_start_frame : 0;
                if (engine_transport_seek(state->engine, target)) input_manager_reset_meter_history_on_seek(state);
            } else if (engine_transport_requested_playing(state->engine)) {
                engine_transport_pause(state->engine);
            } else {
                (void)daw_audio_recording_drain_if_transport_playing(state);
                engine_transport_play(state->engine);
            }
        }
        return;
    }
    transport_input_handle_event(manager, state, event);
    if (midi_instrument_panel_input_handle_event(manager, state, event)) {
        return;
    }
    if (midi_editor_input_handle_event(manager, state, event)) {
        return;
    }
    inspector_input_handle_event(manager, state, event);
    effects_panel_input_handle_event(manager, state, event);
    // Timeline deletion is edge-triggered while note, automation and text editors retain ownership.
    if (event->type == SDL_KEYDOWN && (event->key.keysym.sym == SDLK_DELETE || event->key.keysym.sym == SDLK_BACKSPACE) &&
        !state->track_name_editor.editing && !state->tempo_ui.editing && !midi_editor_should_render(state) &&
        !state->timeline_automation_mode && !state->timeline_tempo_overlay_enabled) {
        if (!event->key.repeat) timeline_selection_delete(state);
        return;
    }
    timeline_input_handle_event(manager, state, event);
}

void input_manager_update(InputManager* manager, AppState* state) {
    if (!manager || !state) {
        return;
    }
    daw_transport_controls_sync(&state->transport_ui,state);
    if (project_modal_input_active(state)) {
        // Block normal updates while prompt is active.
        return;
    }

    int mouse_x = 0;
    int mouse_y = 0;
    Uint32 prev_buttons = manager->current_buttons;
    Uint32 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

    manager->previous_buttons = prev_buttons;
    manager->current_buttons = buttons;
    state->mouse_x = mouse_x;
    state->mouse_y = mouse_y;

    if (daw_workspace_authoring_host_active(&state->workspace_authoring)) {
        return;
    }

    ui_layout_handle_pointer(state, prev_buttons, buttons, mouse_x, mouse_y);
    if (state->layout_runtime.drag.active) {
        state->dragging_library = false;
        state->drag_library_index = -1;
    }

    pane_manager_update_hover(&state->pane_manager, mouse_x, mouse_y);
    transport_ui_update_hover(&state->transport_ui, mouse_x, mouse_y);
    ui_layout_handle_hover(state, mouse_x, mouse_y);

    bool left_was_down = (manager->previous_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    bool left_is_down = (manager->current_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

    transport_input_update(manager, state);
    transport_input_follow_playhead(manager, state);
    timeline_input_update(manager, state, left_was_down, left_is_down);
    midi_instrument_panel_input_update(manager, state, left_was_down, left_is_down);
    midi_editor_input_update(manager, state, left_was_down, left_is_down);
    if (!midi_editor_should_render(state)) {
        effects_panel_input_update(manager, state, left_was_down, left_is_down);
    }
    library_browser_refresh_project_usage(&state->library, state->engine);

    handle_keyboard_shortcuts(manager, state);

    if (state->loop_restart_pending) {
        if (!state->loop_enabled || state->loop_end_frame <= state->loop_start_frame) {
            state->loop_restart_pending = false;
        } else {
            uint64_t frame = engine_get_presentation_frame(state->engine);
            if (frame >= state->loop_start_frame) {
                if (engine_transport_set_loop(state->engine, true, state->loop_start_frame, state->loop_end_frame))
                    state->loop_restart_pending = false;
            }
        }
    }

    if (!midi_editor_should_render(state) && state->inspector.adjusting_gain) {
        inspector_input_handle_gain_drag(state, state->mouse_x);
    }

    if (!midi_editor_should_render(state)) {
        inspector_input_sync(state);
    }
}
