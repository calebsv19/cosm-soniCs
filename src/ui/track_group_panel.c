#include "ui/track_group_panel.h"
#include "input/timeline/timeline_geometry.h"
#include "app_state.h"
#include "daw/routing_actions.h"
#include "engine/groups.h"
#include "input/timeline_selection.h"
#include "ui/effects_panel.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include <stdio.h>
#include <string.h>
// Defines responsive manager rectangles shared by native painting and pointer targeting.
// Uses one centered layout with fixed parallel commands and a scrollable owned lane list.
GroupPanelLayout daw_group_panel_layout(const AppState* s) {
    int w=s->window_width<780 ? s->window_width-20 : 760, h=s->window_height<590 ? s->window_height-20 : 570;
    if(w<300)w=300;if(h<250)h=250;
    GroupPanelLayout l={.panel={(s->window_width-w)/2,(s->window_height-h)/2,w,h}};
    int bw=(w-40)/3;for(int i=0;i<12;++i)l.buttons[i]=(SDL_Rect){l.panel.x+10+(i%3)*(bw+10),l.panel.y+36+(i/3)*32,bw,26};
    int top=l.panel.y+174;l.row_count=(h-220)/26;if(l.row_count>12)l.row_count=12;if(l.row_count<1)l.row_count=1;
    for(int i=0;i<l.row_count;++i)l.rows[i]=(SDL_Rect){l.panel.x+10,top+i*26,w-20,24};return l;
}
// Reports domain refusal without discarding the current project or staged choice.
static void outcome(AppState* s,DawActionStatus status) {
    if(status>DAW_ACTION_UNCHANGED)snprintf(s->group_panel.error,sizeof(s->group_panel.error),"Change refused (%d): refresh, stop playback/gestures, then retry.",status);
    else s->group_panel.error[0]=0;
}
// Captures a fresh exact hierarchy without making changes or forcing any project save.
void daw_group_panel_open(AppState* s) {
    if(!s || !s->engine)return;
    s->group_panel.open=true;s->group_panel.pressed=-1;
    int t=s->selected_track_index;const EngineTrack* rows=engine_get_tracks(s->engine);
    s->group_panel.selected_id=t>=0 && t<engine_get_track_count(s->engine) ? rows[t].runtime_id : 0;
    s->group_panel.parent_id=t>=0 && t<engine_get_track_count(s->engine) ? rows[t].parent_id : 0;
    s->group_panel.error[0]=0;(void)daw_action_get_routing(s,&s->group_panel.captured);
}
// Uses guarded view metadata and routes selection to the newly collapsed header.
void daw_group_toggle(AppState* s,uint64_t identity) {
    EngineRoutingSnapshot current,next,out;
    if(daw_action_get_routing(s,&current)!=DAW_ACTION_UNCHANGED)return;
    int t=engine_group_index(engine_get_tracks(s->engine),current.track_count,identity);
    if(t<0 || !current.tracks[t].is_group)return;
    bool folding=!current.tracks[t].folded;
    int slot=-1;for(int i=0;i<128;++i)if(s->group_selection[i].group_id==identity){slot=i;break;}
    if(slot<0)for(int i=0;i<128;++i)if(!s->group_selection[i].group_id || s->group_selection[i].generation!=current.generation){slot=i;break;}
    int selected=s->selected_track_index;uint64_t child=0,clip=0;
    const EngineTrack* tracks=engine_get_tracks(s->engine);
    if(folding && selected>=0 && selected<current.track_count && selected!=t && engine_group_descendant(tracks,current.track_count,selected,t)) {
        child=tracks[selected].runtime_id;int c=s->selected_clip_index;
        if(c>=0 && c<tracks[selected].clip_count)clip=tracks[selected].clips[c].creation_index;
    }
    next=current;next.tracks[t].folded=folding;
    DawActionStatus status=daw_action_set_routing(s,&current,&next,&out);outcome(s,status);
    if(status!=DAW_ACTION_APPLIED)return;
    if(folding) {
        if(slot>=0){s->group_selection[slot].group_id=identity;s->group_selection[slot].generation=current.generation;s->group_selection[slot].track_id=child;s->group_selection[slot].clip_id=clip;}
        timeline_selection_set_single(s,t,-1);
    }else if(slot>=0 && s->selected_track_index==t && s->group_selection[slot].generation==current.generation) {
        int target=engine_group_index(tracks,current.track_count,s->group_selection[slot].track_id),c=-1;
        if(target>=0 && engine_group_descendant(tracks,current.track_count,target,t)) {
            if(engine_group_visible_row(tracks,current.track_count,target)<0) {
                while(target>=0 && engine_group_visible_row(tracks,current.track_count,target)<0)target=engine_group_index(tracks,current.track_count,tracks[target].parent_id);
            }else for(int i=0;i<tracks[target].clip_count;++i)if(tracks[target].clips[i].creation_index==s->group_selection[slot].clip_id){c=i;break;}
            if(target>=0)timeline_selection_set_single(s,target,c);
        }
        s->group_selection[slot].track_id=0;
    }
    timeline_set_vertical_scroll(s,s->timeline_vertical_scroll);
    effects_panel_sync_from_engine(s);
}
// Moves one sibling in the projected ordering while keeping all stable audio/FX indices intact.
static void reorder(AppState* s,int track,int direction) {
    EngineRoutingSnapshot next=s->group_panel.captured,out;
    int siblings[ENGINE_ROUTING_TRACK_CAP],count=0;
    for(int t=0;t<next.track_count;++t)if(next.tracks[t].parent_id==next.tracks[track].parent_id)siblings[count++]=t;
    for(int a=0;a<count;++a)for(int b=a+1;b<count;++b)if(next.tracks[siblings[b]].sibling_order<next.tracks[siblings[a]].sibling_order) {
        int tmp=siblings[a];siblings[a]=siblings[b];siblings[b]=tmp;
    }
    int at=0;while(at<count && siblings[at]!=track)++at;int to=at+direction;
    if(to<0 || to>=count)return;int tmp=siblings[at];siblings[at]=siblings[to];siblings[to]=tmp;
    for(int i=0;i<count;++i)next.tracks[siblings[i]].sibling_order=i;
    outcome(s,daw_action_set_routing(s,&s->group_panel.captured,&next,&out));
}
// Performs one named creation, guarded parent edit, view edit or scoped history action.
static void action(AppState* s,int button) {
    const char* names[]={"Strings","Woodwinds","Brass","Major Theme","Minor Answer"};EngineRoutingSnapshot out;
    if(button==11){s->group_panel.open=false;return;}
    if(button<5) {
        DawActionStatus status=daw_action_create_group(s,&s->group_panel.captured,names[button],&out);outcome(s,status);
        if(status==DAW_ACTION_APPLIED) { s->group_panel.selected_id=out.tracks[out.track_count-1].track_id;s->group_panel.parent_id=0;
            timeline_selection_set_single(s,out.track_count-1,-1);effects_panel_sync_from_engine(s); }
        return;
    }
    int t=engine_group_index(engine_get_tracks(s->engine),engine_get_track_count(s->engine),s->group_panel.selected_id);
    if(button==9 || button==10){outcome(s,daw_action_routing_history(s,&s->group_panel.captured,button==10,&out));return;}
    if(t<0)return;
    if(button==5) {
        int current=engine_group_index(engine_get_tracks(s->engine),s->group_panel.captured.track_count,s->group_panel.parent_id);
        for(int p=current+1;p<s->group_panel.captured.track_count;++p)if(p!=t && s->group_panel.captured.tracks[p].is_group &&
            !engine_group_descendant(engine_get_tracks(s->engine),s->group_panel.captured.track_count,p,t)) {
            s->group_panel.parent_id=s->group_panel.captured.tracks[p].track_id;return;
        }
        s->group_panel.parent_id=0;
    } else if(button==6) {
        EngineRoutingSnapshot next=s->group_panel.captured;next.tracks[t].parent_id=s->group_panel.parent_id;
        outcome(s,daw_action_set_routing(s,&s->group_panel.captured,&next,&out));
    } else if(button==7)reorder(s,t,-1);
    else if(button==8)reorder(s,t,1);
}
// Keeps matching press/release ownership and all modal pointer input exclusive.
bool daw_group_panel_event(AppState* s,const SDL_Event* event) {
    if(!s || !s->group_panel.open)return false;
    if(event->type==SDL_WINDOWEVENT){s->group_panel.pressed=-1;return false;}
    if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE){s->group_panel.open=false;return true;}
    if(event->type==SDL_MOUSEWHEEL){s->group_panel.scroll-=event->wheel.y;int max=engine_get_track_count(s->engine)-daw_group_panel_layout(s).row_count;
        if(max<0)max=0;if(s->group_panel.scroll<0)s->group_panel.scroll=0;if(s->group_panel.scroll>max)s->group_panel.scroll=max;s->group_panel.pressed=-1;return true;}
    GroupPanelLayout l=daw_group_panel_layout(s);int hit=-1;SDL_Point p;
    if(event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP) {
        p=(SDL_Point){event->button.x,event->button.y};for(int i=0;i<12;++i)if(SDL_PointInRect(&p,&l.buttons[i]))hit=i;
        for(int i=0;i<l.row_count;++i)if(SDL_PointInRect(&p,&l.rows[i]) && i+s->group_panel.scroll<s->group_panel.captured.track_count)hit=100+i+s->group_panel.scroll;
        if(event->button.button!=SDL_BUTTON_LEFT)return true;
        if(event->type==SDL_MOUSEBUTTONDOWN){s->group_panel.pressed=hit;s->group_panel.width=s->window_width;s->group_panel.height=s->window_height;}
        else { int press=s->group_panel.pressed;s->group_panel.pressed=-1;
            EngineRoutingSnapshot current;bool valid=daw_action_get_routing(s,&current)==DAW_ACTION_UNCHANGED && engine_routing_snapshot_equal(&current,&s->group_panel.captured);
            if(hit>=0 && hit==press && valid && s->group_panel.width==s->window_width && s->group_panel.height==s->window_height) {
                if(hit<100)action(s,hit);else {int t=hit-100;s->group_panel.selected_id=current.tracks[t].track_id;s->group_panel.parent_id=current.tracks[t].parent_id;
                    timeline_selection_set_single(s,t,-1);effects_panel_sync_from_engine(s);}
            }
        }
    }
    return event->type!=SDL_QUIT;
}
// Paints current hierarchy names with immediate copied text and a shared parent/list geometry.
void daw_group_panel_render(SDL_Renderer* renderer,AppState* s) {
    if(!s || !s->group_panel.open)return;
    EngineRoutingSnapshot current;if(daw_action_get_routing(s,&current)!=DAW_ACTION_UNCHANGED)return;
    if(!engine_routing_snapshot_equal(&current,&s->group_panel.captured))s->group_panel.pressed=-1;
    s->group_panel.captured=current;GroupPanelLayout l=daw_group_panel_layout(s);SDL_Color ink={225,227,235,255};
    SDL_SetRenderDrawColor(renderer,25,29,39,255);SDL_RenderFillRect(renderer,&l.panel);
    ui_draw_text(renderer,l.panel.x+10,l.panel.y+8,"Track Groups — select a lane, choose Parent, Apply",ink,1);
    const char* labels[]={"+ Strings","+ Woodwinds","+ Brass","+ Major Theme","+ Minor Answer","Parent","Apply Parent","Move Up","Move Down","Undo Group Edit","Redo Group Edit","Close"};
    char parent_label[160];int parent=engine_group_index(engine_get_tracks(s->engine),current.track_count,s->group_panel.parent_id);
    const EngineTrack* tracks=engine_get_tracks(s->engine);snprintf(parent_label,sizeof(parent_label),"Parent: %s",parent<0 ? "Master" : tracks[parent].name);
    for(int i=0;i<12;++i){SDL_SetRenderDrawColor(renderer,49,56,71,255);SDL_RenderFillRect(renderer,&l.buttons[i]);
        ui_draw_text_clipped(renderer,l.buttons[i].x+5,l.buttons[i].y+4,i==5 ? parent_label : labels[i],ink,1,l.buttons[i].w-10);}
    for(int i=0;i<l.row_count && i+s->group_panel.scroll<current.track_count;++i){int t=i+s->group_panel.scroll;
        SDL_SetRenderDrawColor(renderer,tracks[t].runtime_id==s->group_panel.selected_id ? 65 : 35,43,58,255);SDL_RenderFillRect(renderer,&l.rows[i]);
        char text[320];int p=engine_group_index(tracks,current.track_count,tracks[t].parent_id);
        snprintf(text,sizeof(text),"%s%s  [%s]",tracks[t].is_group ? "Bus: " : "Track: ",tracks[t].name,p<0 ? "Master" : tracks[p].name);
        ui_draw_text_clipped(renderer,l.rows[i].x+4,l.rows[i].y+3,text,ink,1,l.rows[i].w-8);}
    ui_draw_text_clipped(renderer,l.panel.x+10,l.panel.y+l.panel.h-30,s->group_panel.error,ink,1,l.panel.w-20);
}
