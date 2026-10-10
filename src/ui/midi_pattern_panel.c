#include "ui/midi_pattern_panel.h"
#include "daw/midi_expression.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "ui/daw_ui_button.h"
#include "ui/render_utils.h"
#include "ui/font.h"
#include "app_state.h"
#include <math.h>
#include <string.h>
// Captures the currently selected MIDI region without retaining its array position.
static bool selected_target(AppState* state, DawActionTarget* out) {
    MidiEditorSelection selection;
    return midi_editor_get_selection(state, &selection) && daw_instrument_target_capture(state, selection.track_index, selection.clip_index, out) == DAW_ACTION_UNCHANGED;
}
// Checks that an open pattern popup still addresses the selected project-owned region.
static bool matches(DawActionTarget a, DawActionTarget b) {
    return a.project_generation == b.project_generation && a.track_id == b.track_id && a.clip_id == b.clip_id;
}
// Computes popup geometry inside the editor and leaves its existing piano/tempo layout intact.
void daw_midi_pattern_layout(const AppState* state, DawMidiPatternLayout* out) {
    memset(out, 0, sizeof(*out));
    MidiEditorLayout base; midi_editor_compute_layout(state, &base);
    out->entry = (SDL_Rect){base.footer_rect.x + base.footer_rect.w - 82, base.footer_rect.y, 80, base.footer_rect.h};
    int width = base.panel_rect.w - 16;
    if (width > 620) width = 620;
    int height = base.panel_rect.h - base.header_rect.h - 12;
    if (height > 165) height = 165;
    out->panel = (SDL_Rect){base.panel_rect.x + base.panel_rect.w - width - 8, base.panel_rect.y + base.panel_rect.h - height - 6, width, height};
    int button_width = (width - 24) / 3;
    for (int i = 0; i < 3; ++i) out->buttons[i] = (SDL_Rect){out->panel.x + 6 + i * (button_width + 5), out->panel.y + 26, button_width, 24};
    out->buttons[3] = (SDL_Rect){out->panel.x + 6, out->panel.y + 55, width - 100, 24};
    for(int i=0;i<3;++i)out->buttons[5+i]=(SDL_Rect){out->panel.x+6+i*(button_width+5),out->panel.y+84,button_width,24};
    out->buttons[4] = (SDL_Rect){out->panel.x + width - 86, out->panel.y + 55, 80, 24};
}
// Uses the same rounded shared button tokens as existing product instrument controls.
static void draw_button(SDL_Renderer* renderer, SDL_Rect rect, const char* label, const DawThemePalette* theme, bool active) {
    DawUiButtonSpec spec; daw_ui_button_spec_init(&spec, label); spec.state.selected = active;
    DawUiButtonStyle style; daw_ui_button_style_resolve(theme, &spec, &style);
    daw_ui_button_draw_frame(renderer, &rect, &style);
    ui_draw_text_clipped(renderer, rect.x + 5, rect.y + 5, label, theme->text_primary, .75f, rect.w - 10);
}
// Displays an explicit append/replace choice and the latest transaction outcome with stable text storage.
void daw_midi_pattern_render(SDL_Renderer* renderer, AppState* state) {
    if (!midi_editor_should_render(state) || midi_instrument_panel_should_render(state) || state->midi_editor_ui.instrument_menu_open) return;
    DawMidiPatternLayout layout; daw_midi_pattern_layout(state, &layout);
    DawThemePalette theme; daw_shared_theme_resolve_palette(&theme);
    DawMidiPatternUI* ui = &state->midi_pattern_ui;
    draw_button(renderer, layout.entry, layout.panel.h<140 || layout.panel.w<300 ? "Enlarge pane" : "Patterns", &theme, ui->open);
    if (!ui->open || layout.panel.h<140 || layout.panel.w<300) return;
    SDL_Rect previous; SDL_RenderGetClipRect(renderer, &previous); bool clipped = SDL_RenderIsClipEnabled(renderer);
    ui_set_content_clip_rect(renderer, &layout.panel);
    SDL_SetRenderDrawColor(renderer, theme.inspector_fill.r, theme.inspector_fill.g, theme.inspector_fill.b, 255);
    SDL_RenderFillRect(renderer, &layout.panel);
    SDL_SetRenderDrawColor(renderer, theme.pane_border.r, theme.pane_border.g, theme.pane_border.b, 255);
    SDL_RenderDrawRect(renderer, &layout.panel);
    ui_draw_text_clipped(renderer, layout.panel.x + 6, layout.panel.y + 5, "C major: four quarter-note beats, velocity 0.65", theme.text_primary, .8f, layout.panel.w - 12);
    const char* names[] = {"Melody", "I - vi - IV - V", "Arpeggio", ui->replace ? "Replace all notes from region start" : "Append after existing notes", "Close", "Quantize eighths", "Swing eighths", "Vary velocity"};
    for (int i = 0; i < 8; ++i) draw_button(renderer, layout.buttons[i], names[i], &theme, i == 3 && ui->replace);
    ui_draw_text_clipped(renderer, layout.panel.x + 6, layout.panel.y + 113, ui->message, theme.text_muted, .75f, layout.panel.w - 12);
    ui_draw_text_clipped(renderer, layout.panel.x + 6, layout.panel.y + 137, "Patterns and expression each use one undo step; note lengths stay fixed.", theme.text_muted, .7f, layout.panel.w - 12);
    SDL_RenderSetClipRect(renderer, clipped ? &previous : NULL);
}
// Resolves fresh notes, generates against that captured timing and publishes one guarded transaction.
bool daw_midi_pattern_activate(AppState* state, int x, int y) {
    if (!state || !midi_editor_should_render(state) || midi_instrument_panel_should_render(state) || state->midi_editor_ui.instrument_menu_open) return false;
    DawMidiPatternLayout layout; daw_midi_pattern_layout(state, &layout); SDL_Point point = {x, y};
    DawMidiPatternUI* ui = &state->midi_pattern_ui;
    if (layout.panel.h<140 || layout.panel.w<300) {ui->open=false;return false;}
    DawActionTarget current;
    if (!selected_target(state, &current)) return false;
    if (!ui->open) {
        if (!SDL_PointInRect(&point, &layout.entry)) return false;
        ui->target = current; ui->open = true; ui->replace = false; ui->pressed=-1;
        SDL_strlcpy(ui->message, "Choose a pattern; append preserves existing notes.", sizeof(ui->message)); return true;
    }
    if (!matches(current, ui->target)) { ui->open = false; return false; }
    for (int i = 0; i < 8; ++i) if (SDL_PointInRect(&point, &layout.buttons[i])) {
        if (i == 4) ui->open = false;
        else if (i == 3) ui->replace = !ui->replace;
        else if(i>=5){
            DawMidiSnapshot snapshot={0};DawMidiResult outcome=daw_action_get_midi(state,ui->target,&snapshot);
            if(outcome.status==DAW_MIDI_UNCHANGED){
                DawMidiExpression expression=i==7?(DawMidiExpression){.kind=DAW_MIDI_HUMANIZE,.velocity_amount=.08,.seed=123}:
                    (DawMidiExpression){.kind=DAW_MIDI_QUANTIZE,.grid_beats=.5,.strength=1,.swing=i==6?.3333333333333333:0};
                outcome=daw_action_transform_midi(state,&snapshot,expression);
            }
            SDL_strlcpy(ui->message,outcome.message,sizeof(ui->message));daw_midi_snapshot_free(&snapshot);
        }
        else {
            DawMidiSnapshot snapshot = {0}; DawMidiPatternNotes notes;
            DawMidiResult outcome = daw_action_get_midi(state, ui->target, &snapshot);
            if (outcome.status == DAW_MIDI_UNCHANGED) {
                double start = 0;
                if (!ui->replace && snapshot.note_count) {
                    uint64_t end = 0;
                    for (int n = 0; n < snapshot.note_count; ++n) {
                        uint64_t finish = snapshot.notes[n].start_frame + snapshot.notes[n].duration_frames;
                        if (finish > end) end = finish;
                    }
                    if (snapshot.start_frame > INT64_MAX || end > INT64_MAX - snapshot.start_frame) start = INFINITY;
                    else start = ceil(tempo_map_samples_to_beats(&snapshot.tempo, (int64_t)(snapshot.start_frame + end)) - tempo_map_samples_to_beats(&snapshot.tempo, (int64_t)snapshot.start_frame) - 1e-9);
                }
                outcome = daw_midi_pattern_build(&snapshot, (DawMidiPattern){(DawMidiPatternKind)i, 60, start, .65f}, &notes);
                if (outcome.status == DAW_MIDI_UNCHANGED) outcome = daw_action_edit_midi(state, &(DawMidiRequest){&snapshot, notes.notes, notes.count, ui->replace ? DAW_MIDI_REPLACE : DAW_MIDI_APPEND});
            }
            SDL_strlcpy(ui->message, outcome.message, sizeof(ui->message)); daw_midi_snapshot_free(&snapshot);
        }
        return true;
    }
    return SDL_PointInRect(&point, &layout.panel);
}
// Owns popup pointer/escape events while leaving global modifiers and active keyboard performances alone.
bool daw_midi_pattern_event(AppState* state, const SDL_Event* event) {
    if (!state || !event) return false;
    if (!midi_editor_should_render(state) || midi_instrument_panel_should_render(state) || state->midi_editor_ui.instrument_menu_open) { state->midi_pattern_ui.open = false; return false; }
    if (!state->midi_pattern_ui.open) return false;
    DawMidiPatternLayout layout;daw_midi_pattern_layout(state,&layout);
    if(layout.panel.h<140 || layout.panel.w<300){state->midi_pattern_ui.open=false;return false;}
    DawActionTarget current;
    if (!selected_target(state, &current) || !matches(current, state->midi_pattern_ui.target)) { state->midi_pattern_ui.open = false; return false; }
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) { state->midi_pattern_ui.open = false; return true; }
    if(event->type==SDL_WINDOWEVENT || event->type==SDL_MOUSEWHEEL){state->midi_pattern_ui.pressed=-1;return false;}
    if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT){
        state->midi_pattern_ui.pressed=-1;SDL_Point point={event->button.x,event->button.y};
        if(SDL_PointInRect(&point,&layout.panel))for(int i=0;i<8;++i)if(SDL_PointInRect(&point,&layout.buttons[i]))state->midi_pattern_ui.pressed=i;
        return true;
    }
    if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT){
        int pressed=state->midi_pattern_ui.pressed;state->midi_pattern_ui.pressed=-1;SDL_Point point={event->button.x,event->button.y};
        if(pressed>=0 && SDL_PointInRect(&point,&layout.panel) && SDL_PointInRect(&point,&layout.buttons[pressed]))daw_midi_pattern_activate(state,point.x,point.y);
        return true;
    }
    return false;
}
