#include "app_state.h"
#include "ui/fx_library_panel.h"
#include "ui/effects_panel.h"
#include "ui/midi_editor.h"
#include "ui/text_edit.h"
#include "ui/daw_ui_button.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/control_chrome.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Resolves the current selected lane or master through stable common chain identity.
static bool current(AppState* state,DawFxSnapshot* out) {
    uint64_t track=0;const EffectsPanelState* panel=&state->effects_panel;
    if(!state->engine)return false;
    if(panel->target==FX_PANEL_TARGET_TRACK){int index=panel->target_track_index;if(index<0 || index>=engine_get_track_count(state->engine))return false;track=engine_get_tracks(state->engine)[index].runtime_id;}
    return daw_action_get_fx(state,engine_instance_id(state->engine),track,out)==DAW_ACTION_UNCHANGED;
}
// Closes only this modal's text/capture ownership while retaining loaded preset identity for a later update.
static void close_panel(AppState* state) {
    DawFxLibraryUI* ui=&state->fx_library_ui;ui->open=ui->saving=ui->editing=false;ui->pressed=-1;kit_ui_text_cancel_composition(&ui->text_edit);daw_text_resume_input(state);
}
// Refreshes library files and row labels only after explicit user actions.
static void refresh(AppState* state) {
    DawFxLibraryUI* ui=&state->fx_library_ui;if(!ui->library)ui->library=calloc(1,sizeof(*ui->library));
    if(!ui->library){SDL_strlcpy(ui->message,"Library allocation failed.",sizeof(ui->message));return;}
    DawPresetStatus status=daw_fx_library_list(state->engine,ui->library);++ui->generation;
    if(status!=DAW_PRESET_OK)SDL_strlcpy(ui->message,daw_preset_status_message(status),sizeof(ui->message));
    else snprintf(ui->message,sizeof(ui->message),"%d chains, %d invalid%s. Recall replaces this whole chain in one undo step.",ui->library->count,ui->library->invalid_count,ui->library->truncated?", list limited":"");
}
// Creates a bounded label-search/name field through the existing shared UTF-8 editing contract.
static void begin_text(AppState* state) {
    DawFxLibraryUI* ui=&state->fx_library_ui;daw_text_cancel_composition(state);ui->editing=true;ui->pressed=-1;char* buffer=ui->saving?ui->name:ui->search;ui->cursor=strlen(buffer);
    daw_text_edit_begin(&ui->text_edit,buffer,96,&ui->cursor,KIT_UI_TEXT_SINGLE_LINE);SDL_StartTextInput();
}
// Opens a copied chain precondition and refreshes its personal library without changing sound/history.
bool daw_fx_library_panel_open(AppState* state) {
    if(!state || state->inspector.visible || midi_editor_should_render(state) || !current(state,&state->fx_library_ui.captured))return false;
    DawFxLibraryUI* ui=&state->fx_library_ui;
    state->effects_panel.overlay_layer=FX_PANEL_OVERLAY_CLOSED;ui->open=true;ui->saving=false;ui->scroll=0;ui->pressed=-1;refresh(state);begin_text(state);return true;
}
// Keeps row clipping and pointer hit regions identical at compact and ordinary pane sizes.
void daw_fx_library_panel_layout(const AppState* state,DawFxLibraryLayout* out) {
    *out=(DawFxLibraryLayout){0};EffectsPanelLayout base;effects_panel_compute_layout(state,&base);out->panel=base.panel_rect;
    int x=out->panel.x+8,y=out->panel.y+8,w=out->panel.w-16,h=out->panel.h-16;if(w<0)w=0;
    out->field=(SDL_Rect){x,y+25,w,24};int button_w=(w-12)/4;if(button_w<0)button_w=0;
    for(int i=0;i<4;++i)out->buttons[i]=(SDL_Rect){x+i*(button_w+4),y+53,button_w,22};
    out->status=(SDL_Rect){x,y+79,w,20};int rows=(h-104)/25;if(rows<0)rows=0;if(rows>8)rows=8;
    const DawFxLibraryUI* ui=&state->fx_library_ui;if(ui->saving || !ui->library)return;int matched=0;
    for(int i=0;i<ui->library->count && out->count<rows;++i){if(*ui->search && !strstr(ui->library->items[i].name,ui->search))continue;if(matched++<ui->scroll)continue;int row=out->count++;out->indices[row]=i;out->rows[row]=(SDL_Rect){x,y+104+row*25,w,23};}
}
// Reports application and storage failures independently while retaining rejected user input.
static bool accepted(DawFxLibraryUI* ui,const DawFxPresetResult* result) {
    if(result->action>DAW_ACTION_UNCHANGED){const char* messages[]={"Applied.","Unchanged.","Invalid chain.","Wrong owner.","Chain changed; close and reopen before retrying.","Stop playback/recording and finish the active edit.","History reservation failed.","Engine rejected the chain."};SDL_strlcpy(ui->message,messages[result->action],sizeof(ui->message));return false;}
    if(result->library!=DAW_PRESET_OK){SDL_strlcpy(ui->message,daw_preset_status_message(result->library),sizeof(ui->message));return false;}return true;
}
// Saves or recalls exactly the chain and explicit library revision captured by the modal owner.
static void activate(AppState* state,int control,const DawFxLibraryLayout* layout) {
    DawFxLibraryUI* ui=&state->fx_library_ui;
    if(control==3){close_panel(state);return;}
    if(ui->saving){
        if(control==0){DawFxPresetResult result=daw_action_save_fx_preset(state,&ui->captured,ui->name,ui->update?ui->loaded.id:NULL,ui->update?ui->loaded.revision:NULL);
            if(accepted(ui,&result)){ui->loaded=result.preset;ui->has_loaded=true;ui->saving=false;refresh(state);SDL_strlcpy(ui->message,result.durable?"Chain saved to your library.":"Chain published; directory sync uncertain.",sizeof(ui->message));begin_text(state);}}
        else if(control==1){ui->saving=false;begin_text(state);}return;
    }
    if(control==0 || control==1){if(control==1 && !ui->has_loaded){SDL_strlcpy(ui->message,"Recall or save a chain before updating it.",sizeof(ui->message));return;}ui->saving=true;ui->update=control==1;SDL_strlcpy(ui->name,ui->update?ui->loaded.name:"",sizeof(ui->name));begin_text(state);}
    else if(control==2){DawFxSnapshot next;if(current(state,&next))ui->captured=next;refresh(state);ui->scroll=0;}
    else if(control>=4 && control<4+layout->count){const DawFxPreset* preset=&ui->library->items[layout->indices[control-4]];
        DawFxPresetResult result=daw_action_apply_fx_preset(state,&ui->captured,preset->id,preset->revision);if(accepted(ui,&result)){ui->captured=result.snapshot;ui->loaded=result.preset;ui->has_loaded=true;effects_panel_sync_from_engine(state);SDL_strlcpy(ui->message,result.action==DAW_ACTION_APPLIED?"Chain recalled as one undo step.":"This chain already matches.",sizeof(ui->message));}}
}
// Finds a discrete control in the same visible rectangles used by the painter.
static int hit(const DawFxLibraryLayout* layout,int x,int y) {
    SDL_Point point={x,y};if(!SDL_PointInRect(&point,&layout->panel))return -1;for(int i=0;i<4;++i)if(SDL_PointInRect(&point,&layout->buttons[i]))return i;for(int i=0;i<layout->count;++i)if(SDL_PointInRect(&point,&layout->rows[i]))return i+4;return -1;
}
// Gives modal text and matched pointer release priority over underlying editor commands.
bool daw_fx_library_panel_event(AppState* state,const SDL_Event* event) {
    if(!state || !event || !state->fx_library_ui.open)return false;DawFxLibraryUI* ui=&state->fx_library_ui;
    if(state->inspector.visible || midi_editor_should_render(state)){close_panel(state);return false;}
    DawFxLibraryLayout layout;daw_fx_library_panel_layout(state,&layout);
    if(event->type==SDL_WINDOWEVENT){ui->pressed=-1;if(event->window.event==SDL_WINDOWEVENT_FOCUS_LOST || event->window.event==SDL_WINDOWEVENT_HIDDEN)kit_ui_text_cancel_composition(&ui->text_edit);return false;}
    if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT){ui->pressed=hit(&layout,event->button.x,event->button.y);SDL_Point p={event->button.x,event->button.y};if(SDL_PointInRect(&p,&layout.panel) && SDL_PointInRect(&p,&layout.field)){begin_text(state);char* buffer=ui->saving?ui->name:ui->search;daw_text_edit_click(&ui->text_edit,buffer,96,&ui->cursor,KIT_UI_TEXT_SINGLE_LINE,.8f,layout.field.x+5,layout.field.w-10,event->button.x);}return true;}
    if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT){int pressed=ui->pressed;ui->pressed=-1;if(pressed>=0 && pressed==hit(&layout,event->button.x,event->button.y))activate(state,pressed,&layout);return true;}
    if(event->type==SDL_MOUSEWHEEL){ui->pressed=-1;if(!ui->saving){ui->scroll-=event->wheel.y;if(ui->scroll<0)ui->scroll=0;if(ui->library && ui->scroll>ui->library->count)ui->scroll=ui->library->count;}return true;}
    if(event->type==SDL_KEYDOWN && event->key.repeat)return true;
    if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE){close_panel(state);return true;}
    char* buffer=ui->saving?ui->name:ui->search;KitUiTextEventResult result=daw_text_edit_event(&ui->text_edit,buffer,96,&ui->cursor,KIT_UI_TEXT_SINGLE_LINE,event);
    if(result.changed){ui->pressed=-1;if(!ui->saving)ui->scroll=0;}if(result.submit && ui->saving){ui->pressed=-1;activate(state,0,&layout);}
    if(event->type==SDL_KEYDOWN || event->type==SDL_KEYUP || event->type==SDL_TEXTINPUT || event->type==SDL_TEXTEDITING || event->type==SDL_MOUSEMOTION || event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP)return true;return result.consumed;
}
// Draws existing rounded product controls with persistent cached label storage.
static void button(SDL_Renderer* renderer,SDL_Rect rect,const char* label,const DawThemePalette* theme) {
    if(rect.w<=0 || rect.h<=0)return;DawUiButtonSpec spec;daw_ui_button_spec_init(&spec,label);DawUiButtonStyle style;daw_ui_button_style_resolve(theme,&spec,&style);daw_ui_button_draw_frame(renderer,&rect,&style);ui_draw_text_clipped(renderer,rect.x+5,rect.y+3,label,theme->text_primary,.8f,rect.w-10);
}
// Paints the selected chain browser/save modal without loading files or rendering audio.
void daw_fx_library_panel_render(SDL_Renderer* renderer,const AppState* state) {
    if(!state->fx_library_ui.open)return;DawFxLibraryLayout layout;daw_fx_library_panel_layout(state,&layout);DawFxLibraryUI* ui=(DawFxLibraryUI*)&state->fx_library_ui;DawThemePalette theme;daw_shared_theme_resolve_palette(&theme);
    SDL_SetRenderDrawColor(renderer,theme.inspector_fill.r,theme.inspector_fill.g,theme.inspector_fill.b,255);SDL_RenderFillRect(renderer,&layout.panel);
    ui_draw_text_clipped(renderer,layout.panel.x+8,layout.panel.y+8,ui->saving?(ui->update?"Update loaded chain":"Save chain as..."):ui->captured.track_id?"Track effect-chain library":"Master effect-chain library",theme.text_primary,.9f,layout.panel.w-16);
    daw_control_frame(renderer,&layout.field,theme.control_fill,theme.control_border);daw_text_edit_draw(renderer,&ui->text_edit,ui->saving?ui->name:ui->search,96,ui->cursor,layout.field,theme.text_primary,theme.control_fill,.8f);
    const char* labels[]={ui->saving?"Save":"Save As",ui->saving?"Back":"Update",ui->saving?"":"Refresh","Close"};for(int i=0;i<4;++i)button(renderer,layout.buttons[i],labels[i],&theme);
    ui_draw_text_clipped(renderer,layout.status.x,layout.status.y,ui->message,theme.text_muted,.75f,layout.status.w);
    for(int i=0;i<layout.count;++i){const DawFxPreset* preset=&ui->library->items[layout.indices[i]];snprintf(ui->rows[i],sizeof(ui->rows[i]),"%s | %d effects",preset->name,preset->chain.count);button(renderer,layout.rows[i],ui->rows[i],&theme);}
}
// Frees only this library's cached entries after its application consumer has stopped.
void daw_fx_library_panel_release(AppState* state) { if(!state)return;close_panel(state);free(state->fx_library_ui.library);state->fx_library_ui.library=NULL; }
