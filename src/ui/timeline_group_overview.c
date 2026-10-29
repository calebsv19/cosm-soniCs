#include "ui/timeline_group_overview.h"
#include "app_state.h"
#include "engine/groups.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline_selection.h"
#include "ui/layout.h"
#include "ui/effects_panel.h"
#include "ui/track_group_panel.h"
#include "ui/waveform_render.h"
#include <math.h>
#include <limits.h>

// Projects only playable source spans, retaining real gaps and excluding ancestors/independent returns.
static bool project_item(const AppState* s,const TimelineGeometry* geometry,int lane_y,int group,int track,int clip,TimelineGroupOverviewItem* out) {
    if(!s || !s->engine || !out)return false;
    const EngineTrack* rows=engine_get_tracks(s->engine);int n=engine_get_track_count(s->engine);
    if(group<0 || group>=n || !rows[group].is_group || track<0 || track>=n || rows[track].is_group || rows[track].is_return ||
        !engine_group_descendant(rows,n,track,group) || clip<0 || clip>=rows[track].clip_count)return false;
    TimelineGeometry g=*geometry;
    const EngineClip* c=&rows[track].clips[clip];int rate=engine_get_config(s->engine)->sample_rate;if(rate<=0 || !c->duration_frames)return false;
    uint64_t frames=c->duration_frames;
    if(c->kind==ENGINE_CLIP_KIND_AUDIO) {
        if(!c->media || c->offset_frames>=c->media->frame_count)return false;
        uint64_t available=c->media->frame_count-c->offset_frames;if(frames>available)frames=available;
    }
    double start=(double)c->timeline_start_frames/rate,end=start+(double)frames/rate;
    double left=fmax(start,g.window_start_seconds),right=fmin(end,(g.window_start_seconds+g.visible_seconds));if(right<=left)return false;
    int x=g.content_left+(int)floor((left-g.window_start_seconds)*g.pixels_per_second);
    int xend=g.content_left+(int)ceil((right-g.window_start_seconds)*g.pixels_per_second);
    if(x<g.content_left)x=g.content_left;if(xend>g.content_left+g.content_width)xend=g.content_left+g.content_width;
    if(xend<=x)return false;
    int y=lane_y;
    SDL_Rect rect={x,y+5,xend-x,g.track_height-10};SDL_Rect visible;
    if(!SDL_IntersectRect(&rect,&g.viewport,&visible))return false;
    uint64_t skip=(uint64_t)llround((left-start)*rate);if(skip>=frames)return false;
    uint64_t length=(uint64_t)llround((right-left)*rate);if(length>frames-skip)length=frames-skip;
    *out=(TimelineGroupOverviewItem){.rect=rect,.source_start=c->offset_frames+skip,.frames=length,.track=track,.clip=clip,.midi=c->kind==ENGINE_CLIP_KIND_MIDI};return length>0;
}
// Computes standalone projection geometry for inspection and behavior qualification.
bool timeline_group_overview_item(const AppState* s,int group,int track,int clip,TimelineGroupOverviewItem* out) {
    if(!s || !s->engine)return false;
    const Pane* pane=ui_layout_get_pane(s,1);TimelineGeometry g;
    if(!pane || !timeline_compute_geometry(s,pane,&g) || engine_group_visible_row(engine_get_tracks(s->engine),engine_get_track_count(s->engine),group)<0)return false;
    return project_item(s,&g,timeline_track_lane_y(s,group,g.track_top,g.track_height,g.track_spacing),group,track,clip,out);
}
// Draws note timing in the group's common pitch range, honoring source offsets and trimmed region bounds.
static void notes(SDL_Renderer* r,const EngineClip* c,const TimelineGroupOverviewItem* item,int low,int high) {
    if(high<=low)high=low+1;
    for(int i=0;i<c->midi_notes.note_count;++i) {
        const EngineMidiNote* note=&c->midi_notes.notes[i];uint64_t end=note->start_frame+note->duration_frames;
        if(end<note->start_frame)end=UINT64_MAX;
        uint64_t a=note->start_frame>item->source_start ? note->start_frame : item->source_start;
        uint64_t limit=item->source_start+item->frames,b=end<limit ? end : limit;if(b<=a)continue;
        int x=item->rect.x+(int)((double)(a-item->source_start)/item->frames*item->rect.w);
        int right=item->rect.x+(int)ceil((double)(b-item->source_start)/item->frames*item->rect.w);if(right<=x)right=x+1;
        int y=item->rect.y+2+(int)((double)(high-note->note)/(high-low)*(item->rect.h-5));
        SDL_SetRenderDrawColor(r,110,198,180,210);SDL_RenderDrawLine(r,x,y,right-1,y);
    }
}
// Reuses ordinary source waveform storage; this overview represents arrangement, not processed bus output.
void timeline_group_overview_render(SDL_Renderer* r,AppState* s,int group,const TimelineTheme* theme) {
    const EngineTrack* rows=engine_get_tracks(s->engine);int n=engine_get_track_count(s->engine);
    const Pane* pane=ui_layout_get_pane(s,1);TimelineGeometry g;if(!pane || !timeline_compute_geometry(s,pane,&g))return;
    int lane_y=timeline_track_lane_y(s,group,g.track_top,g.track_height,g.track_spacing);
    bool midi=false,audio=false;int low=127,high=0;
    for(int t=0;t<n;++t)for(int c=0;c<rows[t].clip_count;++c) {
        TimelineGroupOverviewItem item;if(!project_item(s,&g,lane_y,group,t,c,&item))continue;
        if(item.midi) {midi=true;const EngineClip* source=&rows[t].clips[c];
            for(int i=0;i<source->midi_notes.note_count;++i) {int pitch=source->midi_notes.notes[i].note;if(pitch<low)low=pitch;if(pitch>high)high=pitch;}
        }else audio=true;
    }
    for(int t=0;t<n;++t)for(int c=0;c<rows[t].clip_count;++c) {
        TimelineGroupOverviewItem item;if(!project_item(s,&g,lane_y,group,t,c,&item))continue;
        if(midi && audio){int half=item.rect.h/2;if(item.midi)item.rect.h=half;else {item.rect.y+=half+1;item.rect.h-=half+1;}}
        if(item.rect.h<5)continue;
        SDL_SetRenderDrawColor(r,item.midi ? 50 : 65,item.midi ? 95 : 74,item.midi ? 83 : 110,85);SDL_RenderFillRect(r,&item.rect);
        const EngineClip* source=&rows[t].clips[c];
        if(item.midi)notes(r,source,&item,low,high);
        else waveform_render_view(r,&s->waveform_cache,source->media,engine_clip_get_media_path(source),&item.rect,item.source_start,item.frames,(SDL_Color){128,165,216,170});
        SDL_Color line=s->active_track_index==group ? theme->clip_border_selected : theme->clip_border;
        SDL_SetRenderDrawColor(r,line.r,line.g,line.b,line.a);SDL_RenderDrawRect(r,&item.rect);
    }
}
// Selects the bus as the overview's owner; its child regions remain individually authored.
void timeline_group_overview_activate(AppState* s,uint64_t identity,bool expand) {
    int t=engine_group_index(engine_get_tracks(s->engine),engine_get_track_count(s->engine),identity);if(t<0)return;
    const EngineTrack* track=&engine_get_tracks(s->engine)[t];
    timeline_selection_set_single(s,t,-1);effects_panel_sync_from_engine(s);
    if(expand && track->is_group && track->folded)daw_group_toggle(s,identity);
}
