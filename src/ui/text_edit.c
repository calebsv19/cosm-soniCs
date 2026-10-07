#include "ui/text_edit.h"
#include "app_state.h"
#include <string.h>

// Binds current storage and reconciles cursor changes made by existing mouse owners.
static CoreResult text_sync(KitUiTextEdit* edit, char* text, size_t capacity,
                            int* cursor, unsigned flags) {
    CoreResult result = kit_ui_text_bind(edit, text, capacity, flags & 3u);
    if (result.code != CORE_OK) return result;
    size_t position = *cursor < 0 ? 0 : (size_t)*cursor;
    if (position != edit->cursor) {
        result = kit_ui_text_position(edit, position, position);
    }
    *cursor = (int)edit->cursor;
    return result;
}

void daw_text_edit_begin(KitUiTextEdit* edit, char* text, size_t capacity,
                          int* cursor, unsigned flags) {
    if (!edit || !cursor) return;
    memset(edit, 0, sizeof(*edit));
    (void)text_sync(edit, text, capacity, cursor, flags);
}

void daw_text_edit_position(KitUiTextEdit* edit, char* text, size_t capacity,
                            int* cursor, int position, unsigned flags) {
    if (!edit || !cursor) return;
    if (text_sync(edit, text, capacity, cursor, flags).code != CORE_OK) return;
    (void)kit_ui_text_position(edit, position < 0 ? 0 : (size_t)position,
                               position < 0 ? 0 : (size_t)position);
    *cursor = (int)edit->cursor;
}

KitUiTextEventResult daw_text_edit_event(KitUiTextEdit* edit, char* text,
    size_t capacity, int* cursor, unsigned flags, const SDL_Event* event) {
    KitUiTextEventResult result = {0};
    if (!edit || !cursor || !event) return result;
    result.status = text_sync(edit, text, capacity, cursor, flags);
    if (result.status.code != CORE_OK) { result.consumed = 1; return result; }
    KitUiTextEdit before = *edit;
    char decimal_before[32];
    bool decimal = (flags & DAW_TEXT_DECIMAL) != 0;
    if (decimal && capacity > sizeof(decimal_before)) {
        result.consumed = 1;
        result.status = (CoreResult){CORE_ERR_INVALID_ARG, "invalid decimal edit capacity"};
        return result;
    }
    if (decimal) memcpy(decimal_before, text, capacity);
    result = kit_ui_text_event_sdl(edit, event);
    if (decimal && result.changed) {
        const char* dot = strchr(text, '.');
        if (strspn(text, "0123456789.") != strlen(text) || (dot && strchr(dot + 1, '.'))) {
            memcpy(text, decimal_before, capacity);
            *edit = before;
            result.changed = 0;
            result.status = (CoreResult){CORE_ERR_INVALID_ARG, "tempo requires a decimal number"};
        }
    }
    *cursor = (int)edit->cursor;
    return result;
}

void daw_text_cancel_composition(AppState* state) {
    if (!state) return;
    KitUiTextEdit* edits[] = {&state->project_prompt.text_edit,
        &state->tempo_ui.text_edit, &state->library.text_edit,
        &state->track_name_editor.text_edit, &state->inspector.name_text_edit,
        &state->inspector.edit.text_edit};
    for (size_t i = 0; i < sizeof(edits) / sizeof(edits[0]); ++i)
        kit_ui_text_cancel_composition(edits[i]);
}

void daw_text_resume_input(AppState* state) {
    if (!state) return;
    ClipInspectorEditState* edit = &state->inspector.edit;
    bool numeric = edit->editing_timeline_start || edit->editing_timeline_end ||
        edit->editing_timeline_length || edit->editing_source_start ||
        edit->editing_source_end || edit->editing_playback_rate;
    if (state->project_prompt.active || (!state->project_load.active &&
        (state->tempo_ui.editing || state->library.editing || state->track_name_editor.editing ||
         state->inspector.editing_name || numeric))) SDL_StartTextInput();
    else SDL_StopTextInput();
}
