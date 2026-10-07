#include "ui/project_modal_controls.h"
#include "app_state.h"
#include "kit_ui_interaction_sdl.h"
#include "ui/daw_ui_button.h"
#include "ui/font.h"

void daw_project_modal_buttons(const AppState* state, SDL_Rect* load, SDL_Rect* cancel) {
    int width = state->window_width > 0 ? state->window_width : 800;
    int height = state->window_height > 0 ? state->window_height : 600;
    SDL_Rect modal = {(width - 720) / 2, (height - 420) / 2, 720, 420};
    *load = (SDL_Rect){modal.x + modal.w / 2 + 8, modal.y + modal.h - 52, 120, 36};
    *cancel = (SDL_Rect){load->x + load->w + 12, load->y, 120, 36};
}

void daw_project_modal_controls_sync(AppState* state) {
    DawProjectModalControls* ui = &state->project_modal_controls;
    int active = state->project_load.active;
    if (ui->active != active || ui->selected != state->project_load.selected_index ||
        ui->width != state->window_width || ui->height != state->window_height ||
        ui->engine != state->engine) ++ui->generation;
    ui->active = active; ui->selected = state->project_load.selected_index;
    ui->width = state->window_width; ui->height = state->window_height; ui->engine = state->engine;
    kit_ui_surface_begin(&ui->surface, ui->generation);
    if (active) {
        SDL_Rect load, cancel;
        daw_project_modal_buttons(state, &load, &cancel);
        int valid = ui->selected >= 0 && ui->selected < state->project_load.count;
        (void)kit_ui_surface_register(&ui->surface, (KitUiSurfaceKey){2,1},
            (KitRenderRect){load.x,load.y,load.w,load.h}, NULL, valid, NULL);
        (void)kit_ui_surface_register(&ui->surface, (KitUiSurfaceKey){2,2},
            (KitRenderRect){cancel.x,cancel.y,cancel.w,cancel.h}, NULL, 1, NULL);
    }
    (void)kit_ui_surface_end(&ui->surface);
}

int daw_project_modal_controls_event(AppState* state, const SDL_Event* event, int* action) {
    *action = 0;
    daw_project_modal_controls_sync(state);
    KitUiInteractionEvent input;
    KitUiInteractionResult result;
    if (!kit_ui_interaction_event_from_sdl(event, &input)) return 0;
    (void)kit_ui_surface_route(&state->project_modal_controls.surface, &input, &result);
    if (result.activated_id && kit_ui_surface_take_activation(&state->project_modal_controls.surface, result.activated_id)) {
        KitUiSurfaceKey key;
        if (kit_ui_surface_key(&state->project_modal_controls.surface, result.activated_id, &key))
            *action = (int)key.value;
    }
    return result.consumed;
}

void daw_project_modal_controls_draw(SDL_Renderer* renderer, AppState* state) {
    daw_project_modal_controls_sync(state);
    DawThemePalette theme;
    if (!daw_shared_theme_resolve_palette(&theme)) return;
    SDL_Rect rects[2];
    daw_project_modal_buttons(state, &rects[0], &rects[1]);
    const char* labels[] = {"Load", "Cancel"};
    KitUiButtonAppearance appearance;
    kit_ui_button_appearance_preset(KIT_UI_BUTTON_APPEARANCE_COMPACT_ROUNDED, &appearance);
    KitUiSurface* surface = &state->project_modal_controls.surface;
    for (unsigned i = 0; i < 2; ++i) {
        SDL_Rect rect = rects[i];
        DawUiButtonSpec spec;
        daw_ui_button_spec_init(&spec, labels[i]);
        spec.state = kit_ui_interaction_button_state(&surface->interaction, &surface->controls[i], i == 0);
        DawUiButtonStyle style;
        if (daw_ui_button_style_resolve(&theme, &spec, &style)) continue;
        float radius = kit_ui_corner_radius_clamp(appearance.corner_radius, rect.w, rect.h);
        SDL_SetRenderDrawColor(renderer, style.outline.r, style.outline.g, style.outline.b, style.outline.a);
        SDL_FRect outer = {rect.x, rect.y, rect.w, rect.h};
        vk_renderer_fill_rounded_rect((VkRenderer*)renderer, &outer, radius);
        KitRenderRect inner = kit_ui_rect_inset((KitRenderRect){rect.x,rect.y,rect.w,rect.h}, appearance.border_thickness);
        SDL_FRect inset = {inner.x,inner.y,inner.width,inner.height};
        SDL_SetRenderDrawColor(renderer, style.fill.r, style.fill.g, style.fill.b, style.fill.a);
        vk_renderer_fill_rounded_rect((VkRenderer*)renderer, &inset,
            kit_ui_corner_radius_for_inset(radius, appearance.border_thickness));
        SDL_Color ink = {style.text.r,style.text.g,style.text.b,style.text.a};
        ui_draw_text_clipped(renderer, rect.x + (rect.w - ui_measure_text_width(labels[i],1)) / 2,
            rect.y + (rect.h - ui_font_line_height(1)) / 2, labels[i], ink, 1, rect.w - 8);
        if (spec.state.focused) {
            SDL_Rect marker = {rect.x + 5, rect.y + rect.h - 4, rect.w - 10, 1};
            SDL_SetRenderDrawColor(renderer, ink.r,ink.g,ink.b,ink.a);
            SDL_RenderFillRect(renderer, &marker);
        }
    }
}
