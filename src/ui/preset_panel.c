#include "ui/preset_panel.h"
#include "app_state.h"
#include "ui/font.h"
#include "ui/daw_ui_button.h"
#include "ui/midi_instrument_panel.h"
#include "ui/text_edit.h"
#include "ui/render_utils.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
// Compares runtime handles without depending on struct padding.
static bool same_target(DawActionTarget a, DawActionTarget b) {
    return a.project_generation == b.project_generation && a.track_id == b.track_id && a.clip_id == b.clip_id && a.kind == b.kind;
}
// Refreshes stable row backing storage only outside queued frame construction.
void daw_preset_panel_refresh(AppState* state) {
    if (!state) return;
    DawPresetUI* ui = &state->preset_ui;
    ++ui->library_generation;
    DawPresetStatus status = daw_preset_library_list(&ui->library);
    if (status != DAW_PRESET_OK) SDL_strlcpy(ui->message, daw_preset_status_message(status), sizeof(ui->message));
    else if (ui->library.invalid_count || ui->library.truncated)
        snprintf(ui->message, sizeof(ui->message), "%d presets; %d invalid files%s", ui->library.count,
            ui->library.invalid_count, ui->library.truncated ? "; list limited to 128" : "");
    for (int i = 0; i < ui->library.count; ++i) snprintf(ui->row_labels[i], sizeof(ui->row_labels[i]), "%s   |   %s",
        ui->library.items[i].name, engine_instrument_preset_category_display_name(engine_instrument_preset_category(ui->library.items[i].base)));
}
// Keeps a loaded-sound comparison for panel feedback without changing any engine state.
void daw_preset_panel_remember(AppState* state, DawActionTarget target, const DawUserPreset* preset) {
    if (!state || !preset) return;
    state->preset_ui.loaded = *preset; state->preset_ui.loaded_target = target; state->preset_ui.has_loaded = true;
}
// Finds matching saved sounds after reopening while preserving project-owned parameter values.
const char* daw_preset_panel_sound_name(const AppState* state, const DawInstrumentSnapshot* sound, bool* modified) {
    *modified = false;
    const DawPresetUI* ui = &state->preset_ui;
    if (ui->has_loaded && same_target(ui->loaded_target, sound->target) && ui->loaded.base == sound->effective_preset) {
        *modified = !daw_preset_sound_equal(&ui->loaded, sound->effective_preset, sound->effective_params); return ui->loaded.name;
    }
    for (int i = 0; i < ui->library.count; ++i)
        if (daw_preset_sound_equal(&ui->library.items[i], sound->effective_preset, sound->effective_params)) return ui->library.items[i].name;
    DawUserPreset factory = {.base = sound->effective_preset, .params = engine_instrument_default_params(sound->effective_preset)};
    *modified = !daw_preset_sound_equal(&factory, sound->effective_preset, sound->effective_params);
    return engine_instrument_preset_display_name(sound->effective_preset);
}
// Matches a bounded ASCII-insensitive search while leaving UTF-8 label bytes intact.
static bool matches(const char* text, const char* query) {
    size_t n = strlen(query); if (!n) return true;
    for (; *text; ++text) {
        size_t i = 0;
        while (i < n && text[i] && tolower((unsigned char)text[i]) == tolower((unsigned char)query[i])) ++i;
        if (i == n) return true;
    }
    return false;
}
// Computes one bounded browser/save layout shared by rendering, native focus and hit testing.
void daw_preset_panel_layout(const AppState* state, SDL_Rect panel, SDL_Rect header, DawPresetPanelLayout* out) {
    memset(out, 0, sizeof(*out));
    int x = header.x, width = header.w, y = header.y + header.h + 3;
    bool compact = panel.h < 340;
    if (compact) {
        MidiInstrumentPanelLayout base; midi_instrument_panel_compute_layout(state, &base);
        width = base.preset_button_rect.x - x - 6; if (width < 0) width = 0;
        y = header.y + 1;
    }
    out->bar = (SDL_Rect){x, y, width, 28};
    out->status = (SDL_Rect){x, panel.y + panel.h - 22, width, 20};
    int w = (width - 24) / 5; if (w < 0) w = 0; if (w > 150) w = 150;
    SDL_Rect* controls[] = {&out->library_button, &out->save_button, &out->update_button, &out->note_button, &out->chord_button};
    for (int i = 0; i < 5; ++i) *controls[i] = (SDL_Rect){x + i*(w+6), y, w, 25};
    int overlay_y = compact ? header.y + header.h + 6 : y + 31;
    int h = panel.y + panel.h - overlay_y - 10; if (h < 0) h = 0; if (h > 340) h = 340;
    int ow = header.w > 550 ? 550 : header.w;
    out->overlay = (SDL_Rect){x, overlay_y, ow, h};
    out->field = (SDL_Rect){x + 8, out->overlay.y + 26, ow > 16 ? ow - 16 : 0, h >= 58 ? 30 : 0};
    if (state->preset_ui.save_open) {
        out->confirm = (SDL_Rect){x + 8, out->field.y + 40, 110, h >= 100 ? 26 : 0};
        out->cancel = (SDL_Rect){x + 126, out->field.y + 40, 100, h >= 100 ? 26 : 0};
        return;
    }
    out->refresh = (SDL_Rect){x + ow - 94, out->overlay.y + 3, 86, 20};
    int shown = 0;
    for (int i = 0; i < state->preset_ui.library.count; ++i) {
        const DawUserPreset* p = &state->preset_ui.library.items[i];
        const char* category = engine_instrument_preset_category_display_name(engine_instrument_preset_category(p->base));
        if (!matches(p->name, state->preset_ui.search) && !matches(category, state->preset_ui.search)) continue;
        if (shown++ < state->preset_ui.scroll) continue;
        int ry = out->field.y + 36 + out->row_count*27;
        if (out->row_count == 8 || ry + 26 > out->overlay.y + out->overlay.h - 26) break;
        out->item_indices[out->row_count] = i;
        out->rows[out->row_count++] = (SDL_Rect){x + 8, ry, ow - 16, 25};
    }
}
// Draws the existing shared rounded button appearance with synchronous text lifetime.
static void button(SDL_Renderer* renderer, SDL_Rect rect, const char* label, const DawThemePalette* theme, bool active) {
    if (rect.w <= 0 || rect.h <= 0) return;
    DawUiButtonSpec spec; daw_ui_button_spec_init(&spec, label); spec.state.selected = active;
    DawUiButtonStyle style; daw_ui_button_style_resolve(theme, &spec, &style);
    daw_ui_button_draw_frame(renderer, &rect, &style);
    ui_draw_text_clipped(renderer, rect.x + 7, rect.y + (rect.h - ui_font_line_height(.8f))/2,
        label, theme->text_primary, .8f, rect.w - 14);
}
// Renders the library controls and private text overlay over the existing instrument pane.
void daw_preset_panel_render(SDL_Renderer* renderer, const AppState* state, const DawPresetPanelLayout* layout) {
    DawThemePalette theme; daw_shared_theme_resolve_palette(&theme);
    const DawPresetUI* ui = &state->preset_ui;
    button(renderer, layout->library_button, "My Presets", &theme, ui->open);
    button(renderer, layout->save_button, layout->save_button.w < 110 ? "Save As..." : "Save Preset As...", &theme, ui->save_open && !ui->update);
    button(renderer, layout->update_button, layout->update_button.w < 110 ? "Update" : "Update Preset", &theme, ui->save_open && ui->update);
    button(renderer, layout->note_button, layout->note_button.w < 110 ? "C4" : "Audition C4", &theme, false);
    button(renderer, layout->chord_button, layout->chord_button.w < 110 ? "Chord" : "Audition Chord", &theme, false);
    if (!ui->open && !ui->save_open) {
        if (ui->message[0]) {
            SDL_SetRenderDrawColor(renderer, theme.inspector_fill.r, theme.inspector_fill.g, theme.inspector_fill.b, 255);
            SDL_RenderFillRect(renderer, &layout->status);
            ui_draw_text_clipped(renderer, layout->status.x, layout->status.y, ui->message, theme.text_muted, .75f, layout->status.w);
        }
        return;
    }
    SDL_Rect previous_clip; SDL_RenderGetClipRect(renderer, &previous_clip);
    bool clipped = SDL_RenderIsClipEnabled(renderer);
    SDL_Rect clip = layout->overlay;
    ui_set_content_clip_rect(renderer, &clip);
    SDL_SetRenderDrawColor(renderer, theme.inspector_fill.r, theme.inspector_fill.g, theme.inspector_fill.b, 255);
    SDL_RenderFillRect(renderer, &clip);
    SDL_SetRenderDrawColor(renderer, theme.pane_border.r, theme.pane_border.g, theme.pane_border.b, 255);
    SDL_RenderDrawRect(renderer, &clip);
    ui_draw_text_clipped(renderer, clip.x + 8, clip.y + 5,
        ui->save_open ? (ui->update ? "Update library preset" : "Save sound to My Presets") : "My Presets - search name or category",
        theme.text_primary, .85f, clip.w - (ui->save_open ? 16 : 110));
    SDL_Rect field = layout->field;
    SDL_SetRenderDrawColor(renderer, 20, 22, 28, 255); SDL_RenderFillRect(renderer, &field);
    const char* value = ui->save_open ? ui->name : ui->search;
    if (!ui->editing) ui_draw_text_clipped(renderer, field.x + 6, field.y + 6, value[0] ? value : (ui->save_open ? "Preset name" : "Search..."), theme.text_primary, .85f, field.w - 12);
    if (ui->editing) daw_text_edit_draw(renderer, &ui->text_edit, (char*)value, 96, ui->cursor,
        field, theme.text_primary, theme.text_muted, .85f);
    if (ui->save_open) {
        button(renderer, layout->confirm, ui->update ? "Update" : "Save", &theme, false);
        button(renderer, layout->cancel, "Cancel", &theme, false);
        ui_draw_text_clipped(renderer, field.x, field.y + 79, ui->message, theme.text_muted, .75f, field.w);
    } else {
        button(renderer, layout->refresh, "Refresh", &theme, false);
        for (int i = 0; i < layout->row_count; ++i) button(renderer, layout->rows[i], ui->row_labels[layout->item_indices[i]], &theme, i == ui->selected_row);
        ui_draw_text_clipped(renderer, field.x, clip.y + clip.h - 21,
            ui->message[0] ? ui->message : ui->library.count ? "Select to load; use Audition C4 or Chord to listen." : "No saved sounds yet. Use Save Preset As...",
            theme.text_muted, .75f, field.w);
    }
    SDL_RenderSetClipRect(renderer, clipped ? &previous_clip : NULL);
}
