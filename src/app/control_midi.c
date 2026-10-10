#include "control_bridge_internal.h"
#include "daw/midi_patterns.h"
#include "daw/midi_expression.h"
#include "app_state.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Creates a domain outcome without borrowing mutable project data.
static cJSON* outcome(bool ok, const char* code) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddBoolToObject(object, "ok", ok);
    cJSON_AddStringToObject(object, "code", code);
    return object;
}
// Returns a typed string property without coercion.
static const char* text(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}
// Rejects unknown and duplicate event properties before parsing values.
static bool fields(const cJSON* object, const char* const* allowed) {
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON* item = object->child; item; item = item->next) {
        bool found = false;
        for (int i = 0; allowed[i]; ++i) if (!strcmp(item->string, allowed[i])) found = true;
        if (!found) return false;
        for (const cJSON* next = item->next; next; next = next->next)
            if (!strcmp(item->string, next->string)) return false;
    }
    return true;
}
// Preserves frame positions as unsigned decimal strings across JSON clients.
static void id(cJSON* object, const char* key, uint64_t value) {
    char buffer[32]; snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    cJSON_AddStringToObject(object, key, buffer);
}
// Parses canonical decimal frame values without floating-point conversion or overflow.
static bool integer_string(const cJSON* object, const char* key, uint64_t* out) {
    const char* value = text(object, key);
    if (!value || !*value || strlen(value) > 20 || (value[0] == '0' && value[1])) return false;
    for (const char* p = value; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0; char* end; unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || *end) return false;
    *out = (uint64_t)parsed; return true;
}
// Parses a finite bounded number while refusing Boolean and fractional integer fields.
static bool number(const cJSON* object, const char* key, double min, double max, bool integral, double* out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < min ||
        value->valuedouble > max || (integral && floor(value->valuedouble) != value->valuedouble)) return false;
    *out = value->valuedouble; return true;
}
// Compares full semantic region state without hashes, padding, array slots or rounded JSON values.
static bool same(const DawMidiSnapshot* a, const DawMidiSnapshot* b) {
    if (a->target.project_generation != b->target.project_generation || a->target.track_id != b->target.track_id ||
        a->target.clip_id != b->target.clip_id || a->target.kind != b->target.kind || a->start_frame != b->start_frame ||
        a->duration_frames != b->duration_frames || a->offset_frames != b->offset_frames || a->note_count != b->note_count ||
        a->tempo.sample_rate != b->tempo.sample_rate || a->tempo.event_count != b->tempo.event_count) return false;
    for (int i = 0; i < a->note_count; ++i)
        if (a->notes[i].start_frame != b->notes[i].start_frame || a->notes[i].duration_frames != b->notes[i].duration_frames ||
            a->notes[i].note != b->notes[i].note || a->notes[i].velocity != b->notes[i].velocity) return false;
    for (int i = 0; i < a->tempo.event_count; ++i)
        if (a->tempo.events[i].beat != b->tempo.events[i].beat || a->tempo.events[i].bpm != b->tempo.events[i].bpm) return false;
    return true;
}
// Finds an endpoint-bound exact copied state; expired tokens require a new read.
static DawControlMidiState* find(DawControlBridge* bridge, const char* token) {
    if (!token) return NULL;
    for (int i = 0; i < DAW_CONTROL_MIDI_STATES; ++i)
        if (bridge->midi_states[i].token[0] && !strcmp(token, bridge->midi_states[i].token)) return &bridge->midi_states[i];
    return NULL;
}
// Transfers a prepared snapshot into the bounded endpoint cache and reuses identical states.
static DawControlMidiState* remember(DawControlBridge* bridge, DawMidiSnapshot* snapshot) {
    for (int i = 0; i < DAW_CONTROL_MIDI_STATES; ++i)
        if (bridge->midi_states[i].token[0] && same(snapshot, &bridge->midi_states[i].snapshot)) return &bridge->midi_states[i];
    if (bridge->midi_sequence == UINT64_MAX) return NULL;
    DawControlMidiState* entry = &bridge->midi_states[bridge->midi_cursor++ % DAW_CONTROL_MIDI_STATES];
    daw_midi_snapshot_free(&entry->snapshot); entry->snapshot = *snapshot; memset(snapshot, 0, sizeof(*snapshot));
    snprintf(entry->token, sizeof(entry->token), "%s:midi:%" PRIu64, bridge->instance, ++bridge->midi_sequence);
    return entry;
}
// Copies fixed metadata and bounded notes/tempo pages from one exact retained snapshot.
static void page(cJSON* result, const DawControlMidiState* entry, int offset, int tempo_offset) {
    const DawMidiSnapshot* s = &entry->snapshot;
    cJSON_AddStringToObject(result, "expected_state", entry->token);
    cJSON* region = cJSON_AddObjectToObject(result, "region");
    cJSON* target = cJSON_AddObjectToObject(region, "target");
    id(target, "generation", s->target.project_generation); id(target, "track_id", s->target.track_id);
    id(target, "clip_id", s->target.clip_id); cJSON_AddStringToObject(target, "kind", "midi_region");
    id(region, "start_frame", s->start_frame); id(region, "duration_frames", s->duration_frames); id(region, "offset_frames", s->offset_frames);
    cJSON_AddNumberToObject(region, "sample_rate", s->tempo.sample_rate);
    cJSON_AddNumberToObject(region, "note_count", s->note_count);
    cJSON_AddNumberToObject(region, "tempo_event_count", s->tempo.event_count);
    cJSON_AddNumberToObject(result, "offset", offset); cJSON_AddNumberToObject(result, "tempo_offset", tempo_offset);
    cJSON* notes = cJSON_AddArrayToObject(result, "notes");
    int end = offset + DAW_CONTROL_MIDI_NOTE_PAGE;
    if (end > s->note_count) end = s->note_count;
    for (int i = offset; i < end; ++i) {
        const EngineMidiNote* note = &s->notes[i]; cJSON* object = cJSON_CreateObject();
        id(object, "start_frame", note->start_frame); id(object, "duration_frames", note->duration_frames);
        cJSON_AddNumberToObject(object, "pitch", note->note); cJSON_AddNumberToObject(object, "velocity", note->velocity);
        cJSON_AddItemToArray(notes, object);
    }
    cJSON_AddNumberToObject(result, "next_offset", end < s->note_count ? end : -1);
    cJSON* tempo = cJSON_AddArrayToObject(result, "tempo_events");
    end = tempo_offset + DAW_CONTROL_MIDI_TEMPO_PAGE;
    if (end > s->tempo.event_count) end = s->tempo.event_count;
    for (int i = tempo_offset; i < end; ++i) {
        cJSON* object = cJSON_CreateObject(); cJSON_AddNumberToObject(object, "beat", s->tempo.events[i].beat);
        cJSON_AddNumberToObject(object, "bpm", s->tempo.events[i].bpm); cJSON_AddItemToArray(tempo, object);
    }
    cJSON_AddNumberToObject(result, "next_tempo_offset", end < s->tempo.event_count ? end : -1);
}
// Maps application refusal codes independently from transport success.
static cJSON* action_result(DawMidiResult action) {
    static const char* const codes[] = {"applied", "unchanged", "invalid_request", "wrong_thread", "stale_target",
        "state_conflict", "busy", "history_unavailable", "engine_rejected"};
    cJSON* result = outcome(action.status <= DAW_MIDI_UNCHANGED, codes[action.status]);
    cJSON_AddStringToObject(result, "message", action.message); cJSON_AddNumberToObject(result, "note_count", action.note_count);
    return result;
}
// Adapts bounded MIDI reads and guarded writes exclusively through common actions/history.
cJSON* daw_control_midi(DawControlBridge* bridge, AppState* state, const cJSON* request, DawActionTarget target, bool* changed) {
    const char* op = text(request, "op");
    bool read = !strcmp(op, "get_midi_region");
    int offset = 0, tempo_offset = 0; double value;
    if (read) {
        if (cJSON_GetObjectItemCaseSensitive(request, "offset")) {
            if (!number(request, "offset", 0, ENGINE_MIDI_NOTE_CAP, true, &value)) return outcome(false, "invalid_offset");
            offset = (int)value;
        }
        if (cJSON_GetObjectItemCaseSensitive(request, "tempo_offset")) {
            if (!number(request, "tempo_offset", 0, DAW_MIDI_TEMPO_EVENT_CAP, true, &value)) return outcome(false, "invalid_offset");
            tempo_offset = (int)value;
        }
    }
    DawMidiSnapshot current = {0};
    DawMidiResult got = daw_action_get_midi(state, target, &current);
    if (got.status != DAW_MIDI_UNCHANGED) return action_result(got);
    const char* expected = text(request, "expected_state");
    DawControlMidiState* entry = find(bridge, expected);
    cJSON* result = NULL;
    if ((!read || cJSON_GetObjectItemCaseSensitive(request, "expected_state")) && (!entry || !same(&entry->snapshot, &current))) {
        result = outcome(false, "state_conflict"); goto done;
    }
    if (read) {
        if (offset > current.note_count || tempo_offset > current.tempo.event_count) { result = outcome(false, "invalid_offset"); goto done; }
        if (!entry) entry = remember(bridge, &current);
        if (!entry) { result = outcome(false, "allocation_failed"); goto done; }
        result = outcome(true, "midi_region"); page(result, entry, offset, tempo_offset); goto done;
    }
    if (!strcmp(op, "extend_midi_region")) {
        uint64_t duration;
        if (!integer_string(request, "duration_frames", &duration)) { result = outcome(false, "invalid_request"); goto done; }
        DawMidiResult action = daw_action_extend_midi_region(state, &entry->snapshot, duration);
        *changed = action.status == DAW_MIDI_APPLIED; result = action_result(action);
    } else if (!strcmp(op,"transform_midi")) {
        const char* kind=text(request,"operation");DawMidiExpression expression={0};
        if(!kind){result=outcome(false,"invalid_request");goto done;}
        if(!strcmp(kind,"quantize")){
            expression.kind=DAW_MIDI_QUANTIZE;
            if(!number(request,"grid_beats",.03125,4,false,&expression.grid_beats) || !number(request,"strength",0,1,false,&expression.strength) || !number(request,"swing",0,.75,false,&expression.swing)){result=outcome(false,"invalid_request");goto done;}
        }else if(!strcmp(kind,"velocity")){
            expression.kind=DAW_MIDI_VELOCITY_SCALE;
            if(!number(request,"velocity_scale",0,4,false,&expression.velocity_scale)){result=outcome(false,"invalid_request");goto done;}
        }else if(!strcmp(kind,"humanize")){
            expression.kind=DAW_MIDI_HUMANIZE;
            if(!number(request,"timing_ms",0,100,false,&expression.timing_ms) || !number(request,"velocity_amount",0,.5,false,&expression.velocity_amount) || !number(request,"seed",0,UINT32_MAX,true,&value)){result=outcome(false,"invalid_request");goto done;}
            expression.seed=(uint32_t)value;
        }else{result=outcome(false,"invalid_request");goto done;}
        const char* option_names[]={"grid_beats","strength","swing","velocity_scale","timing_ms","velocity_amount","seed"};
        for(int i=0;i<7;++i){bool relevant=expression.kind==DAW_MIDI_QUANTIZE?i<3:expression.kind==DAW_MIDI_VELOCITY_SCALE?i==3:i>=4;
            if(!relevant && cJSON_GetObjectItemCaseSensitive(request,option_names[i])){result=outcome(false,"invalid_request");goto done;}}
        DawMidiResult action=daw_action_transform_midi(state,&entry->snapshot,expression);*changed=action.status==DAW_MIDI_APPLIED;result=action_result(action);
    } else if (!strcmp(op, "undo_midi") || !strcmp(op, "redo_midi")) {
        bool redo = !strcmp(op, "redo_midi");
        int count = redo ? state->undo.redo_count : state->undo.undo_count;
        UndoCommand* commands = redo ? state->undo.redo_stack : state->undo.undo_stack;
        if (!count || commands[count-1].type != UNDO_CMD_MIDI_NOTE_EDIT) { result = outcome(false, "history_conflict"); goto done; }
        DawActionTarget last = commands[count-1].data.midi_note_edit.target;
        if (last.project_generation != target.project_generation || last.track_id != target.track_id || last.clip_id != target.clip_id || last.kind != target.kind) {
            result = outcome(false, "history_conflict"); goto done;
        }
        if (state->undo.active_drag_valid || state->bounce_active || daw_audio_recording_is_active(&state->audio_recording) ||
            engine_transport_requested_playing(state->engine) || state->midi_editor_ui.qwerty_test_enabled || state->midi_editor_ui.qwerty_record_armed ||
            state->midi_editor_ui.drag_active || state->midi_editor_ui.marquee_active || state->midi_editor_ui.note_press_pending || state->midi_editor_ui.shift_note_pending) {
            result = outcome(false, "busy"); goto done;
        }
        bool accepted = redo ? undo_manager_redo(&state->undo, state) : undo_manager_undo(&state->undo, state);
        *changed = accepted; result = outcome(accepted, accepted ? "applied" : "engine_rejected");
    } else {
        const char* mode = text(request, "mode");
        if (!mode || (strcmp(mode, "append") && strcmp(mode, "replace"))) { result = outcome(false, "invalid_request"); goto done; }
        EngineMidiNote notes[DAW_CONTROL_MIDI_WRITE_NOTES]; int count = 0;
        if (!strcmp(op, "create_midi_pattern")) {
            const char* kind = text(request, "pattern"); DawMidiPattern pattern = {0};
            if (!kind || !number(request, "root_note", 0, 127, true, &value)) { result = outcome(false, "invalid_request"); goto done; }
            pattern.root_note = (int)value;
            if (!strcmp(kind, "melody")) pattern.kind = DAW_MIDI_MELODY;
            else if (!strcmp(kind, "chords")) pattern.kind = DAW_MIDI_CHORDS;
            else if (!strcmp(kind, "arpeggio")) pattern.kind = DAW_MIDI_ARPEGGIO;
            else { result = outcome(false, "invalid_request"); goto done; }
            if (!number(request, "start_beat", 0, 1000000, false, &pattern.start_beat) ||
                !number(request, "velocity", 0, 1, false, &value) || value <= 0) { result = outcome(false, "invalid_request"); goto done; }
            pattern.velocity = (float)value;
            DawMidiPatternNotes generated;
            DawMidiResult built = daw_midi_pattern_build(&current, pattern, &generated);
            if (built.status != DAW_MIDI_UNCHANGED) { result = action_result(built); goto done; }
            count = generated.count; memcpy(notes, generated.notes, (size_t)count * sizeof(*notes));
        } else {
            const cJSON* events = cJSON_GetObjectItemCaseSensitive(request, "notes");
            if (!cJSON_IsArray(events) || (count = cJSON_GetArraySize(events)) > DAW_CONTROL_MIDI_WRITE_NOTES) { result = outcome(false, "invalid_request"); goto done; }
            static const char* const allowed[] = {"start_frame", "duration_frames", "pitch", "velocity", NULL};
            int i = 0;
            for (const cJSON* event = events->child; event; event = event->next, ++i) {
                if (!fields(event, allowed) || !integer_string(event, "start_frame", &notes[i].start_frame) ||
                    !integer_string(event, "duration_frames", &notes[i].duration_frames) || !number(event, "pitch", 0, 127, true, &value)) {
                    result = outcome(false, "invalid_request"); goto done;
                }
                notes[i].note = (uint8_t)value;
                if (!number(event, "velocity", 0, 1, false, &value)) { result = outcome(false, "invalid_request"); goto done; }
                notes[i].velocity = (float)value;
            }
        }
        DawMidiRequest edit = {&entry->snapshot, notes, count, !strcmp(mode, "append") ? DAW_MIDI_APPEND : DAW_MIDI_REPLACE};
        DawMidiResult action = daw_action_edit_midi(state, &edit); *changed = action.status == DAW_MIDI_APPLIED; result = action_result(action);
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "ok"))) {
        daw_midi_snapshot_free(&current);
        got = daw_action_get_midi(state, target, &current);
        entry = got.status == DAW_MIDI_UNCHANGED ? remember(bridge, &current) : NULL;
        if (entry) page(result, entry, 0, 0);
        else cJSON_AddBoolToObject(result, "readback_required", true);
    }
done:
    daw_midi_snapshot_free(&current); return result;
}
