#include "app_state.h"
#include "ui/text_edit.h"
#include "ui/transport_controls.h"
#include "ui/project_modal_controls.h"
#include "input/project_modal_input.h"
#include "input/timeline_input.h"
#include "input/inspector_input.h"
#include "input/inspector_input_numeric_edit.h"
#include "input/library_input.h"
#include "audio/wav_writer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Delivers the application's real key routing without a sampled keyboard/frame delay.
static void key(AppState* state, SDL_Keycode code, unsigned mods) {
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = code; event.key.keysym.mod = mods;
    input_manager_handle_event(&state->input_manager, state, &event);
    event.type = SDL_KEYUP;
    input_manager_handle_event(&state->input_manager, state, &event);
}

// Sends committed or staged text through the current product input owner.
static void text(AppState* state, const char* value, bool composition) {
    SDL_Event event = {.type = composition ? SDL_TEXTEDITING : SDL_TEXTINPUT};
    if (composition) { SDL_strlcpy(event.edit.text,value,sizeof(event.edit.text)); event.edit.length = 1; }
    else SDL_strlcpy(event.text.text,value,sizeof(event.text.text));
    input_manager_handle_event(&state->input_manager,state,&event);
}

// Drives the same pointer press/release path as native modal/transport controls.
static void pointer(AppState* state, unsigned type, int x, int y) {
    SDL_Event event = {.type = type};
    event.button.button = SDL_BUTTON_LEFT; event.button.x = x; event.button.y = y;
    input_manager_handle_event(&state->input_manager,state,&event);
}

