#include "daw/eq_actions.h"
#include "app_state.h"
#include "daw/midi_actions.h"
#include "engine/groups.h"
#include "input/project_modal_input.h"
#include "core/loop/daw_render_invalidation.h"
#include <math.h>

// Resolves master or one exact runtime track without retaining array indices in requests.
static int eq_track(const AppState* s,uint64_t id) { return id ? engine_group_index(engine_get_tracks(s->engine),engine_get_track_count(s->engine),id) : -1; }
// Rejects nonfinite/unbounded stored values and enabled frequencies above the actual DSP limit.
static bool valid(const AppState* s,const EngineEqCurve* c) {
    if(!c)return false;const EngineRuntimeConfig* cfg=engine_get_config(s->engine);float upper=fminf(20000,cfg->sample_rate*.45f);
    const EngineEqCut* cuts[]={&c->low_cut,&c->high_cut};
    for(int i=0;i<2;++i)if(!isfinite(cuts[i]->freq_hz) || cuts[i]->freq_hz<20 || cuts[i]->freq_hz>20000 || (cuts[i]->enabled && cuts[i]->freq_hz>upper))return false;
    for(int i=0;i<ENGINE_EQ_BANDS;++i){const EngineEqBand* b=&c->bands[i];if(!isfinite(b->freq_hz) || b->freq_hz<20 || b->freq_hz>20000 ||
        (b->enabled && b->freq_hz>upper) || !isfinite(b->gain_db) || b->gain_db<-20 || b->gain_db>20 || !isfinite(b->q_width) || b->q_width<.1f || b->q_width>4)return false;}
    return true;
}
// Copies only meaningful engine fields from persisted UI-compatible history.
static EngineEqCurve engine_curve(const SessionEqCurve* c) {
    EngineEqCurve out={.low_cut={c->low_cut.enabled,c->low_cut.freq_hz},.high_cut={c->high_cut.enabled,c->high_cut.freq_hz}};
    for(int i=0;i<ENGINE_EQ_BANDS;++i)out.bands[i]=(EngineEqBand){c->bands[i].enabled,c->bands[i].freq_hz,c->bands[i].gain_db,c->bands[i].q_width};return out;
}
// Updates semantic fields while retaining fixed-slope metadata and UI selection state.
static void session_curve(SessionEqCurve* dst,const EngineEqCurve* c) {
    dst->low_cut.enabled=c->low_cut.enabled;dst->low_cut.freq_hz=c->low_cut.freq_hz;
    dst->high_cut.enabled=c->high_cut.enabled;dst->high_cut.freq_hz=c->high_cut.freq_hz;
    for(int i=0;i<ENGINE_EQ_BANDS;++i)dst->bands[i]=(SessionEqBand){c->bands[i].enabled,c->bands[i].freq_hz,c->bands[i].gain_db,c->bands[i].q_width};
}
// Synchronizes displayed/cache parameters without disturbing pane or selected handles.
static void ui_curve(EqCurveState* dst,const EngineEqCurve* c) {
    dst->low_cut.enabled=c->low_cut.enabled;dst->low_cut.freq_hz=c->low_cut.freq_hz;
    dst->high_cut.enabled=c->high_cut.enabled;dst->high_cut.freq_hz=c->high_cut.freq_hz;
    for(int i=0;i<ENGINE_EQ_BANDS;++i){dst->bands[i].enabled=c->bands[i].enabled;dst->bands[i].freq_hz=c->bands[i].freq_hz;dst->bands[i].gain_db=c->bands[i].gain_db;dst->bands[i].q_width=c->bands[i].q_width;}
}
// Reads exact accepted curve parameters only on the engine control owner.
DawActionStatus daw_eq_get(const AppState* s,uint64_t generation,uint64_t id,DawEqSnapshot* out) {
    if(!s || !s->engine || !out)return DAW_ACTION_INVALID;if(!engine_is_control_thread(s->engine))return DAW_ACTION_WRONG_THREAD;
    if(generation!=engine_instance_id(s->engine))return DAW_ACTION_STALE_TARGET;
    int t=eq_track(s,id);if(id && t<0)return DAW_ACTION_STALE_TARGET;
    *out=(DawEqSnapshot){.generation=generation,.track_id=id};return engine_get_eq_curve(s->engine,t,&out->curve) ? DAW_ACTION_UNCHANGED : DAW_ACTION_ENGINE_REJECTED;
}
// Compares semantic curve values without padding, slope metadata or mutable UI drafts.
bool daw_eq_equal(const DawEqSnapshot* a,const DawEqSnapshot* b) {
    if(!a || !b || a->generation!=b->generation || a->track_id!=b->track_id)return false;
    const EngineEqCurve* x=&a->curve,*y=&b->curve;
    if(x->low_cut.enabled!=y->low_cut.enabled || x->low_cut.freq_hz!=y->low_cut.freq_hz || x->high_cut.enabled!=y->high_cut.enabled || x->high_cut.freq_hz!=y->high_cut.freq_hz)return false;
    for(int i=0;i<ENGINE_EQ_BANDS;++i)if(x->bands[i].enabled!=y->bands[i].enabled || x->bands[i].freq_hz!=y->bands[i].freq_hz || x->bands[i].gain_db!=y->bands[i].gain_db || x->bands[i].q_width!=y->bands[i].q_width)return false;
    return true;
}
// Guarded publication is shared by native continuous edits, discrete MCP edits and history.
bool daw_eq_write(AppState* s,const DawEqSnapshot* expected,const EngineEqCurve* next) {
    DawEqSnapshot current;if(!expected || daw_eq_get(s,expected->generation,expected->track_id,&current)!=DAW_ACTION_UNCHANGED ||
        !daw_eq_equal(expected,&current) || !valid(s,next))return false;
    int t=eq_track(s,expected->track_id);if(!(t<0 ? engine_set_master_eq_curve(s->engine,next) : engine_set_track_eq_curve(s->engine,t,next)))return false;
    EffectsPanelState* p=&s->effects_panel;
    if(t<0)ui_curve(&p->eq_curve_master,next);else if(p->eq_curve_tracks && t<p->eq_curve_tracks_count)ui_curve(&p->eq_curve_tracks[t],next);
    if((t<0 && p->eq_detail.view_mode==EQ_DETAIL_VIEW_MASTER) || (t>=0 && p->eq_detail.view_mode==EQ_DETAIL_VIEW_TRACK && p->target==FX_PANEL_TARGET_TRACK && p->target_track_index==t))ui_curve(&p->eq_curve,next);
    daw_invalidate_all(s->panes,s->pane_count,DAW_RENDER_INVALIDATION_CONTENT);daw_request_full_redraw(DAW_RENDER_INVALIDATION_CONTENT);return true;
}
// Captures accepted history parameters before either UI or MCP can publish.
bool daw_eq_bind_history(AppState* s,UndoEqCurveEdit* edit) {
    if(!s || !s->engine || !edit)return false;
    edit->generation=engine_instance_id(s->engine);
    if(!edit->is_master){if(edit->track_index<0 || edit->track_index>=engine_get_track_count(s->engine))return false;edit->track_runtime_id=engine_get_tracks(s->engine)[edit->track_index].runtime_id;}
    DawEqSnapshot current;if(daw_eq_get(s,edit->generation,edit->is_master ? 0 : edit->track_runtime_id,&current)!=DAW_ACTION_UNCHANGED)return false;
    session_curve(&edit->before,&current.curve);edit->after=edit->before;return true;
}
// Restores only the dedicated curve and refuses a changed current curve or recycled project.
bool daw_eq_restore(AppState* s,const UndoEqCurveEdit* edit,bool after) {
    if(!edit)return false;DawEqSnapshot expected={edit->generation,edit->is_master ? 0 : edit->track_runtime_id,engine_curve(after ? &edit->before : &edit->after)};
    EngineEqCurve desired=engine_curve(after ? &edit->after : &edit->before);return daw_eq_write(s,&expected,&desired);
}
// Keeps structural authoring, recording and modal/text owners ahead of discrete remote EQ edits.
static bool busy(const AppState* s) {return daw_midi_actions_busy(s) || s->track_name_editor.editing || project_modal_input_active(s) || daw_workspace_authoring_host_active(&s->workspace_authoring);}
// Reserves shared history before publishing one complete dedicated curve.
DawActionStatus daw_eq_set(AppState* s,const DawEqSnapshot* expected,const EngineEqCurve* next) {
    if(!expected)return DAW_ACTION_INVALID;DawEqSnapshot current;DawActionStatus status=daw_eq_get(s,expected->generation,expected->track_id,&current);
    if(status!=DAW_ACTION_UNCHANGED)return status;if(!daw_eq_equal(expected,&current))return DAW_ACTION_STALE_TARGET;
    if(!valid(s,next))return DAW_ACTION_INVALID;if(busy(s))return DAW_ACTION_BUSY;
    DawEqSnapshot desired=current;desired.curve=*next;if(daw_eq_equal(&current,&desired))return DAW_ACTION_UNCHANGED;
    UndoCommand cmd={.type=UNDO_CMD_EQ_CURVE};cmd.data.eq_curve_edit.is_master=!current.track_id;cmd.data.eq_curve_edit.track_index=eq_track(s,current.track_id);
    cmd.data.eq_curve_edit.before.low_cut.slope=cmd.data.eq_curve_edit.before.high_cut.slope=12;
    if(!daw_eq_bind_history(s,&cmd.data.eq_curve_edit))return DAW_ACTION_STALE_TARGET;session_curve(&cmd.data.eq_curve_edit.after,next);
    if(!undo_manager_begin_drag(&s->undo,&cmd))return DAW_ACTION_HISTORY_UNAVAILABLE;
    if(!daw_eq_write(s,&current,next)){undo_manager_cancel_drag(&s->undo);return DAW_ACTION_ENGINE_REJECTED;}
    return undo_manager_commit_drag(&s->undo,&s->undo.active_drag) ? DAW_ACTION_APPLIED : DAW_ACTION_HISTORY_UNAVAILABLE;
}
// Refuses to skip a foreign shared-history command and validates its exact curve before replay.
DawActionStatus daw_eq_history(AppState* s,const DawEqSnapshot* expected,bool redo) {
    if(!expected)return DAW_ACTION_INVALID;DawEqSnapshot current;DawActionStatus status=daw_eq_get(s,expected->generation,expected->track_id,&current);
    if(status!=DAW_ACTION_UNCHANGED)return status;if(!daw_eq_equal(expected,&current))return DAW_ACTION_STALE_TARGET;if(busy(s))return DAW_ACTION_BUSY;
    int n=redo ? s->undo.redo_count : s->undo.undo_count;const UndoCommand* stack=redo ? s->undo.redo_stack : s->undo.undo_stack;
    if(!n || stack[n-1].type!=UNDO_CMD_EQ_CURVE)return DAW_ACTION_HISTORY_UNAVAILABLE;
    const UndoEqCurveEdit* edit=&stack[n-1].data.eq_curve_edit;
    if(edit->generation!=current.generation || (edit->is_master ? 0 : edit->track_runtime_id)!=current.track_id)return DAW_ACTION_HISTORY_UNAVAILABLE;
    return (redo ? undo_manager_redo(&s->undo,s) : undo_manager_undo(&s->undo,s)) ? DAW_ACTION_APPLIED : DAW_ACTION_STALE_TARGET;
}
