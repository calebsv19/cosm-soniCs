#include "input/automation_keyboard.h"
#include "app_state.h"
#include "daw/automation_actions.h"
#include "input/timeline_snap.h"
#include "input/inspector_input_numeric_edit.h"
#include <math.h>

// Resolves the selected lane and point without implicitly creating content.
bool automation_keyboard_handle(AppState* state, const SDL_Event* event) {
    if (!state || !event || event->type != SDL_KEYDOWN || !state->timeline_automation_mode ||
        state->automation_ui.point_index < 0 || state->track_name_editor.editing || state->tempo_ui.editing ||
        (state->inspector.editing_name || inspector_numeric_is_editing(&state->inspector.edit)) || (event->key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) return false;
    SDL_Keycode key = event->key.keysym.sym;
    bool curve_key = key >= SDLK_1 && key <= SDLK_3;
    bool boundary_key = key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET;
    bool remove = key == SDLK_DELETE || key == SDLK_BACKSPACE;
    bool horizontal = key == SDLK_LEFT || key == SDLK_RIGHT;
    bool vertical = key == SDLK_UP || key == SDLK_DOWN;
    if (!curve_key && !boundary_key && !remove && !horizontal && !vertical) return false;
    if (state->automation_ui.dragging || event->key.repeat) return true;
    int track = state->automation_ui.track_index, clip = state->automation_ui.clip_index;
    const EngineAutomationLane* lane = NULL;
    if (!engine_clip_get_automation_lane(state->engine, track, clip, state->automation_ui.target, &lane) ||
        !lane || state->automation_ui.point_index >= lane->point_count) return true;
    int index = state->automation_ui.point_index;
    EngineAutomationPoint point = lane->points[index];
    if (curve_key) {
        daw_automation_set_curve(state, track, clip, lane->target, index, (EngineAutomationCurve)(key - SDLK_1));
    } else if (boundary_key) {
        bool before = key == SDLK_LEFTBRACKET;
        EngineAutomationBoundary old = before ? lane->before : lane->after;
        daw_automation_set_boundary(state, track, clip, lane->target, before,
            old == ENGINE_AUTOMATION_BOUNDARY_HOLD ? ENGINE_AUTOMATION_BOUNDARY_BASELINE : ENGINE_AUTOMATION_BOUNDARY_HOLD);
    } else if (remove) {
        if (daw_automation_remove_point(state, track, clip, lane->target, index)) state->automation_ui.point_index = -1;
    } else {
        int factor = event->key.keysym.mod & KMOD_SHIFT ? 10 : 1;
        if (horizontal) {
            const EngineRuntimeConfig* config = engine_get_config(state->engine);
            double step = event->key.keysym.mod & KMOD_ALT ? 1 :
                timeline_get_snap_interval_seconds(state, state->timeline_visible_seconds) * (double)config->sample_rate;
            step = fmax(1, step) * factor;
            uint64_t distance = (uint64_t)llround(step);
            const EngineClip* region = &engine_get_tracks(state->engine)[track].clips[clip];
            point.frame = key == SDLK_LEFT ? (point.frame > distance ? point.frame - distance : 0) :
                (distance > region->duration_frames - point.frame ? region->duration_frames : point.frame + distance);
        } else {
            float step = lane->domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL && lane->target == ENGINE_AUTOMATION_TARGET_VOLUME ? .1f : .01f;
            point.value += (key == SDLK_UP ? step : -step) * factor;
        }
        if (daw_automation_update_point(state, track, clip, lane->target, index, point.frame, point.value, &index))
            state->automation_ui.point_index = index;
    }
    return true;
}
