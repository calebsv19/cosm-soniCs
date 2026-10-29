#include "daw/track_mix.h"
#include "app_state.h"
#include "engine/groups.h"
#include "ui/effects_panel.h"
#include "core/loop/daw_render_invalidation.h"
#include <math.h>

// Uses process/track lifetimes and an exact accepted scalar to reject stale writes before publication.
bool daw_track_mix_write(AppState* s,const DawTrackMixEdit* edit,float expected,float value) {
    if(!s || !s->engine || !edit || !engine_is_control_thread(s->engine) || edit->generation!=engine_instance_id(s->engine) ||
        !isfinite(value) || (edit->pan ? value < -1 || value > 1 : value < 0))return false;
    int t=engine_group_index(engine_get_tracks(s->engine),engine_get_track_count(s->engine),edit->track_id);if(t<0)return false;
    const EngineTrack* track=&engine_get_tracks(s->engine)[t];float current=edit->pan ? track->pan : track->gain;
    if(current!=expected)return false;
    bool accepted=edit->pan ? engine_track_set_pan(s->engine,t,value) : engine_track_set_gain(s->engine,t,value);
    if(accepted && s->effects_panel.target==FX_PANEL_TARGET_TRACK && s->effects_panel.target_track_index==t) {
        if(edit->pan)s->effects_panel.track_snapshot.pan=value;else s->effects_panel.track_snapshot.gain=value;
    }
    if(accepted){daw_invalidate_all(s->panes,s->pane_count,DAW_RENDER_INVALIDATION_CONTENT);daw_request_full_redraw(DAW_RENDER_INVALIDATION_CONTENT);}
    return accepted;
}
// Restores only the owned gain/pan parameter, with no instrument/effect snapshot replacement.
bool daw_track_mix_history_apply(AppState* s,const DawTrackMixEdit* edit,bool after) {
    if(!edit || !daw_track_mix_write(s,edit,after ? edit->before : edit->after,after ? edit->after : edit->before))return false;
    effects_panel_sync_from_engine(s);return true;
}
