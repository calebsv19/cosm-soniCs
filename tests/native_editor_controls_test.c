#include "daw/automation_actions.h"
#include "ui/automation_menu.h"
#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "engine/engine_internal.h"
#include "daw/preset_actions.h"
#include "ui/preset_panel.h"
#include "ui/fx_library_panel.h"
#include "ui/routing_panel.h"
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "ui/editor_controls.h"
#include "ui/layout.h"
#include "ui/render_utils.h"
#include "ui/font.h"
#include "ui/midi_editor.h"
#include "input/midi_editor_input_internal.h"
#include "ui/midi_instrument_panel.h"
#include "ui/effects_panel.h"
#include "input/effects_panel_input.h"
#include "input/timeline_selection.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Captures the actual product painters with shared focus and validation readback.
static void capture(VkRenderer* renderer, AppState* state, const char* prefix, const char* name) {
    char path[4096]; snprintf(path,sizeof(path),"%s-%s.bmp",prefix,name);
    assert(vk_renderer_request_capture(renderer,path)==VK_SUCCESS);
    VkCommandBuffer cmd; VkFramebuffer fb; VkExtent2D extent;
    assert(vk_renderer_begin_frame(renderer,&cmd,&fb,&extent)==VK_SUCCESS);
    vk_renderer_set_logical_size(renderer,1600,1000);
    ui_render_panes((SDL_Renderer*)renderer,state);
    ui_render_controls((SDL_Renderer*)renderer,state);
    ui_render_overlays((SDL_Renderer*)renderer,state);
    assert(vk_renderer_end_frame(renderer,cmd)==VK_SUCCESS); vk_renderer_wait_idle(renderer);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&renderer->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
    assert(renderer->draw_state.draw_call_count>20);
}
// Captures a sentinel frame to prove every product pass respects an enclosing native clip.
static void containment(VkRenderer* renderer, AppState* state, const char* prefix) {
    char path[4096]; snprintf(path,sizeof(path),"%s-pane-clip.bmp",prefix);
    assert(vk_renderer_request_capture(renderer,path)==VK_SUCCESS);
    VkCommandBuffer cmd; VkFramebuffer fb; VkExtent2D extent;
    assert(vk_renderer_begin_frame(renderer,&cmd,&fb,&extent)==VK_SUCCESS);
    vk_renderer_set_logical_size(renderer,1600,1000);
    SDL_Renderer* native=(SDL_Renderer*)renderer;
    SDL_Rect window={0,0,1600,1000}, clip={600,650,500,200}, after;
    SDL_SetRenderDrawColor(native,31,191,79,255); SDL_RenderFillRect(native,&window);
    ui_set_clip_rect(native,&clip);
    ui_render_panes(native,state); ui_get_clip_rect(native,&after); assert(!memcmp(&after,&clip,sizeof(clip)));
    ui_render_controls(native,state); ui_get_clip_rect(native,&after); assert(!memcmp(&after,&clip,sizeof(clip)));
    ui_render_overlays(native,state); ui_get_clip_rect(native,&after); assert(!memcmp(&after,&clip,sizeof(clip)));
    ui_set_clip_rect(native,NULL);
    assert(vk_renderer_end_frame(renderer,cmd)==VK_SUCCESS); vk_renderer_wait_idle(renderer);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&renderer->context.device->runtime);
    assert(report && !report->validation_warning_count && !report->validation_error_count);
}
// Sets focus through application input without applying a release action.
static void focus(AppState* state, unsigned domain, uint64_t key) {
    daw_editor_controls_sync(state); KitUiSurface* s=&state->editor_controls.surface;
    assert(s->collection_result.code==CORE_OK);
    for(unsigned i=0;i<s->count;++i) if(s->keys[i].domain==domain && s->keys[i].value==key) {
        KitRenderRect r=s->controls[i].bounds; SDL_Event down={.type=SDL_MOUSEBUTTONDOWN};
        down.button.button=SDL_BUTTON_LEFT; down.button.x=r.x+r.width/2; down.button.y=r.y+r.height/2;
        input_manager_handle_event(&state->input_manager,state,&down);
        SDL_Event up=down; up.type=SDL_MOUSEBUTTONUP; up.button.x=-100; up.button.y=-100;
        input_manager_handle_event(&state->input_manager,state,&up); return;
    }
    assert(!"native control missing");
}
// Activates a collected product control only with a matching application-owned release.
static void activate_control(AppState* state,unsigned domain,uint64_t key) {
    daw_editor_controls_sync(state);KitUiSurface* surface=&state->editor_controls.surface;
    for(unsigned i=0;i<surface->count;++i)if(surface->keys[i].domain==domain && surface->keys[i].value==key){
        KitRenderRect r=surface->controls[i].bounds;SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;e.button.x=r.x+r.width/2;e.button.y=r.y+r.height/2;
        input_manager_handle_event(&state->input_manager,state,&e);e.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&e);return;
    }
    assert(!"native control missing");
}
// Retires only regular fixture files within a task-owned library directory.
static void retire_library_directory(const char* root) {
    DIR* dir=opendir(root);assert(dir);struct dirent* entry;
    while((entry=readdir(dir))){if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        char path[2048];snprintf(path,sizeof(path),"%s/%s",root,entry->d_name);struct stat metadata;assert(!lstat(path,&metadata));
        if(S_ISDIR(metadata.st_mode))retire_library_directory(path);else{assert(S_ISREG(metadata.st_mode));assert(!unlink(path));}
    }
    closedir(dir);assert(!rmdir(root));
}
// Runs finite native frames without personal projects or audio device activation.
int main(int argc,char** argv) {
    assert(argc==2); assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) && !TTF_Init());
    SDL_Window* window=SDL_CreateWindow("Sonics editor controls qualification",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        1600,1000,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI); assert(window);
    VkRenderer renderer={0}; VkRendererConfig config; vk_renderer_config_set_defaults(&config); config.enable_validation=SDL_TRUE;
    assert(vk_renderer_init(&renderer,window,&config)==VK_SUCCESS);
    assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    AppState* state=calloc(1,sizeof(*state)); assert(state); EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    state->tempo=tempo_state_default(cfg.sample_rate);tempo_map_init(&state->tempo_map,cfg.sample_rate);time_signature_map_init(&state->time_signature_map);
    state->engine=engine_create(&cfg); assert(state->engine); state->runtime_cfg=cfg;
    undo_manager_init(&state->undo); input_manager_init(&state->input_manager);
    state->active_track_index=0; state->selected_track_index=-1; state->selected_clip_index=-1;
    ui_init_panes(state); state->layout_runtime.mixer_ratio=0.55f; ui_layout_panes(state,1600,1000); effects_panel_input_init(state);
    assert(undo_manager_add_effect(state,-1,1)); effects_panel_sync_from_engine(state);
    char library[]="/tmp/daw-preset-native-XXXXXX";assert(mkdtemp(library));assert(!setenv("DAW_PRESET_LIBRARY",library,1));
    activate_control(state,14,5);assert(state->fx_library_ui.open && !state->fx_library_ui.captured.track_id);
    capture(&renderer,state,argv[1],"fx-chain-browser-empty");containment(&renderer,state,argv[1]);
    DawFxLibraryLayout fx_library;daw_fx_library_panel_layout(state,&fx_library);
    SDL_Event fx_click={.type=SDL_MOUSEBUTTONDOWN};fx_click.button.button=SDL_BUTTON_LEFT;fx_click.button.x=fx_library.buttons[0].x+5;fx_click.button.y=fx_library.buttons[0].y+5;
    input_manager_handle_event(&state->input_manager,state,&fx_click);fx_click.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&fx_click);assert(state->fx_library_ui.saving);
    SDL_Event fx_text={.type=SDL_TEXTINPUT};SDL_strlcpy(fx_text.text.text,"Native Warmth",sizeof(fx_text.text.text));input_manager_handle_event(&state->input_manager,state,&fx_text);
    capture(&renderer,state,argv[1],"fx-chain-save-form");
    SDL_Event fx_enter={.type=SDL_KEYDOWN};fx_enter.key.keysym.sym=SDLK_RETURN;input_manager_handle_event(&state->input_manager,state,&fx_enter);
    assert(!state->fx_library_ui.saving && state->fx_library_ui.library->count==1);
    capture(&renderer,state,argv[1],"fx-chain-browser-saved");containment(&renderer,state,argv[1]);
    fx_enter.key.keysym.sym=SDLK_ESCAPE;input_manager_handle_event(&state->input_manager,state,&fx_enter);assert(!state->fx_library_ui.open);

    activate_control(state,14,6); assert(state->routing_ui.open);
    DawRoutingLayout routing; daw_routing_panel_layout(state,&routing); assert(routing.editable);
    SDL_Event route_click={.type=SDL_MOUSEBUTTONDOWN}; route_click.button.button=SDL_BUTTON_LEFT;
    route_click.button.x=routing.buttons[2].x+5; route_click.button.y=routing.buttons[2].y+5;
    input_manager_handle_event(&state->input_manager,state,&route_click); route_click.type=SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&route_click);
    assert(state->routing_ui.staged_return); capture(&renderer,state,argv[1],"routing-return");
    route_click.type=SDL_MOUSEBUTTONDOWN; route_click.button.x=routing.buttons[0].x+5; route_click.button.y=routing.buttons[0].y+5;
    input_manager_handle_event(&state->input_manager,state,&route_click); route_click.type=SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&route_click);
    route_click.type=SDL_MOUSEBUTTONDOWN; route_click.button.x=routing.destinations[0].x+5; route_click.button.y=routing.destinations[0].y+5;
    input_manager_handle_event(&state->input_manager,state,&route_click); route_click.type=SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&route_click);
    route_click.type=SDL_MOUSEBUTTONDOWN; route_click.button.x=routing.gains[0].x+5; route_click.button.y=routing.gains[0].y+5;
    input_manager_handle_event(&state->input_manager,state,&route_click); route_click.type=SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&route_click);
    SDL_Event route_key={.type=SDL_KEYDOWN}; route_key.key.keysym.sym=SDLK_a; route_key.key.keysym.mod=KMOD_CTRL;
    input_manager_handle_event(&state->input_manager,state,&route_key);
    SDL_Event route_text={.type=SDL_TEXTINPUT}; SDL_strlcpy(route_text.text.text,"50",sizeof(route_text.text.text)); input_manager_handle_event(&state->input_manager,state,&route_text);
    route_key.key.keysym.sym=SDLK_RETURN; route_key.key.keysym.mod=KMOD_NONE; input_manager_handle_event(&state->input_manager,state,&route_key);
    assert(state->routing_ui.captured.tracks[state->routing_ui.selected].sends[0].gain==.5f);
    capture(&renderer,state,argv[1],"routing-source-send"); containment(&renderer,state,argv[1]);
    route_key.key.keysym.sym=SDLK_ESCAPE; input_manager_handle_event(&state->input_manager,state,&route_key); assert(!state->routing_ui.open);

    containment(&renderer,state,argv[1]);
    focus(state,11,4); capture(&renderer,state,argv[1],"effects-stack");
    state->effects_panel.view_mode=FX_PANEL_VIEW_LIST; focus(state,10,1); capture(&renderer,state,argv[1],"effects-list");
    timeline_selection_set_single(state,0,-1); effects_panel_sync_from_engine(state);
    focus(state,16,1); capture(&renderer,state,argv[1],"track-snapshot");
    state->effects_panel.track_snapshot.instrument_menu_open=true;
    EffectsPanelLayout snapshot; effects_panel_compute_layout(state,&snapshot);
    assert(snapshot.track_snapshot.instrument_browser.row_count);
    focus(state,16,1000+snapshot.track_snapshot.instrument_browser.rows[0].category);
    capture(&renderer,state,argv[1],"snapshot-preset-menu");
    state->effects_panel.track_snapshot.instrument_menu_open=false;
    state->effects_panel.list_detail_mode=FX_LIST_DETAIL_EQ;
    state->effects_panel.track_snapshot.eq_open=true;
    focus(state,17,2); capture(&renderer,state,argv[1],"eq-detail");
    state->effects_panel.track_snapshot.eq_open=false;
    const unsigned meters[]={102,104,105};
    const char* meter_names[]={"scope-detail","lufs-detail","spectrogram-detail"};
    for(unsigned i=0;i<3;++i) {
        FxInstId id=undo_manager_add_effect(state,0,meters[i]); assert(id);
        effects_panel_sync_from_engine(state);
        state->effects_panel.list_open_slot_index=state->effects_panel.chain_count-1;
        state->effects_panel.list_detail_mode=FX_LIST_DETAIL_METER;
        focus(state,18,(uint64_t)id*128+101); capture(&renderer,state,argv[1],meter_names[i]);
    }
    state->effects_panel.view_mode=FX_PANEL_VIEW_STACK;
    FxInstId spectrogram=state->effects_panel.chain[state->effects_panel.chain_count-1].id;
    focus(state,18,(uint64_t)spectrogram*128+102); capture(&renderer,state,argv[1],"spectrogram-rack");
    while(state->effects_panel.chain_count) {
        assert(engine_fx_track_remove(state->engine,0,state->effects_panel.chain[0].id));
        effects_panel_sync_from_engine(state);
    }
    assert(undo_manager_add_effect(state,0,4));
    assert(undo_manager_add_effect(state,0,30));
    FxInstId delay=undo_manager_add_effect(state,0,50); assert(delay);
    effects_panel_sync_from_engine(state); state->effects_panel.spec_panel_enabled=true;
    focus(state,18,(uint64_t)delay*128+1); capture(&renderer,state,argv[1],"spec-widgets");
    while(state->effects_panel.chain_count) {
        assert(engine_fx_track_remove(state->engine,0,state->effects_panel.chain[0].id));effects_panel_sync_from_engine(state);
    }
    assert(undo_manager_add_effect(state,0,50));
    assert(undo_manager_add_effect(state,0,105));effects_panel_sync_from_engine(state);
    state->effects_panel.view_mode=FX_PANEL_VIEW_STACK;state->effects_panel.spec_panel_enabled=true;
    capture(&renderer,state,argv[1],"delay-spectrogram-rack");
    int clip=-1; assert(engine_add_midi_clip_to_track(state->engine,0,0,120000,&clip));
    assert(engine_clip_midi_add_note(state->engine,0,clip,(EngineMidiNote){0,12000,60,0.8f},NULL));
    timeline_selection_set_single(state,0,clip); state->inspector.visible=true;
    focus(state,12,7); capture(&renderer,state,argv[1],"midi-editor");
    MidiEditorSelection note_selection = {0}; MidiEditorLayout note_layout = {0}; SDL_Rect note_rect = {0};
    assert(midi_editor_get_selection(state, &note_selection));
    midi_editor_select_note(state, &note_selection, 0);
    midi_editor_compute_layout(state, &note_layout);
    assert(midi_editor_note_rect(&note_layout, &engine_clip_midi_notes(note_selection.clip)[0],
                                 note_selection.clip->duration_frames, &note_rect));
    midi_editor_update_hover_note(state, &note_selection, &note_layout, note_rect.x, note_rect.y + note_rect.h / 2);
    assert(state->midi_editor_ui.hover_note_part == MIDI_EDITOR_NOTE_HIT_LEFT_EDGE);
    capture(&renderer,state,argv[1],"midi-resize-start");
    midi_editor_update_hover_note(state, &note_selection, &note_layout,
                                  note_rect.x + note_rect.w - 1, note_rect.y + note_rect.h / 2);
    assert(state->midi_editor_ui.hover_note_part == MIDI_EDITOR_NOTE_HIT_RIGHT_EDGE);
    capture(&renderer,state,argv[1],"midi-resize-end");
    midi_editor_clear_hover_note(state);

    DawMidiPatternLayout patterns;daw_midi_pattern_layout(state,&patterns);
    assert(daw_midi_pattern_activate(state,patterns.entry.x+1,patterns.entry.y+1));
    capture(&renderer,state,argv[1],"midi-pattern-popup");containment(&renderer,state,argv[1]);
    assert(daw_midi_pattern_activate(state,patterns.buttons[3].x+1,patterns.buttons[3].y+1));
    assert(daw_midi_pattern_activate(state,patterns.buttons[0].x+1,patterns.buttons[0].y+1));
    assert(engine_get_tracks(state->engine)[0].clips[clip].midi_notes.note_count==8);
    capture(&renderer,state,argv[1],"midi-pattern-committed");
    int expression_history=state->undo.undo_count;
    assert(daw_midi_pattern_activate(state,patterns.buttons[6].x+1,patterns.buttons[6].y+1));
    assert(state->undo.undo_count==expression_history+1);
    capture(&renderer,state,argv[1],"midi-expression-swung");containment(&renderer,state,argv[1]);
    assert(undo_manager_undo(&state->undo,state));
    assert(daw_midi_pattern_activate(state,patterns.buttons[7].x+1,patterns.buttons[7].y+1));
    assert(state->undo.undo_count==expression_history+1);
    capture(&renderer,state,argv[1],"midi-expression-velocity");
    assert(undo_manager_undo(&state->undo,state));

    assert(undo_manager_undo(&state->undo,state));assert(engine_get_tracks(state->engine)[0].clips[clip].midi_notes.note_count==1);
    assert(undo_manager_redo(&state->undo,state));assert(engine_get_tracks(state->engine)[0].clips[clip].midi_notes.note_count==8);
    assert(daw_midi_pattern_activate(state,patterns.buttons[4].x+1,patterns.buttons[4].y+1));
    capture(&renderer,state,argv[1],"midi-pattern-notes");
    focus(state,12,41);
    assert(daw_midi_arrangement_open(state));
    DawMidiArrangementLayout arrangement;daw_midi_arrangement_layout(state,&arrangement);
    capture(&renderer,state,argv[1],"midi-arrangement-fields");containment(&renderer,state,argv[1]);
    SDL_strlcpy(state->midi_arrangement_ui.fields[0],"24000",sizeof(state->midi_arrangement_ui.fields[0]));
    assert(daw_midi_arrangement_activate(state,arrangement.buttons[0].x+1,arrangement.buttons[0].y+1));
    assert(engine_get_tracks(state->engine)[0].clips[clip].timeline_start_frames==24000);
    capture(&renderer,state,argv[1],"midi-arrangement-moved");
    assert(undo_manager_undo(&state->undo,state));
    assert(daw_midi_arrangement_activate(state,arrangement.buttons[4].x+1,arrangement.buttons[4].y+1));
    SDL_strlcpy(state->midi_arrangement_ui.fields[1],"108000",sizeof(state->midi_arrangement_ui.fields[1]));
    assert(daw_midi_arrangement_activate(state,arrangement.buttons[2].x+1,arrangement.buttons[2].y+1));
    assert(engine_get_tracks(state->engine)[0].clips[clip].duration_frames==108000);
    capture(&renderer,state,argv[1],"midi-arrangement-trimmed");containment(&renderer,state,argv[1]);
    assert(undo_manager_undo(&state->undo,state));
    daw_midi_arrangement_release(state);
    state->midi_editor_ui.panel_mode=MIDI_REGION_PANEL_INSTRUMENT;
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE;
    focus(state,13,13); capture(&renderer,state,argv[1],"instrument-sine-envelope");
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_SYNTH_LAB));
    focus(state,13,13); capture(&renderer,state,argv[1],"instrument-synth-envelope");
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR;
    focus(state,13,11); capture(&renderer,state,argv[1],"instrument-synth-oscillator");
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_SOFT_PAD));
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE;
    focus(state,13,13); capture(&renderer,state,argv[1],"instrument-pad-envelope");
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS));
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR;
    focus(state,13,11); capture(&renderer,state,argv[1],"rounded-keys-oscillator");
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK));
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE;
    focus(state,13,13); capture(&renderer,state,argv[1],"rounded-pluck-envelope");
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD));
    focus(state,13,13); capture(&renderer,state,argv[1],"rounded-pad-envelope");
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS));
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_TONE;
    focus(state,13,12); capture(&renderer,state,argv[1],"filtered-keys-tone");
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_FILTERED_PAD));
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE;
    focus(state,13,13); capture(&renderer,state,argv[1],"filtered-pad-envelope");

    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_MOVING_PAD));
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_MOD;
    focus(state,13,14); capture(&renderer,state,argv[1],"moving-pad-modulation"); containment(&renderer,state,argv[1]);
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK));
    capture(&renderer,state,argv[1],"filter-pluck-modulation");
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,clip,ENGINE_INSTRUMENT_PRESET_FILTERED_PAD));
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_ENVELOPE;
    DawActionTarget preset_target; assert(daw_instrument_target_capture(state,0,clip,&preset_target)==DAW_ACTION_UNCHANGED);
    DawInstrumentSnapshot preset_sound=daw_action_get_instrument(state,preset_target).snapshot;
    assert(daw_action_save_preset(state,&preset_sound,"Evening Glass",NULL,NULL).status==DAW_PRESET_OK);
    MidiInstrumentPanelLayout preset_panel; midi_instrument_panel_compute_layout(state,&preset_panel);
    DawPresetPanelLayout preset_layout; daw_preset_panel_layout(state,preset_panel.panel_rect,preset_panel.header_rect,&preset_layout);
    assert(daw_preset_panel_activate(state,preset_layout.library_button.x+1,preset_layout.library_button.y+1));
    capture(&renderer,state,argv[1],"user-preset-browser"); containment(&renderer,state,argv[1]);
    SDL_Event escape={.type=SDL_KEYDOWN}; escape.key.keysym.sym=SDLK_ESCAPE; assert(daw_preset_panel_event(state,&escape));
    assert(daw_preset_panel_activate(state,preset_layout.save_button.x+1,preset_layout.save_button.y+1));
    capture(&renderer,state,argv[1],"user-preset-save"); assert(daw_preset_panel_event(state,&escape));
    DawSoundPanelLayout inspection;daw_sound_panel_layout(state,&inspection);
    assert(daw_sound_panel_activate(state,inspection.buttons[12].x+1,inspection.buttons[12].y+1));
    daw_sound_panel_layout(state,&inspection);
    assert(inspection.plot.w>0 && inspection.plot.h>0);
    assert(daw_sound_panel_activate(state,inspection.buttons[8].x+1,inspection.buttons[8].y+1));
    DawInstrumentSnapshot compared=daw_action_get_instrument(state,preset_target).snapshot;
    DawInstrumentRequest changed={.target=preset_target,.parameter_mask=1u<<ENGINE_INSTRUMENT_PARAM_CUTOFF_HZ,.params=compared.effective_params};
    changed.params.cutoff_hz=6500;assert(daw_action_set_instrument(state,&changed).status==DAW_ACTION_APPLIED);
    assert(daw_sound_panel_activate(state,inspection.buttons[7].x+1,inspection.buttons[7].y+1));
    assert(daw_sound_panel_activate(state,inspection.buttons[2].x+1,inspection.buttons[2].y+1));
    capture(&renderer,state,argv[1],"inspection-spectrum-reference");containment(&renderer,state,argv[1]);
    assert(daw_sound_panel_activate(state,inspection.buttons[1].x+1,inspection.buttons[1].y+1));
    capture(&renderer,state,argv[1],"inspection-envelope-reference");
    assert(daw_sound_panel_activate(state,inspection.buttons[0].x+1,inspection.buttons[0].y+1));
    capture(&renderer,state,argv[1],"inspection-waveform-reference");
    state->layout_runtime.mixer_ratio=0.25f;ui_layout_panes(state,1600,1000);
    capture(&renderer,state,argv[1],"inspection-compact");containment(&renderer,state,argv[1]);
    daw_sound_panel_layout(state,&inspection);
    assert(daw_sound_panel_activate(state,inspection.buttons[13].x+1,inspection.buttons[13].y+1));
    assert(state->sound_ui.composition && state->sound_ui.has_current);
    assert(daw_sound_panel_activate(state,inspection.buttons[8].x+1,inspection.buttons[8].y+1));
    assert(daw_sound_panel_activate(state,inspection.buttons[6].x+1,inspection.buttons[6].y+1));
    capture(&renderer,state,argv[1],"composition-dry-reference");containment(&renderer,state,argv[1]);
    assert(daw_sound_panel_activate(state,inspection.buttons[2].x+1,inspection.buttons[2].y+1));
    capture(&renderer,state,argv[1],"composition-spectrum-reference");
    assert(!setenv("SDL_AUDIODRIVER","dummy",1));
    assert(engine_start(state->engine));
    daw_sound_panel_layout(state,&inspection);
    SDL_Event listen={.type=SDL_MOUSEBUTTONDOWN};listen.button.button=SDL_BUTTON_LEFT;
    listen.button.x=inspection.buttons[14].x+1;listen.button.y=inspection.buttons[14].y+1;
    input_manager_handle_event(&state->input_manager,state,&listen);assert(!engine_preview_active(state->engine));
    listen.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&listen);assert(engine_preview_active(state->engine));
    capture(&renderer,state,argv[1],"captured-listening-active");containment(&renderer,state,argv[1]);
    Uint64 deadline=SDL_GetTicks64()+10000;
    while(engine_preview_active(state->engine) && SDL_GetTicks64()<deadline)SDL_Delay(10);
    assert(!engine_preview_active(state->engine));
    SDL_LockAudioDevice(state->engine->device.device_id);
    assert(state->engine->device.preview_cursor==state->sound_ui.captured.audio.frame_count);
    SDL_UnlockAudioDevice(state->engine->device.device_id);
    capture(&renderer,state,argv[1],"captured-listening-finished");
    listen.type=SDL_MOUSEBUTTONDOWN;listen.button.x=inspection.buttons[15].x+1;listen.button.y=inspection.buttons[15].y+1;
    input_manager_handle_event(&state->input_manager,state,&listen);listen.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&listen);assert(engine_preview_active(state->engine));
    capture(&renderer,state,argv[1],"captured-reference-listening");
    listen.type=SDL_MOUSEBUTTONDOWN;listen.button.x=inspection.buttons[16].x+1;listen.button.y=inspection.buttons[16].y+1;
    input_manager_handle_event(&state->input_manager,state,&listen);listen.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&listen);
    assert(!engine_preview_active(state->engine) && !state->engine->device.preview_pcm);
    listen.type=SDL_MOUSEBUTTONDOWN;listen.button.x=inspection.buttons[14].x+1;listen.button.y=inspection.buttons[14].y+1;
    input_manager_handle_event(&state->input_manager,state,&listen);listen.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&listen);assert(engine_preview_active(state->engine));
    SDL_Event lost={.type=SDL_WINDOWEVENT};lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;input_manager_handle_event(&state->input_manager,state,&lost);
    assert(!state->sound_ui.open && !state->sound_ui.captured.audio.data && !state->engine->device.preview_pcm);
    daw_sound_panel_layout(state,&inspection);
    assert(daw_sound_panel_activate(state,inspection.buttons[12].x+1,inspection.buttons[12].y+1));
    daw_sound_panel_layout(state,&inspection);
    assert(state->sound_ui.composition && state->sound_ui.has_current);
    assert(daw_sound_panel_activate(state,inspection.buttons[14].x+1,inspection.buttons[14].y+1) && engine_preview_active(state->engine));
    SessionDocument replacement={0};session_document_reset(&replacement);
    assert(session_document_capture(state,&replacement));
    assert(session_apply_document(state,&replacement));session_document_free(&replacement);
    assert(!state->sound_ui.open && !state->sound_ui.captured.audio.data && !engine_preview_active(state->engine));
    // Command-Q must remain an application shortcut even after global modifiers are released.
    SDL_Event quit_shortcut = {.type=SDL_KEYDOWN}; quit_shortcut.key.keysym.sym=SDLK_q;
    quit_shortcut.key.keysym.mod=KMOD_GUI; SDL_SetModState(KMOD_NONE);
    assert(!midi_editor_handle_qwerty_event(state,&quit_shortcut));
    // Verify complete toolbar hitboxes activate once and mouse focus never imitates selection.
    daw_editor_controls_sync(state);
    SDL_Rect auto_button = state->timeline_controls.automation_toggle_rect;
    bool old_auto = state->timeline_automation_mode;
    SDL_Event toolbar = {.type = SDL_MOUSEBUTTONDOWN}; toolbar.button.button = SDL_BUTTON_LEFT;
    toolbar.button.x = auto_button.x + 2; toolbar.button.y = auto_button.y + auto_button.h / 2;
    input_manager_handle_event(&state->input_manager,state,&toolbar);
    toolbar.type = SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&toolbar);
    assert(state->timeline_automation_mode != old_auto && !state->editor_controls.focus_from_keyboard);
    toolbar.type = SDL_MOUSEBUTTONDOWN; toolbar.button.x = auto_button.x + auto_button.w - 3;
    input_manager_handle_event(&state->input_manager,state,&toolbar);
    toolbar.type = SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&toolbar);
    assert(state->timeline_automation_mode == old_auto);
    activate_control(state,11,6);
    assert(state->automation_ui.menu_open);
    capture(&renderer,state,argv[1],"automation-target-dropdown");
    SDL_Rect pan_row = automation_menu_target_rect(state,1);
    toolbar.type = SDL_MOUSEBUTTONDOWN; toolbar.button.x = pan_row.x + 5; toolbar.button.y = pan_row.y + pan_row.h / 2;
    input_manager_handle_event(&state->input_manager,state,&toolbar);
    toolbar.type = SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&toolbar);
    assert(!state->automation_ui.menu_open && !state->automation_ui.menu_release &&
        state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN && state->timeline_automation_mode);
    activate_control(state,11,6); assert(state->automation_ui.menu_open);
    toolbar.type = SDL_MOUSEBUTTONDOWN; toolbar.button.x = 10; toolbar.button.y = 10;
    input_manager_handle_event(&state->input_manager,state,&toolbar);
    toolbar.type = SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&toolbar);
    assert(!state->automation_ui.menu_open && state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN);
    activate_control(state,11,6);
    SDL_Event menu_key = {.type=SDL_KEYDOWN}; menu_key.key.keysym.sym=SDLK_ESCAPE;
    input_manager_handle_event(&state->input_manager,state,&menu_key);
    assert(!state->automation_ui.menu_open);
    // A melodic voice keeps percussion rows visible but refuses dormant pointer and keyboard choices.
    activate_control(state,11,6);
    SDL_Rect dormant_row = automation_menu_target_rect(state,25);
    toolbar.type = SDL_MOUSEBUTTONDOWN; toolbar.button.x = dormant_row.x + 5;
    toolbar.button.y = dormant_row.y + dormant_row.h / 2;
    input_manager_handle_event(&state->input_manager,state,&toolbar);
    toolbar.type = SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&toolbar);
    assert(state->automation_ui.menu_open && state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN);
    menu_key.key.keysym.sym = SDLK_RETURN;
    input_manager_handle_event(&state->input_manager,state,&menu_key);
    assert(state->automation_ui.menu_open && state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN);
    capture(&renderer,state,argv[1],"automation-target-instrument-support");
    menu_key.key.keysym.sym = SDLK_ESCAPE;
    input_manager_handle_event(&state->input_manager,state,&menu_key);
    // Deselecting a region preserves every menu row and disables instrument choices.
    SDL_Rect full_menu_last = automation_menu_target_rect(state,ENGINE_AUTOMATION_TARGET_COUNT-1);
    int saved_track = state->selected_track_index, saved_clip = state->selected_clip_index;
    state->selected_track_index = -1; state->selected_clip_index = -1;
    activate_control(state,11,6);
    SDL_Rect deselected_last = automation_menu_target_rect(state,ENGINE_AUTOMATION_TARGET_COUNT-1);
    assert(full_menu_last.w > 0 && memcmp(&full_menu_last,&deselected_last,sizeof(SDL_Rect)) == 0);
    capture(&renderer,state,argv[1],"automation-target-dropdown-disabled");
    SDL_Rect instrument_row = automation_menu_target_rect(state,2);
    toolbar.type = SDL_MOUSEBUTTONDOWN;
    toolbar.button.x = instrument_row.x + 5; toolbar.button.y = instrument_row.y + instrument_row.h / 2;
    uint64_t menu_history = state->automation_ui.history_serial;
    input_manager_handle_event(&state->input_manager,state,&toolbar);
    toolbar.type = SDL_MOUSEBUTTONUP; input_manager_handle_event(&state->input_manager,state,&toolbar);
    assert(state->automation_ui.menu_open && !state->automation_ui.menu_release &&
        state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN &&
        state->selected_track_index == -1 && state->selected_clip_index == -1 &&
        state->automation_ui.history_serial == menu_history);
    menu_key.key.keysym.sym = SDLK_RETURN;
    input_manager_handle_event(&state->input_manager,state,&menu_key);
    assert(state->automation_ui.menu_open && state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN);
    state->selected_track_index = saved_track; state->selected_clip_index = saved_clip;
    input_manager_handle_event(&state->input_manager,state,&menu_key);
    assert(!state->automation_ui.menu_open && (int)state->automation_ui.target == 2);
    state->automation_ui.target = ENGINE_AUTOMATION_TARGET_PAN;
    const EngineClip* automation_region = &engine_get_tracks(state->engine)[0].clips[0];
    uint64_t automation_duration = automation_region->duration_frames;
    EngineAutomationPoint automation_points[] = {{0, -12, ENGINE_AUTOMATION_CURVE_SMOOTH},
        {automation_duration, 0, ENGINE_AUTOMATION_CURVE_LINEAR}};
    EngineAutomationLane automation_lane = {.target = ENGINE_AUTOMATION_TARGET_VOLUME,
        .points = automation_points, .point_count = 2, .domain = ENGINE_AUTOMATION_DOMAIN_PHYSICAL,
        .before = ENGINE_AUTOMATION_BOUNDARY_HOLD, .after = ENGINE_AUTOMATION_BOUNDARY_HOLD};
    assert(daw_automation_replace_lanes(state, 0, 0, &automation_lane, 1));
    state->timeline_automation_mode = true;
    state->automation_ui.target = ENGINE_AUTOMATION_TARGET_VOLUME;
    state->automation_ui.track_index = 0; state->automation_ui.clip_index = 0; state->automation_ui.point_index = 0;
    SDL_Event automation_key = {.type = SDL_KEYDOWN}; automation_key.key.keysym.sym = SDLK_2;
    input_manager_handle_event(&state->input_manager, state, &automation_key);
    const EngineAutomationLane* native_lane = NULL;
    assert(engine_clip_get_automation_lane(state->engine, 0, 0, ENGINE_AUTOMATION_TARGET_VOLUME, &native_lane));
    assert(native_lane->points[0].curve == ENGINE_AUTOMATION_CURVE_HOLD);
    capture(&renderer, state, argv[1], "automation-hold-and-units");
    automation_key.key.keysym.sym = SDLK_3;
    input_manager_handle_event(&state->input_manager, state, &automation_key);
    capture(&renderer, state, argv[1], "automation-smooth-and-units");
    state->timeline_automation_mode = false;
    state->automation_ui.menu_open=false;
    assert(engine_clip_midi_set_instrument_preset(state->engine,0,0,ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN));
    timeline_selection_set_single(state,0,0);
    state->inspector.visible=true;
    state->midi_editor_ui.panel_mode=MIDI_REGION_PANEL_INSTRUMENT;
    state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_MOD;
    capture(&renderer,state,argv[1],"sampled-violin-expression"); containment(&renderer,state,argv[1]);
    state->automation_ui.target=ENGINE_AUTOMATION_TARGET_INSTRUMENT_DYNAMICS;
    automation_menu_open(state);
    capture(&renderer,state,argv[1],"sampled-violin-automation");
    state->automation_ui.menu_open=false;
    const EngineInstrumentPresetId winds[]={ENGINE_INSTRUMENT_PRESET_SAMPLED_TRUMPET,ENGINE_INSTRUMENT_PRESET_SAMPLED_CLARINET,ENGINE_INSTRUMENT_PRESET_SAMPLED_SAXOPHONE,ENGINE_INSTRUMENT_PRESET_SAMPLED_FLUTE,ENGINE_INSTRUMENT_PRESET_SAMPLED_CELLO_SECTION,ENGINE_INSTRUMENT_PRESET_SAMPLED_OBOE,ENGINE_INSTRUMENT_PRESET_SAMPLED_FRENCH_HORN,ENGINE_INSTRUMENT_PRESET_SAMPLED_BASSOON,ENGINE_INSTRUMENT_PRESET_SAMPLED_TENOR_TROMBONE,ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO,ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLA_SECTION,ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN_SECTION,ENGINE_INSTRUMENT_PRESET_SAMPLED_DOUBLE_BASS};
    for (int i=0;i<13;++i) {
        assert(engine_clip_midi_set_instrument_preset(state->engine,0,0,winds[i]));
        state->midi_editor_ui.instrument_active_group=ENGINE_INSTRUMENT_PARAM_GROUP_MOD;
        const char* names[]={"sampled-trumpet","sampled-clarinet","sampled-saxophone","sampled-flute","sampled-cello-section","sampled-oboe","sampled-french-horn","sampled-bassoon","sampled-tenor-trombone","sampled-acoustic-piano","sampled-viola-section","sampled-violin-section","sampled-double-bass"};
        capture(&renderer,state,argv[1],names[i]);
        containment(&renderer,state,argv[1]);
    }
    engine_stop(state->engine);unsetenv("SDL_AUDIODRIVER");
    daw_routing_panel_release(state); daw_fx_library_panel_release(state);retire_library_directory(library);unsetenv("DAW_PRESET_LIBRARY");
    ui_font_invalidate_cache((SDL_Renderer*)&renderer); ui_font_shutdown(); undo_manager_free(&state->undo);
    tempo_map_free(&state->tempo_map);time_signature_map_free(&state->time_signature_map);
    engine_destroy(state->engine); free(state); vk_renderer_shutdown(&renderer);
    SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();
    puts("native_editor_controls_test: success (actual product painters, discrete exceptions, native focus and validation-clean frames)");
}
