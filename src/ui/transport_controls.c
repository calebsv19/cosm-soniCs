#include "ui/transport_controls.h"
#include "app_state.h"
#include "input/project_modal_input.h"
#include "input/library_input.h"
#include "input/inspector_input.h"
#include "kit_ui_interaction_sdl.h"
#include "ui/font.h"
#include <math.h>

// Borrows the same rectangles that the transport renderer and product layout own.
static SDL_Rect bounds(const TransportUI* ui, DawTransportAction action) {
    const SDL_Rect rects[]={ui->load_rect,ui->save_rect,ui->play_rect,ui->stop_rect,
        ui->grid_rect,ui->beat_toggle_rect,ui->fit_width_rect,ui->fit_height_rect};
    return action<DAW_TRANSPORT_ACTION_COUNT?rects[action]:(SDL_Rect){0};
}

// Text/modal/authoring owners keep their existing priority over background commands.
static bool blocked(const AppState* state) {
    return state->bounce_active || project_modal_input_active(state) ||
        state->tempo_ui.editing || state->track_name_editor.editing ||
        library_input_is_editing(state) || inspector_input_has_text_focus(state) ||
        daw_workspace_authoring_host_active(&state->workspace_authoring);
}

void daw_transport_controls_sync(TransportUI* ui, const AppState* state) {
    if(!ui || !state)return;
    if(ui->action_engine!=state->engine) {
        ui->action_engine=state->engine;ui->control_generation+=2;
    }
    // Captured presses cannot follow a control into changed layout geometry.
    for(unsigned i=0;i<ui->controls.count;i++) {
        KitUiInteractionControl* control=&ui->controls.controls[i];
        if(control->id!=ui->controls.interaction.captured_id &&
           control->id!=ui->controls.interaction.key_owner_id)continue;
        SDL_Rect next=bounds(ui,(DawTransportAction)ui->controls.keys[i].value);
        KitRenderRect old=control->bounds;
        if(old.x!=next.x || old.y!=next.y || old.width!=next.w || old.height!=next.h) {
            KitUiInteractionEvent cancel={.type=KIT_UI_INTERACTION_CANCEL};
            KitUiInteractionResult ignored;
            (void)kit_ui_surface_route(&ui->controls,&cancel,&ignored);
        }
    }
    bool unavailable=blocked(state) || !state->engine;
    uint32_t scope=ui->control_generation+(unavailable?1:0);
    (void)kit_ui_focus_scope_sync(&ui->control_focus,&ui->controls,scope,unavailable);
    kit_ui_surface_begin(&ui->controls,scope);
    KitRenderRect clip={0,0,state->window_width,state->window_height};
    if(!unavailable)for(unsigned i=0;i<DAW_TRANSPORT_ACTION_COUNT;i++) {
        SDL_Rect r=bounds(ui,(DawTransportAction)i);
        if(r.w>0 && r.h>0)(void)kit_ui_surface_register(&ui->controls,
            (KitUiSurfaceKey){1,i},(KitRenderRect){r.x,r.y,r.w,r.h},
            clip.width>0 && clip.height>0?&clip:NULL,1,NULL);
    }
    (void)kit_ui_surface_end(&ui->controls);
    kit_ui_focus_scope_restore(&ui->control_focus,&ui->controls);
}

bool daw_transport_controls_event(AppState* state, const SDL_Event* event) {
    if(!state || !event)return false;
    TransportUI* ui=&state->transport_ui;daw_transport_controls_sync(ui,state);
    // Space already toggles accepted playback intent and Shift+Space seeks.
    if((event->type==SDL_KEYDOWN || event->type==SDL_KEYUP) &&
       event->key.keysym.sym==SDLK_SPACE)return false;
    KitUiInteractionEvent input;KitUiInteractionResult result;
    if(!kit_ui_interaction_event_from_sdl(event,&input))return false;
    // The active text/modal owner receives new keys; old background releases still drain.
    if ((blocked(state) || !state->engine) && input.type == KIT_UI_INTERACTION_KEY_DOWN) return false;
    if(event->type==SDL_MOUSEBUTTONDOWN && (SDL_GetModState()&KMOD_SHIFT))
        input.modifiers|=KIT_UI_INTERACTION_MOD_SHIFT;
    (void)kit_ui_surface_route(&ui->controls,&input,&result);
    if(result.consumed && (input.type==KIT_UI_INTERACTION_POINTER_DOWN ||
                          input.type==KIT_UI_INTERACTION_KEY_DOWN))
        ui->pressed_modifiers=input.modifiers;
    if(result.activated_id && kit_ui_surface_take_activation(&ui->controls,result.activated_id)) {
        KitUiSurfaceKey key;
        if(kit_ui_surface_key(&ui->controls,result.activated_id,&key) && key.domain==1 &&
           key.value<DAW_TRANSPORT_ACTION_COUNT)
            transport_input_activate_control(state,(DawTransportAction)key.value,ui->pressed_modifiers);
        daw_transport_controls_sync(ui,state);
    }
    return result.consumed!=0;
}

