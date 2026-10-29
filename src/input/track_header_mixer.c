#include "input/track_header_mixer.h"
#include "app_state.h"
#include "daw/track_mix.h"
#include "engine/groups.h"
#include "input/timeline/timeline_geometry.h"
#include "input/project_modal_input.h"
#include "input/library_input.h"
#include "input/inspector_input.h"
#include "ui/layout.h"
#include "ui/knob_drag.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// Resolves the current visible rectangle without relying on a recycled array index.
static bool target(AppState* s,uint64_t id,bool pan,int* track,SDL_Rect* bounds) {
    if(!s || !s->engine)return false;
    const EngineTrack* rows=engine_get_tracks(s->engine);int n=engine_get_track_count(s->engine);
    int t=engine_group_index(rows,n,id);if(t<0 || engine_group_visible_row(rows,n,t)<0)return false;
    const Pane* p=ui_layout_get_pane(s,1);TimelineGeometry g;TimelineTrackHeaderLayout l;
    if(!p || !p->visible || !timeline_compute_geometry(s,p,&g))return false;
    timeline_track_header_layout(s,t,&p->rect,timeline_track_lane_y(s,t,g.track_top,g.track_height,g.track_spacing),g.track_height,g.header_width,&l);
    SDL_Rect field=pan ? l.pan_rect : l.gain_rect,visible;
    if(!SDL_IntersectRect(&field,&g.viewport,&visible))return false;
    *track=t;*bounds=field;return true;
}
// Keeps modal, authoring and competing continuous owners ahead of mixer control input.
static bool blocked(AppState* s) {
    return !s->engine || s->bounce_active || s->group_panel.open || project_modal_input_active(s) ||
        s->routing_ui.open || s->fx_library_ui.open || s->sound_ui.open || s->midi_pattern_ui.open || s->midi_arrangement_ui.open ||
        s->preset_ui.editing || s->preset_ui.save_open || s->track_name_editor.editing || s->tempo_ui.editing ||
        s->midi_editor_ui.qwerty_record_armed || s->midi_editor_ui.drag_active || s->midi_editor_ui.marquee_active ||
        s->midi_editor_ui.note_press_pending || s->midi_editor_ui.shift_note_pending || daw_audio_recording_is_active(&s->audio_recording) ||
        s->layout_runtime.drag.active || library_input_is_editing(s) || inspector_input_has_text_focus(s) || daw_workspace_authoring_host_active(&s->workspace_authoring);
}
// Checks that the reserved history slot still belongs to this exact gesture.
static bool owns(AppState* s) {
    DawTrackHeaderMixer* m=&s->header_mixer;
    return m->dragging && s->undo.active_drag_valid && s->undo.drag_serial==m->serial &&
        s->undo.active_drag.type==UNDO_CMD_TRACK_MIX && s->undo.active_drag.data.track_mix.generation==m->generation &&
        s->undo.active_drag.data.track_mix.track_id==m->track_id;
}
// Commits the accepted scalar once; Escape reverts only against the last owned accepted value.
void daw_track_header_mixer_finish(AppState* s,bool revert) {
    if(!s)return;
    if(owns(s)) {
        DawTrackMixEdit* edit=&s->undo.active_drag.data.track_mix;
        if(revert && edit->after!=edit->before && daw_track_mix_write(s,edit,edit->after,edit->before))edit->after=edit->before;
        if(edit->after!=edit->before) {
            if(!undo_manager_commit_drag(&s->undo,&s->undo.active_drag))return;
        }else undo_manager_cancel_drag(&s->undo);
    }
    if(s->header_mixer.dragging)SDL_CaptureMouse(SDL_FALSE);
    s->header_mixer.dragging=false;s->header_mixer.wait_release=false;
}
// Ends stale geometry/lifetime ownership before another frame can apply motion to a different row.
void daw_track_header_mixer_sync(AppState* s) {
    if(!s)return;
    DawTrackHeaderMixer* m=&s->header_mixer;int t;SDL_Rect field;
    if(m->dragging && (!s->engine || m->generation!=engine_instance_id(s->engine) || !owns(s) || blocked(s) ||
        !target(s,m->track_id,m->pan,&t,&field) || memcmp(&field,&m->bounds,sizeof(field))))daw_track_header_mixer_finish(s,false);
    if(m->focus_id && (!s->engine || m->focus_generation!=engine_instance_id(s->engine) || blocked(s) ||
        !target(s,m->focus_id,m->focus_pan,&t,&field)))m->focus_id=0;
}
// Reserves one scalar command before any engine publication, including live playback updates.
static bool begin(AppState* s,int track,bool pan,SDL_Rect field,int y,float scale) {
    if(s->undo.active_drag_valid)return false;
    const EngineTrack* row=&engine_get_tracks(s->engine)[track];float value=pan ? row->pan : row->gain;
    UndoCommand c={.type=UNDO_CMD_TRACK_MIX};
    c.data.track_mix=(DawTrackMixEdit){.generation=engine_instance_id(s->engine),.track_id=row->runtime_id,.pan=pan,.before=value,.after=value};
    if(!undo_manager_begin_drag(&s->undo,&c))return false;
    DawTrackHeaderMixer* m=&s->header_mixer;m->dragging=true;m->generation=c.data.track_mix.generation;m->track_id=row->runtime_id;
    SDL_CaptureMouse(SDL_TRUE);
    m->serial=s->undo.drag_serial;m->pan=pan;m->bounds=field;m->start_y=y;m->scale=scale;
    m->start_position=pan ? (value+1)*.5f : (20*log10f(fmaxf(value,.001f))+60)/80;
    return true;
}
// Advances only the owned scalar and leaves failed engine publication visible at its last accepted value.
static void update(AppState* s,float value) {
    if(!owns(s))return;
    DawTrackMixEdit* edit=&s->undo.active_drag.data.track_mix;
    if(value!=edit->after && daw_track_mix_write(s,edit,edit->after,value))edit->after=value;
}
// Converts a bounded dB position into gain, with the bottom position representing silence.
static float gain(float position) {return position<=0 ? 0 : powf(10,(-60+80*position)/20);}
// Routes relative drags, reset and focused arrow keys without replaying sampled timeline selection.
bool daw_track_header_mixer_event(AppState* s,const SDL_Event* e) {
    if(!s || !e)return false;DawTrackHeaderMixer* m=&s->header_mixer;
    bool active=m->dragging;daw_track_header_mixer_sync(s);
    if(active && !m->dragging && (e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEBUTTONUP))return true;
    if(m->wait_release) {
        if(e->type==SDL_MOUSEBUTTONUP && e->button.button==SDL_BUTTON_LEFT)m->wait_release=false;
        return e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP || e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEWHEEL;
    }
    if(m->dragging) {
        if(e->type==SDL_MOUSEMOTION){if(abs(e->motion.y-m->start_y)<2){if(owns(s))update(s,s->undo.active_drag.data.track_mix.before);return true;}float p=daw_knob_drag_position(m->start_position,m->start_y,e->motion.y,m->scale);update(s,m->pan ? -1+2*p : gain(p));return true;}
        if(e->type==SDL_MOUSEBUTTONUP && e->button.button==SDL_BUTTON_LEFT){daw_track_header_mixer_finish(s,false);return true;}
        if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE){daw_track_header_mixer_finish(s,true);return true;}
        // Let keyboard releases reach the audition owner so held notes/pedals cannot stick.
        return e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEWHEEL || e->type==SDL_KEYDOWN;
    }
    if(blocked(s) || s->undo.active_drag_valid)return false;
    if(e->type==SDL_KEYDOWN && m->focus_id) {
        int t;SDL_Rect field;SDL_Keycode key=e->key.keysym.sym;
        if(key==SDLK_ESCAPE || key==SDLK_TAB){m->focus_id=0;return false;}
        if(key!=SDLK_LEFT && key!=SDLK_RIGHT && key!=SDLK_UP && key!=SDLK_DOWN)return false;
        if(e->key.repeat)return true;
        if(!target(s,m->focus_id,m->focus_pan,&t,&field))return false;
        float direction=(key==SDLK_RIGHT || key==SDLK_UP) ? 1 : -1;
        float fine=(e->key.keysym.mod & KMOD_SHIFT) ? .1f : 1;
        const EngineTrack* row=&engine_get_tracks(s->engine)[t];
        float db=fminf(20,fmaxf(-60,20*log10f(fmaxf(.001f,row->gain))+direction*fine));
        float value=m->focus_pan ? fminf(1,fmaxf(-1,row->pan+direction*.05f*fine)) : (db<=-60 ? 0 : powf(10,db/20));
        if(begin(s,t,m->focus_pan,field,0,1)){update(s,value);daw_track_header_mixer_finish(s,false);}return true;
    }
    if(e->type!=SDL_MOUSEBUTTONDOWN || e->button.button!=SDL_BUTTON_LEFT)return false;
    m->focus_id=0;const Pane* pane=ui_layout_get_pane(s,1);TimelineGeometry g;SDL_Point p={e->button.x,e->button.y};
    if(!pane || !timeline_compute_geometry(s,pane,&g) || !SDL_PointInRect(&p,&g.viewport))return false;
    if(p.x>=pane->rect.x+g.header_width)return false;
    int t=timeline_track_at_position(s,p.y,g.track_height,g.track_spacing);if(t<0)return false;
    const EngineTrack* rows=engine_get_tracks(s->engine);
    for(int control=0;control<2;++control) {
        SDL_Rect field;int found;
        if(!target(s,rows[t].runtime_id,control!=0,&found,&field) || !SDL_PointInRect(&p,&field))continue;
        m->focus_id=rows[t].runtime_id;m->focus_generation=engine_instance_id(s->engine);m->focus_pan=control!=0;
        if(begin(s,t,control!=0,field,p.y,(SDL_GetModState() & KMOD_SHIFT) ? .1f : 1)) {
            if(e->button.clicks>1){update(s,control ? 0 : 1);daw_track_header_mixer_finish(s,false);m->wait_release=true;}
        }
        return true;
    }
    return false;
}
