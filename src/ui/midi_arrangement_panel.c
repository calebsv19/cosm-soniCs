#include "ui/midi_arrangement_panel.h"
#include "app_state.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "ui/midi_pattern_panel.h"
#include "ui/text_edit.h"
#include "ui/daw_ui_button.h"
#include "ui/render_utils.h"
#include "ui/font.h"
#include "input/timeline_selection.h"
#include "input/inspector_input.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Resolves the selected region through stable application identities.
static bool selected(AppState* state, DawActionTarget* target) {
    MidiEditorSelection selection;
    return midi_editor_get_selection(state,&selection) &&
        daw_instrument_target_capture(state,selection.track_index,selection.clip_index,target)==DAW_ACTION_UNCHANGED;
}
// Retires one form's snapshot and composition without changing the project.
void daw_midi_arrangement_release(AppState* state) {
    if (!state) return;
    DawMidiArrangementUI* ui=&state->midi_arrangement_ui;
    daw_arrangement_snapshot_free(ui->captured); free(ui->captured); ui->captured=NULL;
    ui->open=false; ui->focused=ui->pressed=-1;
    kit_ui_text_cancel_composition(&ui->text_edit); daw_text_resume_input(state);
}
// Shares bounded editor geometry with all native input and painter consumers.
void daw_midi_arrangement_layout(const AppState* state,DawMidiArrangementLayout* out) {
    *out=(DawMidiArrangementLayout){0}; MidiEditorLayout base; midi_editor_compute_layout(state,&base);
    DawMidiPatternLayout patterns; daw_midi_pattern_layout(state,&patterns);
    out->entry=(SDL_Rect){patterns.entry.x-98,patterns.entry.y,94,patterns.entry.h};
    int width=base.panel_rect.w-16; if(width>620)width=620;
    int height=base.panel_rect.h-base.header_rect.h-12; if(height>165)height=165;
    out->panel=(SDL_Rect){base.panel_rect.x+base.panel_rect.w-width-8,base.panel_rect.y+base.panel_rect.h-height-6,width,height};
    int field_width=(width-24)/3;
    for(int i=0;i<3;++i)out->fields[i]=(SDL_Rect){out->panel.x+6+i*(field_width+6),out->panel.y+45,field_width,25};
    int button_width=(width-22)/6;
    for(int i=0;i<6;++i)out->buttons[i]=(SDL_Rect){out->panel.x+6+i*(button_width+2),out->panel.y+78,button_width,25};
}
// Requires enough visible space for every staged control.
static bool fits(const DawMidiArrangementLayout* layout) { return layout->panel.w>=380 && layout->panel.h>=160; }
// Refreshes explicit fields and the retained full-state guard only on user actions.
static bool refresh(AppState* state) {
    DawMidiArrangementUI* ui=&state->midi_arrangement_ui; int track,clip;
    if(daw_action_target_resolve(state,ui->target,&track,&clip)!=DAW_ACTION_UNCHANGED)return false;
    DawArrangementSnapshot* next=calloc(1,sizeof(*next)); if(!next)return false;
    DawMidiResult got=daw_action_get_arrangement(state,next);
    if(got.status!=DAW_MIDI_UNCHANGED){daw_arrangement_snapshot_free(next);free(next);SDL_strlcpy(ui->message,got.message,sizeof(ui->message));return false;}
    daw_arrangement_snapshot_free(ui->captured);free(ui->captured);ui->captured=next;
    const EngineClip* region=&engine_get_tracks(state->engine)[track].clips[clip];
    snprintf(ui->fields[0],sizeof(ui->fields[0]),"%" PRIu64,region->timeline_start_frames);
    snprintf(ui->fields[1],sizeof(ui->fields[1]),"%" PRIu64,region->duration_frames);
    snprintf(ui->fields[2],sizeof(ui->fields[2]),"%d",track+1);
    return true;
}
// Opens a guarded form without editing notes, placement, sound or history.
bool daw_midi_arrangement_open(AppState* state) {
    if(!state || !midi_editor_should_render(state) || midi_instrument_panel_should_render(state) || state->midi_editor_ui.instrument_menu_open)return false;
    DawMidiArrangementLayout layout;daw_midi_arrangement_layout(state,&layout);if(!fits(&layout))return false;
    DawActionTarget target;if(!selected(state,&target))return false;
    daw_midi_arrangement_release(state);DawMidiArrangementUI* ui=&state->midi_arrangement_ui;ui->target=target;
    if(!refresh(state))return false;
    daw_text_cancel_composition(state);state->midi_pattern_ui.open=false;ui->open=true;ui->focused=ui->pressed=-1;
    SDL_strlcpy(ui->message,"Set placement, then Move or Copy. Trim changes only the right edge.",sizeof(ui->message));return true;
}
// Parses exact unsigned fields without floating-point conversion or partial numeric acceptance.
static bool number(const char* value,uint64_t* out) {
    if(!value || !*value || (value[0]=='0' && value[1]))return false;
    for(const char* p=value;*p;++p)if(*p<'0' || *p>'9')return false;
    errno=0;char* end;unsigned long long parsed=strtoull(value,&end,10);if(errno || *end)return false;*out=parsed;return true;
}
// Begins one shared text field without permitting editor shortcut fallthrough.
static void focus(AppState* state,int field) {
    DawMidiArrangementUI* ui=&state->midi_arrangement_ui;daw_text_cancel_composition(state);ui->focused=field;ui->cursor=strlen(ui->fields[field]);
    daw_text_edit_begin(&ui->text_edit,ui->fields[field],sizeof(ui->fields[field]),&ui->cursor,KIT_UI_TEXT_SINGLE_LINE);SDL_StartTextInput();
}
// Applies exactly one form operation through the common atomic arrangement owner.
static void apply(AppState* state,int button) {
    DawMidiArrangementUI* ui=&state->midi_arrangement_ui;
    if(button==5){daw_midi_arrangement_release(state);return;}
    kit_ui_text_cancel_composition(&ui->text_edit);ui->focused=-1;daw_text_resume_input(state);
    if(button==4){if(refresh(state))SDL_strlcpy(ui->message,"Current arrangement captured. Staged values refreshed.",sizeof(ui->message));return;}
    DawArrangementEdit edit={.target=ui->target};uint64_t lane=0;
    if(button<2){edit.operation=button==0?ENGINE_ARRANGE_MOVE:ENGINE_ARRANGE_DUPLICATE;
        if(!number(ui->fields[0],&edit.start_frame) || !number(ui->fields[2],&lane) || !lane || !ui->captured || lane>(uint64_t)ui->captured->layout.track_count)goto invalid;
        edit.destination_track_id=ui->captured->layout.track_ids[lane-1];
    }else if(button==2){edit.operation=ENGINE_ARRANGE_TRIM;if(!number(ui->fields[1],&edit.duration_frames))goto invalid;}
    else edit.operation=ENGINE_ARRANGE_DELETE;
    DawActionTarget output={0};DawMidiResult result=daw_action_arrange(state,ui->captured,&edit,1,&output);
    SDL_strlcpy(ui->message,result.message,sizeof(ui->message));
    if(result.status<=DAW_MIDI_UNCHANGED){
        if(!output.clip_id){timeline_selection_clear(state);daw_midi_arrangement_release(state);return;}
        int track,clip;if(daw_action_target_resolve(state,output,&track,&clip)==DAW_ACTION_UNCHANGED){timeline_selection_set_single(state,track,clip);inspector_input_set_clip(state,track,clip);ui->target=output;}
        refresh(state);
    }
    return;
invalid:
    SDL_strlcpy(ui->message,"Use whole frames and an existing lane number. Refresh after project changes.",sizeof(ui->message));
}
// Finds only controls inside the same complete visible pane used by painting.
static int hit(const DawMidiArrangementLayout* layout,int x,int y) {
    SDL_Point point={x,y};if(!fits(layout) || !SDL_PointInRect(&point,&layout->panel))return -1;
    for(int i=0;i<6;++i)if(SDL_PointInRect(&point,&layout->buttons[i]))return i;
    for(int i=0;i<3;++i)if(SDL_PointInRect(&point,&layout->fields[i]))return 100+i;
    return -1;
}
// Routes direct shared-control activation through the same form action owner.
bool daw_midi_arrangement_activate(AppState* state,int x,int y) {
    if(!state)return false;DawMidiArrangementLayout layout;daw_midi_arrangement_layout(state,&layout);
    if(!state->midi_arrangement_ui.open){SDL_Point point={x,y};return SDL_PointInRect(&point,&layout.entry) && daw_midi_arrangement_open(state);}
    int control=hit(&layout,x,y);if(control<0)return false;if(control>=100)focus(state,control-100);else apply(state,control);return true;
}
// Consumes modal events and cancels stale targets, focus changes and unmatched pointer releases.
bool daw_midi_arrangement_event(AppState* state,const SDL_Event* event) {
    if(!state || !event || !state->midi_arrangement_ui.open)return false;
    DawMidiArrangementUI* ui=&state->midi_arrangement_ui;DawMidiArrangementLayout layout;daw_midi_arrangement_layout(state,&layout);
    DawActionTarget current={0};
    if(!fits(&layout) || !midi_editor_should_render(state) || midi_instrument_panel_should_render(state) || state->midi_editor_ui.instrument_menu_open || !selected(state,&current) ||
        current.project_generation!=ui->target.project_generation || current.track_id!=ui->target.track_id || current.clip_id!=ui->target.clip_id){daw_midi_arrangement_release(state);return false;}
    if(event->type==SDL_WINDOWEVENT){ui->pressed=-1;if(event->window.event==SDL_WINDOWEVENT_FOCUS_LOST)daw_midi_arrangement_release(state);return false;}
    if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE){daw_midi_arrangement_release(state);return true;}
    if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_TAB){focus(state,(ui->focused+1)%3);return true;}
    if(ui->focused>=0){KitUiTextEventResult result=daw_text_edit_event(&ui->text_edit,ui->fields[ui->focused],sizeof(ui->fields[0]),&ui->cursor,KIT_UI_TEXT_SINGLE_LINE,event);
        if(result.submit){ui->focused=-1;daw_text_resume_input(state);}if(result.consumed)return true;}
    if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT){ui->pressed=hit(&layout,event->button.x,event->button.y);return true;}
    if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT){int pressed=ui->pressed;ui->pressed=-1;
        if(pressed>=0 && pressed==hit(&layout,event->button.x,event->button.y))daw_midi_arrangement_activate(state,event->button.x,event->button.y);return true;}
    if(event->type==SDL_MOUSEWHEEL){ui->pressed=-1;return true;}
    return event->type==SDL_KEYDOWN || event->type==SDL_KEYUP || event->type==SDL_TEXTINPUT || event->type==SDL_TEXTEDITING || event->type==SDL_MOUSEMOTION;
}
// Paints cached fields and shared buttons with enclosing clipping preserved.
void daw_midi_arrangement_render(SDL_Renderer* renderer,const AppState* state) {
    if(!state || !midi_editor_should_render(state) || midi_instrument_panel_should_render(state) || state->midi_editor_ui.instrument_menu_open)return;
    DawMidiArrangementLayout layout;daw_midi_arrangement_layout(state,&layout);DawThemePalette theme;daw_shared_theme_resolve_palette(&theme);
    DawUiButtonSpec spec;daw_ui_button_spec_init(&spec,fits(&layout)?"Arrange":"Enlarge pane");spec.state.selected=state->midi_arrangement_ui.open;
    DawUiButtonStyle style;daw_ui_button_style_resolve(&theme,&spec,&style);daw_ui_button_draw_frame(renderer,&layout.entry,&style);
    ui_draw_text_clipped(renderer,layout.entry.x+5,layout.entry.y+5,spec.label,theme.text_primary,.75f,layout.entry.w-10);
    const DawMidiArrangementUI* ui=&state->midi_arrangement_ui;if(!ui->open || !fits(&layout))return;
    SDL_Rect previous;SDL_RenderGetClipRect(renderer,&previous);bool clipped=SDL_RenderIsClipEnabled(renderer);ui_set_content_clip_rect(renderer,&layout.panel);
    SDL_SetRenderDrawColor(renderer,theme.inspector_fill.r,theme.inspector_fill.g,theme.inspector_fill.b,255);SDL_RenderFillRect(renderer,&layout.panel);
    ui_draw_text_clipped(renderer,layout.panel.x+6,layout.panel.y+5,"Arrange selected MIDI region",theme.text_primary,.8f,layout.panel.w-12);
    const char* labels[]={"Start (frames)","Length (frames)","Destination lane"};
    for(int i=0;i<3;++i){SDL_Rect field=layout.fields[i];ui_draw_text_clipped(renderer,field.x,field.y-18,labels[i],theme.text_muted,.75f,field.w);
        SDL_SetRenderDrawColor(renderer,theme.pane_border.r,theme.pane_border.g,theme.pane_border.b,255);SDL_RenderDrawRect(renderer,&field);
        if(ui->focused==i)daw_text_edit_draw(renderer,&ui->text_edit,(char*)ui->fields[i],sizeof(ui->fields[i]),ui->cursor,field,theme.text_primary,theme.text_muted,.75f);
        else ui_draw_text_clipped(renderer,field.x+5,field.y+5,ui->fields[i],theme.text_primary,.75f,field.w-10);}
    const char* names[]={"Move","Copy","Trim","Delete","Refresh","Close"};
    for(int i=0;i<6;++i){daw_ui_button_spec_init(&spec,names[i]);daw_ui_button_style_resolve(&theme,&spec,&style);daw_ui_button_draw_frame(renderer,&layout.buttons[i],&style);ui_draw_text_clipped(renderer,layout.buttons[i].x+5,layout.buttons[i].y+5,names[i],theme.text_primary,.75f,layout.buttons[i].w-10);}
    ui_draw_text_clipped(renderer,layout.panel.x+6,layout.panel.y+110,ui->message,theme.text_muted,.7f,layout.panel.w-12);
    ui_draw_text_clipped(renderer,layout.panel.x+6,layout.panel.y+137,"Lane numbers start at 1. Overlaps and trims that lose notes refuse.",theme.text_muted,.7f,layout.panel.w-12);
    SDL_RenderSetClipRect(renderer,clipped?&previous:NULL);
}