void daw_transport_control_draw(SDL_Renderer* renderer, const TransportUI* ui,
    DawTransportAction action, const char* label, bool active, bool hovered,
    const DawThemePalette* palette) {
    if(!renderer || !ui || !label || !palette)return;
    SDL_Rect r=bounds(ui,action);if(r.w<=0 || r.h<=0)return;
    DawUiButtonSpec spec;daw_ui_button_spec_init(&spec,label);
    spec.state.selected=active;spec.state.hovered=hovered;
    for(unsigned i=0;i<ui->controls.count;i++)if(ui->controls.keys[i].value==(uint64_t)action)
        spec.state=kit_ui_interaction_button_state(&ui->controls.interaction,&ui->controls.controls[i],active);
    DawThemePalette colors=*palette;
    if(action==DAW_TRANSPORT_FIT_WIDTH || action==DAW_TRANSPORT_FIT_HEIGHT)
        colors.control_fill=colors.slider_track;
    if(action==DAW_TRANSPORT_BEATS || action==DAW_TRANSPORT_FIT_WIDTH || action==DAW_TRANSPORT_FIT_HEIGHT)
        colors.control_hover_fill=active?colors.control_active_fill:colors.control_fill;
    DawUiButtonStyle style;if(daw_ui_button_style_resolve(&colors,&spec,&style))return;
    KitUiButtonAppearance appearance;
    kit_ui_button_appearance_preset(KIT_UI_BUTTON_APPEARANCE_COMPACT_ROUNDED,&appearance);
    float radius=kit_ui_corner_radius_clamp(appearance.corner_radius,r.w,r.h);
    SDL_SetRenderDrawColor(renderer,style.outline.r,style.outline.g,style.outline.b,style.outline.a);
    vk_renderer_fill_rounded_rect((VkRenderer*)renderer,&(SDL_FRect){r.x,r.y,r.w,r.h},radius);
    KitRenderRect inset=kit_ui_rect_inset((KitRenderRect){r.x,r.y,r.w,r.h},appearance.border_thickness);
    SDL_SetRenderDrawColor(renderer,style.fill.r,style.fill.g,style.fill.b,style.fill.a);
    vk_renderer_fill_rounded_rect((VkRenderer*)renderer,
        &(SDL_FRect){inset.x,inset.y,inset.width,inset.height},
        kit_ui_corner_radius_for_inset(radius,appearance.border_thickness));
    float scale=1;int height=ui_font_line_height(scale);
    if(height>r.h-4 && height>0){scale=fmaxf(4,r.h-4)/height;height=ui_font_line_height(scale);}
    int measured=ui_measure_text_width(label,scale),padding=4;
    int x=r.x+(int)fmaxf(padding,(r.w-measured)/2.f),y=r.y+(int)fmaxf(2,(r.h-height)/2.f);
    SDL_Color ink={style.text.r,style.text.g,style.text.b,style.text.a};
    // Captions draw synchronously; pending PLAY/STOP labels never enter a frame queue.
    ui_draw_text_clipped(renderer,x,y,label,ink,scale,(int)fmaxf(1,r.x+r.w-padding-x));
    if(spec.state.focused) {
        KitRenderRect marker;
        if(kit_ui_interaction_focus_marker(&ui->controls.interaction,ui->controls.controls,
            ui->controls.count,&marker)) {
            SDL_SetRenderDrawColor(renderer,ink.r,ink.g,ink.b,ink.a);
            SDL_Rect focus_rect = {marker.x, marker.y, marker.width, marker.height};
            SDL_RenderFillRect(renderer, &focus_rect);
        }
    }
}
