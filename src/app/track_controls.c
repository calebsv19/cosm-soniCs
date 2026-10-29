#include "daw/track_controls.h"
#include "daw/track_mix.h"
#include "app_state.h"
#include "input/project_modal_input.h"
#include "ui/effects_panel.h"
#include "core/loop/daw_render_invalidation.h"
#include <math.h>
#include <string.h>

// Keeps recording, authoring, text and continuous owners ahead of remote header edits; playback is allowed.
static bool busy(const AppState* s) {
    return s->undo.active_drag_valid || s->bounce_active || daw_audio_recording_is_active(&s->audio_recording) ||
        s->midi_editor_ui.qwerty_record_armed || s->midi_editor_ui.drag_active || s->midi_editor_ui.marquee_active ||
        s->midi_editor_ui.note_press_pending || s->midi_editor_ui.shift_note_pending || s->track_name_editor.editing ||
        project_modal_input_active(s) || s->layout_runtime.drag.active || daw_workspace_authoring_host_active(&s->workspace_authoring);
}
// Rejects truncated or control-character names while retaining UTF-8 bytes and spaces exactly.
static bool valid_name(const char* name) {
    if (!name || !name[0] || strlen(name) >= ENGINE_CLIP_NAME_MAX) return false;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p) if (*p < 32 || *p == 127) return false;
    return true;
}
// Resolves only a track handle before copying all truthful mixer inspection fields.
DawActionStatus daw_track_controls_get(const AppState* s, DawActionTarget target, DawTrackControls* out) {
    if (!out || target.kind != DAW_ACTION_TARGET_TRACK) return DAW_ACTION_INVALID;
    int t, c; DawActionStatus status = daw_action_target_resolve(s, target, &t, &c);
    if (status != DAW_ACTION_UNCHANGED) return status;
    const EngineTrack* row = &engine_get_tracks(s->engine)[t];
    *out = (DawTrackControls){.target=target, .persistent_id=row->persistent_id, .gain=row->gain, .pan=row->pan,
        .muted=row->muted, .solo=row->solo, .is_group=row->is_group, .is_return=row->is_return};
    memcpy(out->name, row->name, sizeof(out->name)); return DAW_ACTION_UNCHANGED;
}
// Requires the same project/track and only the selected scalar or name, allowing unrelated native edits.
bool daw_track_controls_equal(const DawTrackControls* a, const DawTrackControls* b, DawTrackControl control) {
    if (!a || !b || a->target.project_generation != b->target.project_generation || a->target.track_id != b->target.track_id ||
        a->target.clip_id || b->target.clip_id || a->target.kind != DAW_ACTION_TARGET_TRACK || b->target.kind != DAW_ACTION_TARGET_TRACK) return false;
    return control == DAW_TRACK_NAME ? !strcmp(a->name,b->name) : control == DAW_TRACK_GAIN ? a->gain==b->gain : control == DAW_TRACK_PAN && a->pan==b->pan;
}
// Restores labels through lifetime and exact-name guards without replacing other track state.
bool daw_track_name_write(AppState* s, DawActionTarget target, const char* expected, const char* name) {
    int t,c;
    if (!expected || !valid_name(name) || daw_action_target_resolve(s,target,&t,&c)!=DAW_ACTION_UNCHANGED ||
        target.kind!=DAW_ACTION_TARGET_TRACK || strcmp(engine_get_tracks(s->engine)[t].name,expected)) return false;
    if (!engine_track_set_name(s->engine,t,name)) return false;
    effects_panel_sync_from_engine(s); daw_invalidate_all(s->panes,s->pane_count,DAW_RENDER_INVALIDATION_CONTENT);
    daw_request_full_redraw(DAW_RENDER_INVALIDATION_CONTENT); return true;
}
// Validates one request and publishes only after the undo slot has been reserved.
DawActionStatus daw_track_controls_set(AppState* s,const DawTrackControls* expected,DawTrackControl control,const char* name,float value) {
    if (!expected || control<DAW_TRACK_NAME || control>DAW_TRACK_PAN ||
        (control==DAW_TRACK_NAME ? !valid_name(name) : !isfinite(value) || (control==DAW_TRACK_GAIN ? value<0 || value>10 : value<-1 || value>1))) return DAW_ACTION_INVALID;
    DawTrackControls current; DawActionStatus status=daw_track_controls_get(s,expected->target,&current);
    if (status!=DAW_ACTION_UNCHANGED) return status;
    if (!daw_track_controls_equal(expected,&current,control)) return DAW_ACTION_STALE_TARGET;
    if (busy(s)) return DAW_ACTION_BUSY;
    if (control==DAW_TRACK_NAME ? !strcmp(name,current.name) : value==(control==DAW_TRACK_GAIN ? current.gain : current.pan)) return DAW_ACTION_UNCHANGED;
    UndoCommand command={0};
    if (control==DAW_TRACK_NAME) {
        command.type=UNDO_CMD_TRACK_RENAME; command.data.track_rename.target=current.target;
        strcpy(command.data.track_rename.before_name,current.name); strcpy(command.data.track_rename.after_name,name);
    } else {
        command.type=UNDO_CMD_TRACK_MIX;
        command.data.track_mix=(DawTrackMixEdit){current.target.project_generation,current.target.track_id,control==DAW_TRACK_PAN,control==DAW_TRACK_GAIN ? current.gain : current.pan,value};
    }
    if (!undo_manager_begin_drag(&s->undo,&command)) return DAW_ACTION_HISTORY_UNAVAILABLE;
    bool accepted=control==DAW_TRACK_NAME ? daw_track_name_write(s,current.target,current.name,name) :
        daw_track_mix_write(s,&command.data.track_mix,command.data.track_mix.before,value);
    if (!accepted) { undo_manager_cancel_drag(&s->undo); return DAW_ACTION_ENGINE_REJECTED; }
    return undo_manager_commit_drag(&s->undo,&s->undo.active_drag) ? DAW_ACTION_APPLIED : DAW_ACTION_HISTORY_UNAVAILABLE;
}
// Refuses to skip any different command, field or target at the shared undo/redo cursor.
DawActionStatus daw_track_controls_history(AppState* s,const DawTrackControls* expected,DawTrackControl control,bool redo) {
    if (!expected || control<DAW_TRACK_NAME || control>DAW_TRACK_PAN) return DAW_ACTION_INVALID;
    DawTrackControls current; DawActionStatus status=daw_track_controls_get(s,expected->target,&current);
    if (status!=DAW_ACTION_UNCHANGED) return status;
    if (!daw_track_controls_equal(expected,&current,control)) return DAW_ACTION_STALE_TARGET;
    if (busy(s)) return DAW_ACTION_BUSY;
    int count=redo ? s->undo.redo_count : s->undo.undo_count;
    const UndoCommand* stack=redo ? s->undo.redo_stack : s->undo.undo_stack;
    if (!count) return DAW_ACTION_HISTORY_UNAVAILABLE;
    const UndoCommand* cmd=&stack[count-1]; uint64_t generation=0,id=0;
    if (control==DAW_TRACK_NAME && cmd->type==UNDO_CMD_TRACK_RENAME) {
        generation=cmd->data.track_rename.target.project_generation; id=cmd->data.track_rename.target.track_id;
    } else if (control!=DAW_TRACK_NAME && cmd->type==UNDO_CMD_TRACK_MIX && cmd->data.track_mix.pan==(control==DAW_TRACK_PAN)) {
        generation=cmd->data.track_mix.generation; id=cmd->data.track_mix.track_id;
    }
    if (generation!=current.target.project_generation || id!=current.target.track_id) return DAW_ACTION_HISTORY_UNAVAILABLE;
    return (redo ? undo_manager_redo(&s->undo,s) : undo_manager_undo(&s->undo,s)) ? DAW_ACTION_APPLIED : DAW_ACTION_STALE_TARGET;
}