// Qualifies each real text owner and modal focus without physical audio or personal data.
int main(void) {
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    AppState* state = calloc(1,sizeof(*state)); assert(state);
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    state->engine = engine_create(&cfg); assert(state->engine);
    input_manager_init(&state->input_manager);
    undo_manager_init(&state->undo);
    state->window_width = 800; state->window_height = 600;
    state->transport_ui.play_rect = (SDL_Rect){10,10,50,20};
    pointer(state,SDL_MOUSEBUTTONDOWN,20,20); pointer(state,SDL_MOUSEBUTTONUP,20,20);
    engine_transport_stop(state->engine);
    uint32_t return_focus = state->transport_ui.controls.interaction.focused_id;
    assert(return_focus);
    project_modal_input_open_save_prompt(state);
    text(state,"Aé🟦",false);
    key(state,SDLK_BACKSPACE,0); assert(!strcmp(state->project_prompt.buffer,"Aé"));
    key(state,SDLK_LEFT,KMOD_SHIFT); text(state,"界",false);
    assert(!strcmp(state->project_prompt.buffer,"A界"));
    key(state,SDLK_a,KMOD_GUI); text(state,"replacement",false);
    assert(!strcmp(state->project_prompt.buffer,"replacement"));
    assert(SDL_SetClipboardText("clipboard") == 0);
    key(state,SDLK_a,KMOD_CTRL); key(state,SDLK_v,KMOD_CTRL);
    assert(!strcmp(state->project_prompt.buffer,"clipboard"));
    key(state,SDLK_a,KMOD_GUI); key(state,SDLK_x,KMOD_GUI);
    assert(!state->project_prompt.buffer[0]);
    char* copied = SDL_GetClipboardText(); assert(copied && !strcmp(copied,"clipboard")); SDL_free(copied);
    key(state,SDLK_v,KMOD_GUI);
    text(state,"é",true); assert(!strcmp(state->project_prompt.buffer,"clipboard"));
    key(state,SDLK_RETURN,0); assert(state->project_prompt.active && state->project_prompt.text_edit.composition[0]);
    key(state,SDLK_ESCAPE,0); assert(state->project_prompt.active && !state->project_prompt.text_edit.composition[0]);
    text(state,"é",true);
    SDL_Event lost = {.type = SDL_WINDOWEVENT}; lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    input_manager_handle_event(&state->input_manager,state,&lost);
    assert(!state->project_prompt.text_edit.composition[0] && state->project_prompt.active);
    // Capacity and malformed UTF-8 reject the whole insertion and retain selection/cursor.
    memset(state->project_prompt.buffer,'x',sizeof(state->project_prompt.buffer)-2);
    state->project_prompt.buffer[sizeof(state->project_prompt.buffer)-2] = 0;
    state->project_prompt.cursor = (int)strlen(state->project_prompt.buffer);
    daw_text_edit_begin(&state->project_prompt.text_edit,state->project_prompt.buffer,
        sizeof(state->project_prompt.buffer),&state->project_prompt.cursor,KIT_UI_TEXT_SINGLE_LINE);
    char before[SESSION_NAME_MAX]; memcpy(before,state->project_prompt.buffer,sizeof(before));
    text(state,"é",false); assert(!memcmp(before,state->project_prompt.buffer,sizeof(before)));
    text(state,"\xc3",false); assert(!memcmp(before,state->project_prompt.buffer,sizeof(before)));
    key(state,SDLK_r,0); key(state,SDLK_SPACE,0);
    assert(!engine_transport_requested_playing(state->engine));
    key(state,SDLK_ESCAPE,0);
    KitUiSurfaceKey restored;
    assert(kit_ui_surface_key(&state->transport_ui.controls,
        state->transport_ui.controls.interaction.focused_id, &restored));
    assert(restored.domain == 1 && restored.value == DAW_TRANSPORT_PLAY);
    // Modal release outside, changed geometry and changed selection cannot load.
    state->project_load.active = true; state->project_load.count = 1; state->project_load.selected_index = 0;
    SDL_strlcpy(state->project_load.entries[0].path,"/no-such-shared-ui-project.json",SESSION_PATH_MAX);
    SDL_Rect load,cancel; daw_project_modal_buttons(state,&load,&cancel);
    pointer(state,SDL_MOUSEBUTTONDOWN,load.x+10,load.y+10); assert(!state->project_load.error[0]);
    pointer(state,SDL_MOUSEBUTTONUP,0,0); assert(!state->project_load.error[0]);
    pointer(state,SDL_MOUSEBUTTONDOWN,load.x+10,load.y+10);
    state->window_width += 100; daw_project_modal_buttons(state,&load,&cancel);
    pointer(state,SDL_MOUSEBUTTONUP,load.x+10,load.y+10); assert(!state->project_load.error[0]);
    pointer(state,SDL_MOUSEBUTTONDOWN,load.x+10,load.y+10); state->project_load.selected_index = -1;
    pointer(state,SDL_MOUSEBUTTONUP,load.x+10,load.y+10); assert(!state->project_load.error[0]);
    state->project_load.selected_index = 0;
    key(state,SDLK_TAB,0);
    SDL_Event enter = {.type=SDL_KEYDOWN}; enter.key.keysym.sym = SDLK_RETURN;
    input_manager_handle_event(&state->input_manager,state,&enter); assert(!state->project_load.error[0]);
    enter.key.repeat = 1; input_manager_handle_event(&state->input_manager,state,&enter);
    assert(!state->project_load.error[0]); enter.type = SDL_KEYUP;
    input_manager_handle_event(&state->input_manager,state,&enter);
    assert(state->project_load.active && state->project_load.error[0]);
    pointer(state,SDL_MOUSEBUTTONDOWN,cancel.x+10,cancel.y+10); assert(state->project_load.active);
    pointer(state,SDL_MOUSEBUTTONUP,cancel.x+10,cancel.y+10); assert(!state->project_load.active);
    // A modal suspends platform text delivery, clears staging and restores its surviving field.
    state->track_name_editor.editing = true;
    strcpy(state->track_name_editor.buffer,"Pending track"); state->track_name_editor.cursor=13;
    daw_text_edit_begin(&state->track_name_editor.text_edit,state->track_name_editor.buffer,
        sizeof(state->track_name_editor.buffer),&state->track_name_editor.cursor,KIT_UI_TEXT_SINGLE_LINE);
    SDL_StartTextInput(); text(state,"é",true);
    assert(state->track_name_editor.text_edit.composition[0]);
    (void)project_modal_input_open_load_modal(state);
    assert(!state->track_name_editor.text_edit.composition[0] && !SDL_IsTextInputActive());
    project_modal_input_close_load_modal(state);
    assert(SDL_IsTextInputActive() && !strcmp(state->track_name_editor.buffer,"Pending track"));
    state->track_name_editor.editing=false;
    // Track commit/cancel and actual library filesystem rename retain product meaning.
    track_name_editor_start(state,0); key(state,SDLK_a,KMOD_GUI); text(state,"Track é",false);
    key(state,SDLK_RETURN,0); assert(!strcmp(engine_get_tracks(state->engine)[0].name,"Track é"));
    track_name_editor_start(state,0); key(state,SDLK_a,KMOD_CTRL); text(state,"Canceled",false);
    key(state,SDLK_ESCAPE,0); assert(!strcmp(engine_get_tracks(state->engine)[0].name,"Track é"));
    char directory[]="/tmp/sonics-shared-text-XXXXXX"; assert(mkdtemp(directory));
    char old_path[512],new_path[512]; snprintf(old_path,sizeof(old_path),"%s/old.wav",directory);
    snprintf(new_path,sizeof(new_path),"%s/renamed-é.wav",directory);
    float silence[32]={0}; assert(wav_write_pcm16(old_path,silence,32,1,48000));
    LibraryBrowser* library = &state->library;
    SDL_strlcpy(library->directory,directory,sizeof(library->directory)); library->count=1;
    SDL_strlcpy(library->items[0].name,"old.wav",sizeof(library->items[0].name));
    library->editing=true; library->edit_index=0; SDL_strlcpy(library->edit_buffer,"old.wav",sizeof(library->edit_buffer));
    library->edit_cursor=7; daw_text_edit_begin(&library->text_edit,library->edit_buffer,
        sizeof(library->edit_buffer),&library->edit_cursor,KIT_UI_TEXT_SINGLE_LINE);
    key(state,SDLK_a,KMOD_GUI); text(state,"renamed-é.wav",false); key(state,SDLK_RETURN,0);
    assert(!library->editing && access(old_path,F_OK)!=0 && access(new_path,F_OK)==0);
    unlink(new_path); rmdir(directory);
    // Inspector name and numeric rejection/commit use their existing clip/undo owners.
    assert(engine_add_midi_clip_to_track(state->engine,0,0,48000,NULL));
    inspector_input_show(state,0,0,&engine_get_tracks(state->engine)[0].clips[0]); inspector_input_begin_rename(state);
    key(state,SDLK_a,KMOD_GUI); text(state,"Clip é",false); key(state,SDLK_RETURN,0);
    assert(!state->inspector.editing_name && !strcmp(engine_get_tracks(state->engine)[0].clips[0].name,"Clip é"));
    const EngineClip* clip = &engine_get_tracks(state->engine)[0].clips[0];
    inspector_numeric_begin_edit(state,clip,&state->inspector.edit.editing_timeline_start);
    key(state,SDLK_a,KMOD_GUI); text(state,"invalid",false); key(state,SDLK_RETURN,0);
    assert(state->inspector.edit.editing_timeline_start && !strcmp(state->inspector.edit.timeline_start,"invalid"));
    key(state,SDLK_a,KMOD_CTRL); text(state,"0.25",false); key(state,SDLK_RETURN,0);
    assert(!state->inspector.edit.editing_timeline_start && engine_get_tracks(state->engine)[0].clips[0].timeline_start_frames==12000);
    state->inspector.visible=false;
    // Both tempo fields preserve their app-owned apply/range policies.
    state->tempo_ui.editing=true; state->tempo_ui.focus=TEMPO_FOCUS_BPM;
    strcpy(state->tempo_ui.buffer,"120"); state->tempo_ui.cursor=3;
    daw_text_edit_begin(&state->tempo_ui.text_edit,state->tempo_ui.buffer,sizeof(state->tempo_ui.buffer),
        &state->tempo_ui.cursor,KIT_UI_TEXT_SINGLE_LINE|DAW_TEXT_DECIMAL);
    key(state,SDLK_a,KMOD_GUI); text(state,"90.5",false); assert(!strcmp(state->tempo_ui.buffer,"90.5"));
    text(state,"x",false); assert(!strcmp(state->tempo_ui.buffer,"90.5"));
    key(state,SDLK_ESCAPE,0); assert(!state->tempo_ui.editing);
    state->tempo_ui.editing=true; state->tempo_ui.focus=TEMPO_FOCUS_TS; state->tempo_ui.ts_part=TEMPO_TS_PART_DEN;
    strcpy(state->tempo_ui.ts_buffer,"4"); state->tempo_ui.ts_cursor=1;
    daw_text_edit_begin(&state->tempo_ui.text_edit,state->tempo_ui.ts_buffer,sizeof(state->tempo_ui.ts_buffer),
        &state->tempo_ui.ts_cursor,KIT_UI_TEXT_SINGLE_LINE|KIT_UI_TEXT_DIGITS);
    key(state,SDLK_a,KMOD_GUI); text(state,"0",false); key(state,SDLK_RETURN,0);
    assert(state->tempo_ui.editing); // Nonpositive denominator is retained for correction.
    key(state,SDLK_ESCAPE,0);
    engine_destroy(state->engine); free(state); SDL_Quit();
    puts("shared_text_focus_test: success (six real owners, UTF-8, clipboard, preedit, modal focus/cancellation and product commits)");
    return 0;
}
