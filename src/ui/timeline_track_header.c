#include "ui/timeline_track_header.h"
#include "app_state.h"
#include "ui/font.h"
#include "ui/text_edit.h"
#include <math.h>
#include <stdio.h>

// Paints a compact button without borrowing text beyond the current submission.
static void badge(SDL_Renderer* r,SDL_Rect box,const char* text,bool on,const TimelineTheme* theme) {
    SDL_Color fill=on ? theme->clip_border_selected : theme->button_fill;
    SDL_SetRenderDrawColor(r,fill.r,fill.g,fill.b,fill.a);SDL_RenderFillRect(r,&box);
    ui_draw_text_clipped(r,box.x+3,box.y+2,text,theme->text,.85f,box.w-6);
}
// Distinguishes occupied track rows from the canvas without turning them into bright color blocks.
SDL_Color timeline_track_lane_color(const AppState* s,int t,const TimelineTheme* theme) {
    const EngineTrack* track=&engine_get_tracks(s->engine)[t];SDL_Color color=theme->bg;
    int shade=6+(int)(track->persistent_id%3)*3;
    int direction=((int)color.r+color.g+color.b)>384 ? -1 : 1;
    color.r=(Uint8)fminf(255,fmaxf(0,color.r+direction*shade));
    color.g=(Uint8)fminf(255,fmaxf(0,color.g+direction*(shade+1)));
    color.b=(Uint8)fminf(255,fmaxf(0,color.b+direction*(shade+3)));
    if(s->active_track_index==t){SDL_Color selected=theme->lane_header_fill_active;
        color.r=(color.r*2+selected.r)/3;color.g=(color.g*2+selected.g)/3;color.b=(color.b*2+selected.b)/3;}
    return color;
}
// Keeps controls near the name even when the timeline is zoomed to tall rows.
void timeline_track_header_render(SDL_Renderer* r,AppState* s,int t,const TimelineTrackHeaderLayout* l,const TimelineTheme* theme) {
    const EngineTrack* track=&engine_get_tracks(s->engine)[t];
    SDL_Color fill=timeline_track_lane_color(s,t,theme);
    SDL_SetRenderDrawColor(r,fill.r,fill.g,fill.b,fill.a);SDL_RenderFillRect(r,&l->header_rect);
    SDL_SetRenderDrawColor(r,theme->lane_header_border.r,theme->lane_header_border.g,theme->lane_header_border.b,theme->lane_header_border.a);
    SDL_RenderDrawLine(r,l->header_rect.x,l->header_rect.y+l->header_rect.h-1,l->header_rect.x+l->header_rect.w,l->header_rect.y+l->header_rect.h-1);
    int stripe=l->header_rect.x+2;
    SDL_SetRenderDrawColor(r,track->is_group ? 118 : 70,track->is_group ? 150 : 95,track->is_group ? 190 : 128,255);
    SDL_RenderDrawLine(r,stripe,l->header_rect.y+3,stripe,l->header_rect.y+l->header_rect.h-4);
    if(track->is_group)ui_draw_text(r,l->fold_rect.x,l->fold_rect.y,track->folded ? ">" : "v",theme->text,1);
    char fallback[32];snprintf(fallback,sizeof(fallback),"Track %d",t+1);
    const char* name=track->name[0] ? track->name : fallback;
    int width=l->text_max_x-l->text_x;
    if(s->track_name_editor.editing && s->track_name_editor.track_index==t) {
        TrackNameEditor* e=&s->track_name_editor;SDL_Rect view={l->text_x,l->text_y,width,ui_font_line_height(1)};
        daw_text_edit_draw(r,&e->text_edit,e->buffer,sizeof(e->buffer),e->cursor,view,theme->text,theme->text,1);
    }else ui_draw_text_clipped(r,l->text_x,l->text_y,name,track->muted ? theme->text_muted : theme->text,1,width);
    badge(r,l->mute_rect,"M",track->muted,theme);badge(r,l->solo_rect,"S",track->solo,theme);badge(r,l->effects_rect,"FX",false,theme);
    char volume[32],pan[32];
    if(track->gain<=0)snprintf(volume,sizeof(volume),"V -inf");else snprintf(volume,sizeof(volume),"V %+.1f",20*log10f(track->gain));
    if(fabsf(track->pan)<.005f)snprintf(pan,sizeof(pan),"P C");else snprintf(pan,sizeof(pan),"P %c%d",track->pan<0 ? 'L' : 'R',(int)lroundf(fabsf(track->pan)*100));
    badge(r,l->gain_rect,volume,false,theme);badge(r,l->pan_rect,pan,false,theme);
    float position=track->gain<=0 ? 0 : fminf(1,fmaxf(0,(20*log10f(track->gain)+60)/80));
    SDL_SetRenderDrawColor(r,106,142,185,220);
    SDL_RenderDrawLine(r,l->gain_rect.x+2,l->gain_rect.y+l->gain_rect.h-2,l->gain_rect.x+2+(int)((l->gain_rect.w-4)*position),l->gain_rect.y+l->gain_rect.h-2);
    int pan_x=l->pan_rect.x+2+(int)((l->pan_rect.w-4)*(track->pan+1)*.5f);
    SDL_RenderDrawLine(r,pan_x,l->pan_rect.y+l->pan_rect.h-3,pan_x,l->pan_rect.y+l->pan_rect.h-1);
    if(s->header_mixer.focus_id==track->runtime_id && s->header_mixer.focus_generation==engine_instance_id(s->engine)) {
        SDL_Rect focus=s->header_mixer.focus_pan ? l->pan_rect : l->gain_rect;
        SDL_SetRenderDrawColor(r,theme->clip_border_selected.r,theme->clip_border_selected.g,theme->clip_border_selected.b,255);SDL_RenderDrawRect(r,&focus);
    }
    if(l->meter_rect.w>0) {
        SDL_SetRenderDrawColor(r,42,47,55,255);SDL_RenderFillRect(r,&l->meter_rect);
        EngineMeterSnapshot meter={0};
        if(engine_get_track_meter_snapshot(s->engine,t,&meter) && meter.runtime_id==track->runtime_id && isfinite(meter.peak)) {
            SDL_Rect bar=l->meter_rect;float value=fminf(1,fmaxf(0,meter.peak));bar.w=(int)(bar.w*value);
            SDL_SetRenderDrawColor(r,meter.clipped ? 230 : 74,meter.clipped ? 80 : 182,99,255);SDL_RenderFillRect(r,&bar);
        }
    }
}
