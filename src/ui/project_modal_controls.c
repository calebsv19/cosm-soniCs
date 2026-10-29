#include "ui/project_modal_controls.h"
#include "app_state.h"
#include "kit_ui_interaction_sdl.h"
#include "ui/daw_ui_button.h"
#include "ui/font.h"
#include <stdio.h>
#include <string.h>

// Shares bounded modal geometry between painters and all native hit targets.
void daw_project_modal_layout(const AppState* s,DawProjectModalLayout* l) {
    memset(l,0,sizeof(*l));int w=s->window_width>0?s->window_width:800,h=s->window_height>0?s->window_height:600;
    int bw=w-24;if(bw>720)bw=720;if(bw<300)bw=300;
    int bh=s->project_load.active ? 460 : 320;if(bh>h-24)bh=h-24;if(bh<280)bh=280;
    l->box=(SDL_Rect){(w-bw)/2,(h-bh)/2,bw,bh};SDL_Rect b=l->box;
    l->list=(SDL_Rect){b.x+16,b.y+116,b.w/2-24,b.h-176};
    l->info=(SDL_Rect){b.x+b.w/2+8,b.y+116,b.w/2-24,b.h-176};
    l->field=(SDL_Rect){b.x+16,b.y+116,b.w-32,40};
    int width=(b.w-56)/3;
    l->buttons[0]=(SDL_Rect){b.x+16,b.y+b.h-48,width,32};
    l->buttons[1]=(SDL_Rect){b.x+b.w-width-16,b.y+b.h-48,width,32};
    l->buttons[2]=(SDL_Rect){b.x+16,b.y+48,40,30};
    l->buttons[3]=(SDL_Rect){b.x+b.w-56,b.y+48,40,30};
    l->buttons[4]=(SDL_Rect){b.x+64,b.y+48,b.w-128,30};
    l->buttons[5]=(SDL_Rect){b.x+28+width,b.y+b.h-48,width,32};
}
// Preserves the original load/cancel helper for existing callers.
void daw_project_modal_buttons(const AppState* s,SDL_Rect* load,SDL_Rect* cancel) {
    DawProjectModalLayout l;daw_project_modal_layout(s,&l);*load=l.buttons[0];*cancel=l.buttons[1];
}
// Cancels held controls whenever scope, mode, selection, engine or geometry changes.
void daw_project_modal_controls_sync(AppState* s) {
    DawProjectModalControls* ui=&s->project_modal_controls;
    int active=s->project_load.active?1:s->project_prompt.active?(s->project_prompt.folder_mode?3:2):0;
    if(ui->active!=active || ui->selected!=s->project_load.selected_index || ui->width!=s->window_width ||
        ui->height!=s->window_height || ui->engine!=s->engine || ui->collection_revision!=s->project_collections.revision)++ui->generation;
    ui->active=active;ui->selected=s->project_load.selected_index;ui->width=s->window_width;ui->height=s->window_height;
    ui->engine=s->engine;ui->collection_revision=s->project_collections.revision;
    kit_ui_surface_begin(&ui->surface,ui->generation);
    if(active) {
        DawProjectModalLayout l;daw_project_modal_layout(s,&l);
        for(int i=0;i<6;++i) {
            bool enabled=true;
            if(i==0) enabled=active!=1 || (ui->selected>=0 && ui->selected<s->project_load.count);
            if(i>=2 && i<=4)enabled=active!=3 && s->project_collections.count>0;
            if(i==4)enabled=active!=3;
            if(i==5)enabled=active==2 && s->project.has_name && s->project.path[0];
            SDL_Rect r=l.buttons[i];
            (void)kit_ui_surface_register(&ui->surface,(KitUiSurfaceKey){2,i+1},
                (KitRenderRect){r.x,r.y,r.w,r.h},NULL,enabled,NULL);
        }
    }
    (void)kit_ui_surface_end(&ui->surface);
}
// Uses matching release ownership for project actions and collection selector controls.
int daw_project_modal_controls_event(AppState* s,const SDL_Event* e,int* action) {
    *action=0;daw_project_modal_controls_sync(s);KitUiInteractionEvent input;KitUiInteractionResult result;
    if(!kit_ui_interaction_event_from_sdl(e,&input))return 0;
    (void)kit_ui_surface_route(&s->project_modal_controls.surface,&input,&result);
    if(result.activated_id && kit_ui_surface_take_activation(&s->project_modal_controls.surface,result.activated_id)) {
        KitUiSurfaceKey key;if(kit_ui_surface_key(&s->project_modal_controls.surface,result.activated_id,&key))*action=(int)key.value;
    }
    return result.consumed;
}
// Paints the same selector and action controls registered by the modal input owner.
void daw_project_modal_controls_draw(SDL_Renderer* renderer,AppState* s) {
    daw_project_modal_controls_sync(s);DawThemePalette theme;if(!daw_shared_theme_resolve_palette(&theme))return;
    DawProjectModalLayout l;daw_project_modal_layout(s,&l);
    bool folder=s->project_prompt.active && s->project_prompt.folder_mode;
    const char* label=s->project_collections.count ? s->project_collections.items[s->project_collections.selected].label : "Choose collections folder";
    const char* labels[]={s->project_load.active?"Load":folder?"Use folder":"Save new version","Cancel","<",">",label,"Update current"};
    KitUiSurface* surface=&s->project_modal_controls.surface;
    for(int i=0;i<6;++i) {
        if((folder && i>=2) || (s->project_load.active && i==5))continue;
        SDL_Rect r=l.buttons[i];DawUiButtonSpec spec;daw_ui_button_spec_init(&spec,labels[i]);
        spec.state=kit_ui_interaction_button_state(&surface->interaction,&surface->controls[i],i==0);
        DawUiButtonStyle style;if(daw_ui_button_style_resolve(&theme,&spec,&style))continue;
        SDL_SetRenderDrawColor(renderer,style.fill.r,style.fill.g,style.fill.b,style.fill.a);SDL_RenderFillRect(renderer,&r);
        SDL_SetRenderDrawColor(renderer,style.outline.r,style.outline.g,style.outline.b,style.outline.a);SDL_RenderDrawRect(renderer,&r);
        ui_draw_text_clipped(renderer,r.x+8,r.y+8,labels[i],(SDL_Color){style.text.r,style.text.g,style.text.b,style.text.a},1,r.w-16);
    }
}
