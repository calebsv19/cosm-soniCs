#include "app_state.h"
#include "ui/editor_controls.h"
#include "ui/layout.h"
#include "ui/effects_panel.h"
#include "input/effects_panel_input.h"
#include "input/effects_panel_input_helpers.h"
#include "input/timeline/timeline_input_mouse_click.h"
#include "input/timeline_selection.h"
#include <SDL2/SDL_ttf.h>
#include "ui/font.h"
#include <assert.h>
#include <stdlib.h>

// Finds a real registered owner without inventing test-only hit geometry.
static SDL_Rect control(AppState* state, unsigned domain, uint64_t key) {
    daw_editor_controls_sync(state);
    KitUiSurface* surface = &state->editor_controls.surface;
    assert(surface->collection_result.code == CORE_OK);
    for (unsigned i = 0; i < surface->count; ++i)
        if (surface->keys[i].domain == domain && surface->keys[i].value == key) {
            KitRenderRect r = surface->controls[i].bounds;
            return (SDL_Rect){r.x, r.y, r.width, r.height};
        }
    fprintf(stderr, "missing exception %u/%llu\n", domain, (unsigned long long)key);
    assert(!"missing visible exception"); return (SDL_Rect){0};
}
// Routes one edge through the actual input manager, not an isolated kit fixture.
static void pointer(AppState* state, Uint32 type, SDL_Rect rect) {
    SDL_Event event = {.type = type}; event.button.button = SDL_BUTTON_LEFT; event.button.clicks = 1;
    event.button.x = rect.x + rect.w / 2; event.button.y = rect.y + rect.h / 2;
    state->mouse_x = event.button.x; state->mouse_y = event.button.y;
    input_manager_handle_event(&state->input_manager, state, &event);
}
// Applies a matching press/release through the product route.
static void click(AppState* state, unsigned domain, uint64_t key) {
    SDL_Rect rect = control(state, domain, key);
    pointer(state, SDL_MOUSEBUTTONDOWN, rect); pointer(state, SDL_MOUSEBUTTONUP, rect);
}
// Qualifies sampled duplicate suppression and stable track-header command identity.
static void tracks(AppState* state) {
    const EngineTrack* track = engine_get_tracks(state->engine);
    uint64_t key = track[0].runtime_id * 2;
    SDL_Rect mute = control(state, 15, key); bool before = track[0].muted;
    pointer(state, SDL_MOUSEBUTTONDOWN, mute);
    timeline_input_mouse_click_update(&state->input_manager, state, false, true);
    assert(engine_get_tracks(state->engine)[0].muted == before);
    pointer(state, SDL_MOUSEBUTTONUP, mute);
    assert(engine_get_tracks(state->engine)[0].muted != before);
    mute = control(state, 15, key);
    before = engine_get_tracks(state->engine)[0].muted;
    pointer(state, SDL_MOUSEBUTTONDOWN, mute);
    pointer(state, SDL_MOUSEBUTTONUP, (SDL_Rect){-100,-100,1,1});
    assert(engine_get_tracks(state->engine)[0].muted == before);
    click(state, 15, key + 1); assert(engine_get_tracks(state->engine)[0].solo);
}
// Preserves snapshot undo and menu ownership while excluding controls underneath it.
static void snapshot(AppState* state) {
    state->inspector.visible = false;
    timeline_selection_set_single(state, 0, -1); effects_panel_sync_from_engine(state);
    state->effects_panel.view_mode = FX_PANEL_VIEW_LIST;
    bool before = engine_get_tracks(state->engine)[0].muted;
    int undo = state->undo.undo_count; SDL_Rect mute = control(state, 16, 1);
    pointer(state, SDL_MOUSEBUTTONDOWN, mute);
    assert(engine_get_tracks(state->engine)[0].muted == before && state->undo.undo_count == undo);
    pointer(state, SDL_MOUSEBUTTONUP, mute);
    assert(engine_get_tracks(state->engine)[0].muted != before && state->undo.undo_count == undo + 1);
    undo_manager_undo(&state->undo, state); effects_panel_sync_from_engine(state);
    assert(engine_get_tracks(state->engine)[0].muted == before);
    click(state, 16, 0); assert(state->effects_panel.track_snapshot.instrument_menu_open);
    daw_editor_controls_sync(state);
    KitUiSurface* surface = &state->editor_controls.surface;
    for (unsigned i = 0; i < surface->count; ++i)
        assert(surface->keys[i].domain != 17 && surface->keys[i].domain != 18);
    EffectsPanelLayout layout; effects_panel_compute_layout(state, &layout);
    assert(layout.track_snapshot.instrument_browser.row_count);
    MidiPresetBrowserRow row = layout.track_snapshot.instrument_browser.rows[0];
    assert(row.type == MIDI_PRESET_BROWSER_ROW_CATEGORY);
    click(state, 16, 1000 + row.category);
    assert(state->effects_panel.track_snapshot.instrument_menu_expanded_category == (int)row.category);
    click(state, 16, 0); assert(!state->effects_panel.track_snapshot.instrument_menu_open);
}
// Qualifies EQ toggles through their original history owner and rejects view takeover.
static void equalizer(AppState* state) {
    state->effects_panel.list_detail_mode = FX_LIST_DETAIL_EQ;
    state->effects_panel.track_snapshot.eq_open = true;
    effects_panel_set_eq_detail_view(state, EQ_DETAIL_VIEW_MASTER);
    SDL_Rect low = control(state, 17, 2);
    bool before = state->effects_panel.eq_curve_master.low_cut.enabled;
    pointer(state, SDL_MOUSEBUTTONDOWN, low);
    assert(state->effects_panel.eq_curve_master.low_cut.enabled == before);
    pointer(state, SDL_MOUSEBUTTONUP, low);
    assert(state->effects_panel.eq_curve_master.low_cut.enabled != before);
    before = state->effects_panel.eq_curve_master.low_cut.enabled;
    pointer(state, SDL_MOUSEBUTTONDOWN, low);
    effects_panel_set_eq_detail_view(state, EQ_DETAIL_VIEW_TRACK);
    pointer(state, SDL_MOUSEBUTTONUP, low);
    assert(state->effects_panel.eq_curve_master.low_cut.enabled == before);
    EffectsPanelLayout layout; EffectsSlotLayout hidden; effects_panel_compute_layout(state, &layout);
    assert(!compute_detail_slot_layout(state, &layout, 0, &hidden));
    daw_editor_controls_sync(state);
    KitUiSurface* surface = &state->editor_controls.surface;
    for (unsigned i = 0; i < surface->count; ++i) {
        KitUiSurfaceKey key = surface->keys[i];
        // Specialized EQ has no ordinary effect-detail enable/remove/preview buttons.
        assert(key.domain != 14 || key.value < 8 || key.value >= 1000 || key.value % 16 == 11);
    }
}
// Checks meter modes and rack palettes against accepted parameter readback and undo.
static void meters(AppState* state) {
    state->effects_panel.track_snapshot.eq_open = false;
    const unsigned types[] = {102,104,105};
    for (unsigned t = 0; t < 3; ++t) {
        FxInstId id = undo_manager_add_effect(state, 0, types[t]); assert(id);
        effects_panel_sync_from_engine(state);
        int index = state->effects_panel.chain_count - 1;
        state->effects_panel.list_open_slot_index = index;
        state->effects_panel.list_detail_mode = FX_LIST_DETAIL_METER;
        unsigned param = types[t] == 105 ? 2 : 0;
        SDL_Rect button = control(state, 18, (uint64_t)id * 128 + 101);
        float before = state->effects_panel.chain[index].param_values[param];
        int undo = state->undo.undo_count;
        pointer(state, SDL_MOUSEBUTTONDOWN, button);
        assert(state->effects_panel.chain[index].param_values[param] == before);
        pointer(state, SDL_MOUSEBUTTONUP, button);
        assert(state->effects_panel.chain[index].param_values[param] == 1 && state->undo.undo_count == undo + 1);
        undo_manager_undo(&state->undo, state); effects_panel_sync_from_engine(state);
        assert(state->effects_panel.chain[index].param_values[param] == before);
        // Switching the painted detail mode invalidates a pending old meter action.
        pointer(state, SDL_MOUSEBUTTONDOWN, button);
        state->effects_panel.list_detail_mode = FX_LIST_DETAIL_EQ;
        pointer(state, SDL_MOUSEBUTTONUP, button);
        assert(state->effects_panel.chain[index].param_values[param] == before);
        if (types[t] == 105) {
            state->effects_panel.view_mode = FX_PANEL_VIEW_STACK;
            click(state, 18, (uint64_t)id * 128 + 102);
            assert(state->effects_panel.chain[index].param_values[2] == 2);
            state->effects_panel.view_mode = FX_PANEL_VIEW_LIST;
        }
    }
}
// Exercises actual registered spec discrete widgets while keeping continuous regions unregistered.
static void parameters(AppState* state) {
    state->effects_panel.view_mode = FX_PANEL_VIEW_STACK;
    state->effects_panel.spec_panel_enabled = true;
    bool exercised = false;
    for (int type = 0; type < state->effects_panel.type_count && !exercised; ++type) {
        FxInstId id = undo_manager_add_effect(state, 0, state->effects_panel.types[type].type_id);
        if (!id) continue;
        effects_panel_sync_from_engine(state);
        int index = state->effects_panel.chain_count - 1;
        daw_editor_controls_sync(state); KitUiSurface* surface = &state->editor_controls.surface;
        for (unsigned i = 0; i < surface->count; ++i) {
            uint64_t value = surface->keys[i].value;
            if (surface->keys[i].domain != 18 || value / 128 != id || value % 128 >= 100 || value % 2) continue;
            unsigned param = value % 128 / 2;
            float before = state->effects_panel.chain[index].param_values[param];
            click(state, 18, value);
            assert(state->effects_panel.chain[index].param_values[param] != before);
            exercised = true; break;
        }
        if (!exercised) { assert(engine_fx_track_remove(state->engine, 0, id)); effects_panel_sync_from_engine(state); }
    }
    assert(exercised);
    FxInstId eq = undo_manager_add_effect(state, 0, 30); assert(eq);
    effects_panel_sync_from_engine(state);
    int eq_index = state->effects_panel.chain_count - 1;
    float enum_before = state->effects_panel.chain[eq_index].param_values[0];
    click(state, 18, (uint64_t)eq * 128);
    assert(state->effects_panel.chain[eq_index].param_values[0] != enum_before);
    FxInstId delay = undo_manager_add_effect(state, 0, 50); assert(delay);
    effects_panel_sync_from_engine(state);
    int index = state->effects_panel.chain_count - 1;
    for (int spec = 0; spec < 2; ++spec) {
        state->effects_panel.spec_panel_enabled = spec;
        unsigned before = state->effects_panel.chain[index].param_mode[0];
        SDL_Rect mode = control(state, 18, (uint64_t)delay * 128 + 1);
        pointer(state, SDL_MOUSEBUTTONDOWN, mode);
        assert(state->effects_panel.chain[index].param_mode[0] == before);
        pointer(state, SDL_MOUSEBUTTONUP, mode);
        assert(state->effects_panel.chain[index].param_mode[0] != before);
        // Changing the spec presentation cancels an old native/beats button press.
        before = state->effects_panel.chain[index].param_mode[0];
        pointer(state, SDL_MOUSEBUTTONDOWN, mode);
        state->effects_panel.spec_panel_enabled = !spec;
        pointer(state, SDL_MOUSEBUTTONUP, mode);
        assert(state->effects_panel.chain[index].param_mode[0] == before);
    }
}
// Uses an offline engine and dummy platform to qualify the real application owners.
int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1); SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) && !TTF_Init());
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf", 9));
    AppState* state = calloc(1, sizeof(*state)); assert(state);
    EngineRuntimeConfig config; config_set_defaults(&config);
    state->engine = engine_create(&config); assert(state->engine);
    undo_manager_init(&state->undo); input_manager_init(&state->input_manager);
    state->active_track_index = 0; state->selected_track_index = -1; state->selected_clip_index = -1;
    ui_init_panes(state); state->layout_runtime.mixer_ratio = 0.55f; ui_layout_panes(state, 1600, 1000); effects_panel_input_init(state);
    tracks(state); snapshot(state); equalizer(state); meters(state); parameters(state);
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state); ui_font_shutdown(); TTF_Quit(); SDL_Quit();
    puts("shared_editor_exceptions_test: success (real owners, one release, cancellation, history and accepted readback)");
}
