#include "control_bridge_internal.h"
#include "app_state.h"
#include "engine/instrument_samples.h"
#include "daw/app_actions.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Produces a machine-readable rejection without claiming an edit was applied.
static cJSON* reply(bool ok, const char* code) {
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", ok);
    cJSON_AddStringToObject(result, "code", code);
    return result;
}

// Retrieves a case-sensitive string property without coercion.
static const char* string_at(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}

// Rejects unknown and duplicate properties at each typed object boundary.
static bool fields_valid(const cJSON* object, const char* const* allowed) {
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON* item = object->child; item; item = item->next) {
        bool found = false;
        for (int i = 0; allowed[i]; ++i) if (strcmp(item->string, allowed[i]) == 0) found = true;
        if (!found) return false;
        for (const cJSON* other = item->next; other; other = other->next)
            if (strcmp(item->string, other->string) == 0) return false;
    }
    return true;
}

// Encodes identities as decimal strings to preserve all 64 bits in MCP clients.
static void add_id(cJSON* object, const char* key, uint64_t value) {
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu64, value);
    cJSON_AddStringToObject(object, key, text);
}

// Parses a canonical unsigned decimal identity with explicit overflow checking.
static bool parse_id(const cJSON* object, const char* key, uint64_t* value) {
    const char* text = string_at(object, key);
    if (!text || !text[0] || strlen(text) > 20 || (text[0] == '0' && text[1])) return false;
    for (const char* p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0;
    char* end;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || *end) return false;
    *value = (uint64_t)parsed;
    return true;
}

// Copies a generation-bound target from a strict wire object.
bool daw_control_parse_target(const cJSON* object, DawActionTarget* target) {
    static const char* const fields[] = {"generation", "track_id", "clip_id", "kind", NULL};
    const char* kind = string_at(object, "kind");
    if (!fields_valid(object, fields) || !kind) return false;
    if (strcmp(kind, "track") == 0) target->kind = DAW_ACTION_TARGET_TRACK;
    else if (strcmp(kind, "midi_region") == 0) target->kind = DAW_ACTION_TARGET_MIDI_REGION;
    else return false;
    return parse_id(object, "generation", &target->project_generation) &&
        parse_id(object, "track_id", &target->track_id) && parse_id(object, "clip_id", &target->clip_id);
}

// Serializes an opaque target without publishing mutable array positions as handles.
static cJSON* target_json(DawActionTarget target) {
    cJSON* object = cJSON_CreateObject();
    add_id(object, "generation", target.project_generation);
    add_id(object, "track_id", target.track_id);
    add_id(object, "clip_id", target.clip_id);
    cJSON_AddStringToObject(object, "kind", target.kind == DAW_ACTION_TARGET_TRACK ? "track" : "midi_region");
    return object;
}

// Copies all instrument parameters in stable specification order.
static cJSON* params_json(EngineInstrumentParams params) {
    cJSON* object = cJSON_CreateObject();
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec;
        engine_instrument_param_spec((EngineInstrumentParamId)i, &spec);
        cJSON_AddNumberToObject(object, spec.id, engine_instrument_params_get(params, (EngineInstrumentParamId)i));
    }
    return object;
}

// Copies stored and effective settings for readback and an exact state precondition.
static cJSON* snapshot_json(DawControlBridge* bridge, DawInstrumentSnapshot snapshot) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "endpoint_instance", bridge->instance);
    cJSON_AddItemToObject(object, "target", target_json(snapshot.target));
    cJSON_AddStringToObject(object, "preset", engine_instrument_preset_id_string(snapshot.stored.preset));
    cJSON_AddItemToObject(object, "parameters", params_json(snapshot.stored.params));
    cJSON_AddBoolToObject(object, "enabled", snapshot.stored.enabled);
    cJSON_AddBoolToObject(object, "inherits_track", snapshot.stored.inherits_track);
    cJSON_AddStringToObject(object, "effective_preset", engine_instrument_preset_id_string(snapshot.effective_preset));
    cJSON_AddItemToObject(object, "effective_parameters", params_json(snapshot.effective_params));
    return object;
}

// Adds copied state and a canonical state token; this token is not a project-wide revision.
static void add_snapshot(DawControlBridge* bridge, cJSON* result, DawInstrumentSnapshot snapshot) {
    cJSON* object = snapshot_json(bridge, snapshot);
    char* version = cJSON_PrintUnformatted(object);
    if (version) cJSON_AddStringToObject(result, "expected_state", version);
    cJSON_AddItemToObject(result, "instrument", object);
    free(version);
}

// Maps the common application result to a stable wire outcome.
cJSON* daw_control_action_json(DawControlBridge* bridge, DawActionResult action) {
    static const char* const codes[] = {"applied", "unchanged", "invalid_request", "wrong_thread", "stale_target",
        "busy", "history_unavailable", "engine_rejected"};
    cJSON* result = reply(action.status <= DAW_ACTION_UNCHANGED, codes[action.status]);
    cJSON_AddStringToObject(result, "message", action.message);
    if (action.has_snapshot) add_snapshot(bridge, result, action.snapshot);
    return result;
}

// Advertises actual presets, ranges and preset-specific visible controls from the engine registry.
static cJSON* capabilities(DawControlBridge* bridge) {
    cJSON* result = reply(true, "capabilities");
    cJSON_AddBoolToObject(result, "writes_enabled", bridge->allow_write);
    cJSON_AddStringToObject(result, "precondition_scope", "exact target instrument state including effective inheritance; not project revision");
    cJSON_AddNumberToObject(result, "receipt_capacity", DAW_CONTROL_RECEIPTS);
    cJSON_AddNumberToObject(result, "receipts_used", bridge->receipt_count);
    cJSON* dynamics=cJSON_AddObjectToObject(result,"dynamics_feedback");cJSON_AddStringToObject(dynamics,"read","get_effect_chain.dynamics_feedback");cJSON_AddStringToObject(dynamics,"controls","edit_effect_chain with expected_state; native insert controls share history");cJSON_AddNumberToObject(dynamics,"stale_after_ms",1000);cJSON_AddStringToObject(dynamics,"qualified","Compressor independent peak/RMS detectors; linked sample-peak Limiter, not true peak");
    cJSON* lm=cJSON_AddObjectToObject(result,"live_meters");cJSON_AddNumberToObject(lm,"window_ms",400);cJSON_AddNumberToObject(lm,"stale_after_ms",1000);cJSON_AddBoolToObject(lm,"qualified_live_lufs",false);cJSON_AddStringToObject(lm,"monitor","ephemeral output-only stereo/mono; 5 ms callback transition; production meters before fold, captured previews excluded");
    cJSON* ex=cJSON_AddObjectToObject(result,"song_export");cJSON_AddNumberToObject(ex,"material_seconds",3600);cJSON_AddNumberToObject(ex,"tail_seconds",60);cJSON_AddNumberToObject(ex,"active_jobs",1);cJSON_AddNumberToObject(ex,"retained_jobs",8);cJSON_AddStringToObject(ex,"formats","pcm16, float32 WAV at captured project rate/stereo; no resampling, no automatic insertion");
    cJSON* ab=cJSON_AddObjectToObject(result,"ab_comparison");cJSON_AddNumberToObject(ab,"material_seconds",10);cJSON_AddNumberToObject(ab,"tail_seconds",5);cJSON_AddNumberToObject(ab,"preceding_context_seconds",60);cJSON_AddNumberToObject(ab,"pair_bytes",67108864);cJSON_AddStringToObject(ab,"matching","attenuation-only all-channel RMS; listening-only original/matched modes; immutable captured pairs, 5 ms shared-cursor crossfade");
    cJSON* eq=cJSON_AddObjectToObject(result,"dedicated_eq");
    cJSON_AddNumberToObject(eq,"state_capacity",DAW_CONTROL_EQ_STATES);cJSON_AddNumberToObject(eq,"bands",4);
    cJSON_AddStringToObject(eq,"units","frequency Hz 20..20000; enabled frequency also <=0.45*sample rate; gain dB -20..20; bandwidth octaves 0.1..4; fixed 12 dB/oct cuts");
    cJSON_AddStringToObject(eq,"scope","master or stable track/child/group/return; complete stored curve including disabled controls; separate from inserts; stopped exact guarded writes/history");
    cJSON* controls=cJSON_AddObjectToObject(result,"track_controls");
    cJSON_AddNumberToObject(controls,"state_capacity",DAW_CONTROL_TRACK_STATES);
    cJSON_AddNumberToObject(controls,"name_max_utf8_bytes",ENGINE_CLIP_NAME_MAX-1);
    cJSON_AddStringToObject(controls,"gain_unit","linear amplitude; 0 silence, 1 unity, maximum 10 (+20 dB)");
    cJSON_AddNumberToObject(controls,"gain_min",0);cJSON_AddNumberToObject(controls,"gain_max",10);
    cJSON_AddStringToObject(controls,"pan_unit","stereo balance; -1 left, 0 center, +1 right");
    cJSON_AddNumberToObject(controls,"pan_min",-1);cJSON_AddNumberToObject(controls,"pan_max",1);
    cJSON_AddStringToObject(controls,"scope","track/child/group/return base name, gain or pan; one field/undo step; token checks addressed field and lifetime only; mute/solo read-only; playback allowed, recording/competing edits refused");
    cJSON* routing = cJSON_AddObjectToObject(result, "routing");
    cJSON_AddNumberToObject(routing, "state_capacity", DAW_CONTROL_ROUTING_STATES);
    cJSON_AddNumberToObject(routing, "return_capacity", ENGINE_RETURN_CAP);
    cJSON_AddNumberToObject(routing, "send_capacity", ENGINE_SEND_CAP);
    cJSON_AddNumberToObject(routing, "track_capacity", ENGINE_ROUTING_TRACK_CAP);
    cJSON_AddNumberToObject(routing, "batch_capacity", 16);
    cJSON_AddNumberToObject(routing,"automation_points_per_lane",ENGINE_MIX_AUTOMATION_POINTS);cJSON_AddNumberToObject(routing,"automation_points_total",ENGINE_MIX_AUTOMATION_TOTAL);cJSON_AddStringToObject(routing,"automation_units","absolute project frames; group linear gain0..10; bound send linear gain0..1; curves0linear,1hold,2smooth; boundaries0static baseline,1hold");
    cJSON_AddStringToObject(routing, "scope", "post-fader source-to-return only; linear sends 0..1; no feedback; stopped guarded writes; creation supports an empty project");
    cJSON* arrangement = cJSON_AddObjectToObject(result, "arrangement");
    cJSON_AddNumberToObject(arrangement, "state_capacity", DAW_CONTROL_ARRANGEMENT_STATES);
    cJSON_AddNumberToObject(arrangement, "batch_capacity", DAW_ARRANGEMENT_BATCH_CAP);
    cJSON_AddNumberToObject(arrangement, "clip_capacity", DAW_ARRANGEMENT_CLIP_CAP);
    cJSON_AddNumberToObject(arrangement, "note_capacity", DAW_ARRANGEMENT_NOTE_CAP);
    cJSON_AddNumberToObject(arrangement, "automation_point_capacity", DAW_ARRANGEMENT_POINT_CAP);
    cJSON_AddStringToObject(arrangement, "scope", "exact full layout/tempo and authored clips/track sound; existing MIDI regions; right-edge trim refuses discarded notes; serial track effects remain destination-owned");
    cJSON* automation = cJSON_AddObjectToObject(result, "automation");
    cJSON_AddStringToObject(automation, "scope", "audio and MIDI regions; local sample frames; volume dB and stereo balance or legacy offsets");
    cJSON_AddNumberToObject(automation, "point_page", 128);
    cJSON_AddNumberToObject(automation, "write_point_limit", 128);
    cJSON_AddBoolToObject(automation, "shared_history", true);

    cJSON* mix=cJSON_AddObjectToObject(result,"mix_diagnostics");
    cJSON_AddStringToObject(mix,"schema","sonics_mix_capture_v3");
    cJSON_AddNumberToObject(mix,"track_capacity",DAW_MIX_TRACKS);cJSON_AddNumberToObject(mix,"source_capacity",DAW_ARRANGEMENT_CLIP_CAP);cJSON_AddNumberToObject(mix,"channel_capacity",DAW_MIX_CHANNELS);cJSON_AddNumberToObject(mix,"time_bins",DAW_MIX_BINS);cJSON_AddNumberToObject(mix,"retained_capture_capacity",DAW_CONTROL_MIX_STATES);cJSON_AddNumberToObject(mix,"range_seconds",DAW_MIX_RANGE_SECONDS);cJSON_AddNumberToObject(mix,"full_context_seconds",DAW_MIX_CONTEXT_SECONDS);
    cJSON_AddBoolToObject(mix,"whole_song_lufs_lra",true);cJSON_AddNumberToObject(mix,"song_seconds",DAW_LOUDNESS_PROGRAM_SECONDS);cJSON_AddNumberToObject(mix,"song_tail_seconds",DAW_LOUDNESS_TAIL_SECONDS);
    cJSON_AddBoolToObject(mix,"passage_lufs_true_peak",true);cJSON_AddNumberToObject(mix,"loudness_max_history_points",DAW_LOUDNESS_POINTS);
    cJSON_AddNumberToObject(mix,"frequency_windows",DAW_FREQUENCY_WINDOWS);cJSON_AddNumberToObject(mix,"frequency_fft_size",DAW_FREQUENCY_FFT);cJSON_AddNumberToObject(mix,"frequency_channels_per_page",1);
    cJSON_AddNumberToObject(mix,"note_page",8);cJSON_AddNumberToObject(mix,"activity_threshold_dbfs",-80);
    cJSON_AddStringToObject(mix,"semantics","read-only synchronous private production graph; exact guard; full cold context from frame zero; per-tap latency; immutable paged channel sample-peak/RMS; immutable insert levels and native dynamics reduction; 32 per-channel output spectral windows; selected-passage M/S/I LUFS and per-channel true peak at explicitly mapped mono/stereo 44.1/48/96 kHz; not isolated stems or whole-programme loudness");
    cJSON* composition = cJSON_AddObjectToObject(result, "composition");
    cJSON_AddNumberToObject(composition, "render_capacity", DAW_CONTROL_COMPOSITION_STATES);
    cJSON_AddNumberToObject(composition, "retained_audio_bytes", DAW_CONTROL_COMPOSITION_BYTES);
    cJSON_AddNumberToObject(composition, "current_audio_bytes", bridge->composition_bytes);
    int retained_renders = 0;
    for (int i = 0; i < DAW_CONTROL_COMPOSITION_STATES; ++i) if (bridge->composition_states[i].render) ++retained_renders;
    cJSON_AddNumberToObject(composition, "current_render_count", retained_renders);
    cJSON_AddNumberToObject(composition,"channel_meter_capacity",DAW_COMPOSITION_CHANNELS);
    cJSON_AddStringToObject(composition,"metering_scope","immutable captured samples; per-channel sample peak/RMS, dBFS (null for silence), abs>=1 full-scale and abs>1 overload counts; not live, LUFS or true-peak");
    cJSON_AddNumberToObject(composition, "range_seconds", DAW_COMPOSITION_SECONDS);
    cJSON_AddNumberToObject(composition, "tail_seconds", DAW_COMPOSITION_TAIL_SECONDS);
    cJSON_AddStringToObject(composition, "scope", "immutable original-level production project/track/region audio; dry excludes FX/EQ; preview publishes captured audio through receipts");
    cJSON* effects = cJSON_AddObjectToObject(result, "effects");
    cJSON_AddNumberToObject(effects, "chain_limit", FX_MASTER_MAX);
    cJSON_AddNumberToObject(effects, "state_token_capacity", DAW_CONTROL_FX_STATES);
    cJSON_AddNumberToObject(effects, "catalog_page_size", 8);
    cJSON_AddNumberToObject(effects,"library_capacity",128);
    cJSON_AddNumberToObject(effects,"library_page_size",8);
    cJSON_AddNumberToObject(effects,"effects_version",1);
    cJSON_AddStringToObject(effects, "scope", "track or master serial chain; add/remove/reorder/bypass/physical or beat-synced parameter; exact guarded history");
    cJSON* midi = cJSON_AddObjectToObject(result, "midi");
    cJSON_AddNumberToObject(midi, "layout_state_capacity", DAW_CONTROL_LAYOUT_STATES);
    cJSON_AddNumberToObject(midi, "layout_track_limit", DAW_MIDI_LAYOUT_TRACK_CAP);
    cJSON_AddNumberToObject(midi, "layout_clip_limit", DAW_MIDI_LAYOUT_CLIP_CAP);
    cJSON_AddStringToObject(midi, "region_scope", "create empty MIDI region on existing or one new trailing track; nonoverlap; extension changes right edge only; structural history exact identity");
    cJSON_AddNumberToObject(midi, "note_page_size", DAW_CONTROL_MIDI_NOTE_PAGE);
    cJSON_AddNumberToObject(midi, "tempo_page_size", DAW_CONTROL_MIDI_TEMPO_PAGE);
    cJSON_AddNumberToObject(midi, "write_note_limit", DAW_CONTROL_MIDI_WRITE_NOTES);
    cJSON_AddNumberToObject(midi, "region_note_limit", ENGINE_MIDI_NOTE_CAP);
    cJSON_AddNumberToObject(midi, "state_token_capacity", DAW_CONTROL_MIDI_STATES);
    cJSON_AddStringToObject(midi, "state_scope", "exact region notes, bounds, source offset and tempo map; eight retained states per endpoint; refresh expired tokens");
    cJSON_AddStringToObject(midi,"expression_scope","whole untrimmed region; nearest swung grid with strength, clamped velocity scaling, deterministic seeded timing/velocity variation; fixed durations; boundary/collision refusal; one undo step");
    cJSON_AddNumberToObject(midi,"expression_note_limit",ENGINE_MIDI_NOTE_CAP);
    cJSON_AddStringToObject(midi, "pattern_scope", "four quarter-note beats, major tonic, untrimmed fixed-length region, stopped transport");
    cJSON* patterns = cJSON_AddArrayToObject(midi, "patterns");
    cJSON_AddItemToArray(patterns, cJSON_CreateString("melody"));
    cJSON_AddItemToArray(patterns, cJSON_CreateString("chords"));
    cJSON_AddItemToArray(patterns, cJSON_CreateString("arpeggio"));
    cJSON* presets = cJSON_AddArrayToObject(result, "presets");
    for (int i = 0; i < engine_instrument_preset_count(); ++i) {
        EngineInstrumentPresetId preset = (EngineInstrumentPresetId)i;
        cJSON* object = cJSON_CreateObject();
        cJSON_AddStringToObject(object, "id", engine_instrument_preset_id_string(preset));
        cJSON_AddStringToObject(object, "name", engine_instrument_preset_display_name(preset));
        cJSON_AddItemToObject(object, "defaults", params_json(engine_instrument_default_params(preset)));
        EngineSampleBankInfo bank;
        if (engine_instrument_sample_bank_info(preset,&bank)) {
            cJSON_AddStringToObject(object,"sample_bank",bank.id);
            cJSON_AddNumberToObject(object,"minimum_note",bank.minimum_note);
            cJSON_AddNumberToObject(object,"maximum_note",bank.maximum_note);
            cJSON_AddNumberToObject(object,"recorded_pitches",bank.recorded_pitches);
            cJSON_AddNumberToObject(object,"dynamic_layers",bank.layers);
            if (preset == ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO) {
                cJSON_AddStringToObject(object,"sustain_mode","binary pedal; region automation and live semicolon audition");
                cJSON_AddStringToObject(object,"resonance_model","88 damped string modes driven by recorded strikes");
                cJSON_AddNumberToObject(object,"key_release_recordings",88);
                cJSON_AddNumberToObject(object,"pedal_recordings",4);
            }
            cJSON_AddStringToObject(object,"articulation",bank.articulation);
        }
        cJSON* visible = cJSON_AddArrayToObject(object, "visible_parameters");
        for (int j = 0; j < engine_instrument_preset_param_count(preset); ++j) {
            EngineInstrumentParamId id;
            EngineInstrumentParamSpec spec;
            engine_instrument_preset_param_id_at(preset, j, &id);
            engine_instrument_param_spec(id, &spec);
            cJSON_AddItemToArray(visible, cJSON_CreateString(spec.id));
        }
        cJSON_AddItemToArray(presets, object);
    }
    cJSON* parameters = cJSON_AddArrayToObject(result, "parameters");
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec;
        engine_instrument_param_spec((EngineInstrumentParamId)i, &spec);
        cJSON* object = cJSON_CreateObject();
        cJSON_AddStringToObject(object, "id", spec.id);
        cJSON_AddStringToObject(object, "name", spec.display_name);
        cJSON_AddStringToObject(object, "unit", spec.unit ? spec.unit : "");
        cJSON_AddNumberToObject(object, "minimum", spec.min_value);
        cJSON_AddNumberToObject(object, "maximum", spec.max_value);
        cJSON_AddStringToObject(object, "control_scale", spec.scale == ENGINE_INSTRUMENT_PARAM_SCALE_LOG_TIME ? "log1p_ms" :
            spec.scale == ENGINE_INSTRUMENT_PARAM_SCALE_LOG_FREQUENCY ? "log_hz" : "linear");
        cJSON_AddItemToArray(parameters, object);
    }
    return result;
}

// Lists at most 64 instrument targets per page with copied names and generation-bound handles.
static cJSON* project_json(AppState* state, const cJSON* request) {
    const cJSON* page = cJSON_GetObjectItemCaseSensitive(request, "offset");
    if (page && (!cJSON_IsNumber(page) || !isfinite(page->valuedouble) || page->valuedouble < 0 ||
        page->valuedouble > 1000000 || floor(page->valuedouble) != page->valuedouble)) return reply(false, "invalid_offset");
    int offset = page ? page->valueint : 0;
    cJSON* result = reply(true, "project");
    add_id(result, "generation", engine_instance_id(state->engine));
    cJSON* targets = cJSON_AddArrayToObject(result, "targets");
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int count = 0, emitted = 0;
    for (int t = 0; t < engine_get_track_count(state->engine); ++t) {
        for (int c = -1; c < tracks[t].clip_count; ++c) {
            if (c >= 0 && tracks[t].clips[c].kind != ENGINE_CLIP_KIND_MIDI) continue;
            int index = count++;
            if (index < offset || emitted >= 64) continue;
            DawActionTarget target;
            if (daw_instrument_target_capture(state, t, c, &target) != DAW_ACTION_UNCHANGED) continue;
            cJSON* object = target_json(target);
            cJSON_AddStringToObject(object, "name", c < 0 ? tracks[t].name : tracks[t].clips[c].name);
            cJSON_AddItemToArray(targets, object);
            ++emitted;
        }
    }
    cJSON_AddNumberToObject(result, "total", count);
    cJSON_AddNumberToObject(result, "next_offset", offset + emitted < count ? offset + emitted : -1);
    return result;
}

// Validates raw floating values before the engine's sanitizing helper can clamp them.
static bool parse_patch(const cJSON* request, DawInstrumentRequest* patch) {
    const cJSON* preset = cJSON_GetObjectItemCaseSensitive(request, "preset");
    if (preset && !cJSON_IsNull(preset)) {
        if (!cJSON_IsString(preset) || !engine_instrument_preset_from_id_string(preset->valuestring, &patch->preset)) return false;
        patch->set_preset = true;
    }
    const cJSON* params = cJSON_GetObjectItemCaseSensitive(request, "parameters");
    if (!params || !cJSON_IsObject(params)) return false;
    for (const cJSON* item = params->child; item; item = item->next) {
        EngineInstrumentParamId id;
        EngineInstrumentParamSpec spec;
        if (!cJSON_IsNumber(item) || !engine_instrument_param_from_id_string(item->string, &id)) return false;
        engine_instrument_param_spec(id, &spec);
        if (!isfinite(item->valuedouble) || item->valuedouble < spec.min_value || item->valuedouble > spec.max_value ||
            (patch->parameter_mask & (UINT64_C(1) << id))) return false;
        patch->parameter_mask |= UINT64_C(1) << id;
        patch->params = engine_instrument_params_set(ENGINE_INSTRUMENT_PRESET_SYNTH_LAB, patch->params, id, (float)item->valuedouble);
    }
    return true;
}

// Applies a guarded instrument patch through the existing reserved application action.
static cJSON* set_instrument(DawControlBridge* bridge, AppState* state, const cJSON* request, bool* changed) {
    DawInstrumentRequest patch = {0};
    if (!daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &patch.target) || !parse_patch(request, &patch))
        return reply(false, "invalid_request");
    DawActionResult before = daw_action_get_instrument(state, patch.target);
    if (!before.has_snapshot) return daw_control_action_json(bridge, before);
    const char* expected = string_at(request, "expected_state");
    cJSON* object = snapshot_json(bridge, before.snapshot);
    char* current = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    bool match = expected && current && strcmp(expected, current) == 0;
    free(current);
    if (!match) return reply(false, "state_conflict");
    if (state->bounce_active || daw_audio_recording_is_active(&state->audio_recording)) return reply(false, "busy");
    DawActionResult result = daw_action_set_instrument(state, &patch);
    *changed = result.status == DAW_ACTION_APPLIED;
    return daw_control_action_json(bridge, result);
}

// Reverses only a matching instrument command at the history cursor after checking current state.
static cJSON* instrument_history(DawControlBridge* bridge, AppState* state, const cJSON* request, bool redo, bool* changed) {
    DawActionTarget target = {0};
    if (!daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &target)) return reply(false, "invalid_target");
    DawActionResult before = daw_action_get_instrument(state, target);
    if (!before.has_snapshot) return daw_control_action_json(bridge, before);
    cJSON* object = snapshot_json(bridge, before.snapshot);
    char* current = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    const char* expected = string_at(request, "expected_state");
    bool match = current && expected && strcmp(current, expected) == 0;
    free(current);
    if (!match) return reply(false, "state_conflict");
    if (state->undo.active_drag_valid || state->bounce_active || daw_audio_recording_is_active(&state->audio_recording))
        return reply(false, "busy");
    int count = redo ? state->undo.redo_count : state->undo.undo_count;
    UndoCommand* commands = redo ? state->undo.redo_stack : state->undo.undo_stack;
    if (!count || commands[count - 1].type != UNDO_CMD_INSTRUMENT_SETTINGS) return reply(false, "history_conflict");
    DawActionTarget last = commands[count - 1].data.instrument_settings.target;
    if (last.project_generation != target.project_generation || last.track_id != target.track_id ||
        last.clip_id != target.clip_id || last.kind != target.kind) return reply(false, "history_conflict");
    bool accepted = redo ? undo_manager_redo(&state->undo, state) : undo_manager_undo(&state->undo, state);
    if (!accepted) return reply(false, "engine_rejected");
    *changed = true;
    DawActionResult after = daw_action_get_instrument(state, target);
    after.status = DAW_ACTION_APPLIED;
    return daw_control_action_json(bridge, after);
}

// Routes strict requests and reserves immutable receipt storage before any mutation.
char* daw_control_dispatch(DawControlBridge* bridge, AppState* state, const char* raw, bool* changed) {
    *changed = false;
    static const char* const fields[] = {"op", "offset", "target", "instance", "request_id", "expected_state", "preset", "parameters", "search", "name", "preset_id", "preset_revision", "mode", "window", "tempo_offset", "notes", "pattern", "root_note", "start_beat", "velocity", "expected_layout", "start_frame", "duration_frames", "clip_offset", "action", "type_id", "effect_id", "parameter", "value", "beats", "position", "enabled", "end_frame", "tail_frames", "window_frame", "dry", "render_id", "operation", "grid_beats", "strength", "swing", "velocity_scale", "timing_ms", "velocity_amount", "seed", "expected_arrangement", "expected_routing", "edits", "lane_target", "domain", "before", "after", "points", "control", "curve", "expected_mix", "capture_id", "node_offset", "source_offset", "effect_offset", "frequency_window", "frequency_channel", "note_offset", "whole_song", "automation", "expected_comparison", "side", "matched", "restart", "clear", "job_id", "format", "normalize", "expected_monitor", NULL};
    const char* end = NULL;
    cJSON* request = cJSON_ParseWithOpts(raw, &end, true);
    cJSON* result = NULL;
    DawControlReceipt* receipt = NULL;
    if (!request || !fields_valid(request, fields)) { result = reply(false, "invalid_request"); goto done; }
    const char* op = string_at(request, "op");
    if (!op) { result = reply(false, "invalid_request"); goto done; }
    static const char* const read_fields[] = {"op", NULL};
    static const char* const project_fields[] = {"op", "offset", NULL};
    static const char* const get_fields[] = {"op", "target", NULL};
    static const char* const status_fields[] = {"op", "instance", "request_id", NULL};
    static const char* const history_fields[] = {"op", "instance", "request_id", "target", "expected_state", NULL};
    static const char* const set_fields[] = {"op", "instance", "request_id", "target", "expected_state", "preset", "parameters", NULL};
    static const char* const analysis_fields[] = {"op", "target", "mode", "window", NULL};
    static const char* const preset_analysis_fields[] = {"op", "preset_id", "preset_revision", "mode", "window", NULL};
    static const char* const list_fields[] = {"op", "search", "offset", NULL};
    static const char* const preset_get_fields[] = {"op", "preset_id", "preset_revision", NULL};
    static const char* const save_fields[] = {"op", "instance", "request_id", "target", "expected_state", "name", "preset_id", "preset_revision", NULL};
    static const char* const apply_fields[] = {"op", "instance", "request_id", "target", "expected_state", "preset_id", "preset_revision", NULL};
    static const char* const preview_fields[] = {"op", "instance", "request_id", "preset_id", "preset_revision", "mode", NULL};
    static const char* const midi_get_fields[] = {"op", "target", "offset", "tempo_offset", "expected_state", NULL};
    static const char* const midi_set_fields[] = {"op", "instance", "request_id", "target", "expected_state", "mode", "notes", NULL};
    static const char* const midi_pattern_fields[] = {"op", "instance", "request_id", "target", "expected_state", "mode", "pattern", "root_note", "start_beat", "velocity", NULL};
    static const char* const layout_fields[] = {"op", "offset", "tempo_offset", "clip_offset", "expected_layout", NULL};
    static const char* const region_create_fields[] = {"op", "instance", "request_id", "target", "expected_layout", "start_frame", "duration_frames", NULL};
    static const char* const region_extend_fields[] = {"op", "instance", "request_id", "target", "expected_state", "duration_frames", NULL};
    static const char* const region_history_fields[] = {"op", "instance", "request_id", "target", "expected_layout", NULL};
    static const char* const fx_fields[] = {"op", "instance", "request_id", "target", "expected_state", "action", "type_id", "effect_id", "parameter", "value", "mode", "beats", "position", "enabled", "automation", NULL};
    static const char* const fx_get_fields[] = {"op", "target", NULL};
    static const char* const mix_capture_fields[]={"op","expected_mix","start_frame","end_frame","whole_song","tail_frames",NULL};
    static const char* const mix_read_fields[]={"op","capture_id","node_offset","source_offset","effect_offset","frequency_window","frequency_channel","note_offset",NULL};
    static const char* const composition_fields[] = {"op", "target", "start_frame", "end_frame", "tail_frames", "window_frame", "dry", NULL};
    static const char* const composition_preview_fields[] = {"op", "instance", "request_id", "render_id", NULL};
    bool automation_write = !strcmp(op, "set_automation") || !strcmp(op, "undo_automation") || !strcmp(op, "redo_automation");
    static const char* const automation_get_fields[] = {"op", "target", "lane_target", "offset", "expected_arrangement", NULL};
    static const char* const automation_set_fields[] = {"op", "instance", "request_id", "target", "lane_target", "expected_arrangement", "domain", "before", "after", "points", NULL};
    static const char* const automation_history_fields[] = {"op", "instance", "request_id", "target", "lane_target", "expected_arrangement", NULL};
    bool eq_write=!strcmp(op,"edit_eq") || !strcmp(op,"undo_eq") || !strcmp(op,"redo_eq");
    static const char* const eq_set_fields[]={"op","instance","request_id","target","expected_state","curve",NULL};
    bool track_write=!strcmp(op,"set_track_control") || !strcmp(op,"undo_track_control") || !strcmp(op,"redo_track_control");
    static const char* const track_set_fields[]={"op","instance","request_id","target","expected_state","control","value",NULL};
    static const char* const track_history_fields[]={"op","instance","request_id","target","expected_state","control",NULL};
    bool routing_write = !strcmp(op,"create_group") || !strcmp(op,"create_return") || !strcmp(op,"edit_routing") || !strcmp(op,"undo_routing") || !strcmp(op,"redo_routing");
    static const char* const group_create_fields[]={"op","instance","request_id","expected_routing","name",NULL};
    static const char* const routing_edit_fields[]={"op","instance","request_id","expected_routing","edits",NULL};
    static const char* const routing_history_fields[]={"op","instance","request_id","expected_routing",NULL};
    bool arrangement_write = !strcmp(op,"arrange_regions") || !strcmp(op,"undo_arrangement") || !strcmp(op,"redo_arrangement");
    static const char* const arrangement_get_fields[]={"op","expected_arrangement",NULL};
    static const char* const arrangement_edit_fields[]={"op","instance","request_id","expected_arrangement","edits",NULL};
    static const char* const arrangement_history_fields[]={"op","instance","request_id","expected_arrangement",NULL};
    bool composition_write = !strcmp(op, "preview_composition");
    bool fx_library_write=!strcmp(op,"save_effect_preset") || !strcmp(op,"apply_effect_preset");
    bool fx_write = !strcmp(op, "edit_effect_chain") || !strcmp(op, "undo_effect_chain") || !strcmp(op, "redo_effect_chain");
    bool region_write = !strcmp(op, "create_midi_region") || !strcmp(op, "undo_midi_region") || !strcmp(op, "redo_midi_region");
    static const char* const expression_fields[]={"op","instance","request_id","target","expected_state","operation","grid_beats","strength","swing","velocity_scale","timing_ms","velocity_amount","seed",NULL};
    bool monitor_write=!strcmp(op,"set_monitor_mode");
    static const char* const monitor_fields[]={"op","instance","request_id","expected_monitor","mode",NULL};
    static const char* const live_fields[]={"op","target",NULL};
    bool export_write=!strcmp(op,"start_song_export") || !strcmp(op,"cancel_song_export");
    static const char* const export_start_fields[]={"op","instance","request_id","expected_mix","format","tail_frames","normalize",NULL};
    static const char* const export_cancel_fields[]={"op","instance","request_id","job_id",NULL};
    static const char* const export_read_fields[]={"op","job_id",NULL};
    bool ab_write=!strcmp(op,"capture_ab") || !strcmp(op,"listen_ab") || !strcmp(op,"stop_ab");
    static const char* const ab_capture_fields[]={"op","instance","request_id","expected_comparison","expected_mix","side","start_frame","end_frame","tail_frames","dry","target",NULL};
    static const char* const ab_listen_fields[]={"op","instance","request_id","expected_comparison","side","matched","restart",NULL};
    static const char* const ab_stop_fields[]={"op","instance","request_id","expected_comparison","clear",NULL};
    bool midi_write = !strcmp(op,"transform_midi") || !strcmp(op, "extend_midi_region") || !strcmp(op, "set_midi_notes") || !strcmp(op, "create_midi_pattern") || !strcmp(op, "undo_midi") || !strcmp(op, "redo_midi");
    const char* const* operation_fields = fields;
    if (!strcmp(op, "get_midi_layout")) operation_fields = layout_fields;
    else if (!strcmp(op, "create_midi_region")) operation_fields = region_create_fields;
    else if (!strcmp(op, "extend_midi_region")) operation_fields = region_extend_fields;
    else if (!strcmp(op, "undo_midi_region") || !strcmp(op, "redo_midi_region")) operation_fields = region_history_fields;
    else if (!strcmp(op, "get_midi_region")) operation_fields = midi_get_fields;
    else if (!strcmp(op, "set_midi_notes")) operation_fields = midi_set_fields;
    else if (!strcmp(op, "create_midi_pattern")) operation_fields = midi_pattern_fields;
    else if (!strcmp(op,"transform_midi")) operation_fields=expression_fields;
    else if (!strcmp(op, "undo_midi") || !strcmp(op, "redo_midi")) operation_fields = history_fields;
    if (strcmp(op, "capabilities") == 0) operation_fields = read_fields;
    else if (strcmp(op, "project") == 0) operation_fields = project_fields;
    else if (strcmp(op, "get_instrument") == 0) operation_fields = get_fields;
    else if (strcmp(op, "request_status") == 0) operation_fields = status_fields;
    else if (strcmp(op, "undo_instrument") == 0 || strcmp(op, "redo_instrument") == 0) operation_fields = history_fields;
    else if (strcmp(op, "set_instrument") == 0) operation_fields = set_fields;
    else if (strcmp(op, "analyze_instrument") == 0) operation_fields = analysis_fields;
    else if (strcmp(op, "analyze_preset") == 0) operation_fields = preset_analysis_fields;
    else if (strcmp(op, "list_presets") == 0) operation_fields = list_fields;
    else if (strcmp(op, "get_preset") == 0) operation_fields = preset_get_fields;
    else if (strcmp(op, "save_preset") == 0) operation_fields = save_fields;
    else if (strcmp(op, "apply_preset") == 0) operation_fields = apply_fields;
    else if (strcmp(op, "preview_preset") == 0) operation_fields = preview_fields;
    if(!strcmp(op,"list_effect_presets"))operation_fields=list_fields;
    else if(!strcmp(op,"get_effect_preset"))operation_fields=preset_get_fields;
    else if(fx_library_write)operation_fields=!strcmp(op,"save_effect_preset")?save_fields:apply_fields;
    else if (fx_write) operation_fields = !strcmp(op, "edit_effect_chain") ? fx_fields : history_fields;
    else if (!strcmp(op, "get_effect_chain")) operation_fields = fx_get_fields;
    else if (!strcmp(op, "list_effects")) operation_fields = project_fields;
    if (!strcmp(op, "analyze_composition")) operation_fields = composition_fields;
    else if (composition_write) operation_fields = composition_preview_fields;
    if (!strcmp(op,"get_arrangement")) operation_fields=arrangement_get_fields;
    else if (arrangement_write) operation_fields=!strcmp(op,"arrange_regions")?arrangement_edit_fields:arrangement_history_fields;
    if (!strcmp(op,"get_routing")) operation_fields=read_fields;
    else if (routing_write) operation_fields=!strcmp(op,"create_group") ? group_create_fields : (!strcmp(op,"edit_routing")?routing_edit_fields:routing_history_fields);
    if (!strcmp(op, "get_automation")) operation_fields = automation_get_fields;
    else if (automation_write) operation_fields = !strcmp(op, "set_automation") ? automation_set_fields : automation_history_fields;
    if(!strcmp(op,"get_eq"))operation_fields=get_fields;
    else if(eq_write)operation_fields=!strcmp(op,"edit_eq") ? eq_set_fields : history_fields;
    if (!strcmp(op,"get_track_controls")) operation_fields=get_fields;
    else if(track_write)operation_fields=!strcmp(op,"set_track_control") ? track_set_fields : track_history_fields;
    if(!strcmp(op,"get_mix_snapshot"))operation_fields=read_fields;
    else if(!strcmp(op,"capture_mix"))operation_fields=mix_capture_fields;
    else if(!strcmp(op,"get_mix_capture"))operation_fields=mix_read_fields;
    if(!strcmp(op,"get_live_meters"))operation_fields=live_fields;else if(monitor_write)operation_fields=monitor_fields;else if(!strcmp(op,"get_song_export"))operation_fields=export_read_fields;else if(export_write)operation_fields=!strcmp(op,"start_song_export")?export_start_fields:export_cancel_fields;else if(!strcmp(op,"get_ab_comparison"))operation_fields=read_fields;else if(ab_write)operation_fields=!strcmp(op,"capture_ab")?ab_capture_fields:!strcmp(op,"listen_ab")?ab_listen_fields:ab_stop_fields;
    if (!fields_valid(request, operation_fields)) { result = reply(false, "invalid_request"); goto done; }
    if (strcmp(op, "capabilities") == 0) { result = capabilities(bridge); goto done; }
    if(!strcmp(op,"get_live_meters")){result=daw_control_live_meters(bridge,state,request);goto done;}
    if(!strcmp(op,"get_song_export")){result=daw_control_song_export(bridge,state,request);goto done;}
    if(!strcmp(op,"get_ab_comparison")){result=daw_control_ab_comparison(bridge,state,request);goto done;}
    if(!strcmp(op,"get_mix_snapshot") || !strcmp(op,"capture_mix") || !strcmp(op,"get_mix_capture")){result=daw_control_mix_capture(bridge,state,request);goto done;}
    if (!strcmp(op, "get_automation")) { result = daw_control_automation(bridge, state, request, changed); goto done; }
    if(!strcmp(op,"get_eq")){result=daw_control_eq(bridge,state,request,changed);goto done;}
    if (!strcmp(op,"get_track_controls")) { result=daw_control_track_controls(bridge,state,request,raw,changed); goto done; }
    if (!strcmp(op,"get_routing")) { result=daw_control_routing(bridge,state,request,changed); goto done; }
    if (!strcmp(op,"get_arrangement")) { result=daw_control_arrangement(bridge,state,request,changed); goto done; }
    if (!strcmp(op, "get_midi_layout")) { result = daw_control_regions(bridge, state, request, NULL, changed); goto done; }
    if (strcmp(op, "project") == 0) { result = project_json(state, request); goto done; }
    if (strcmp(op, "get_instrument") == 0) {
        DawActionTarget target = {0};
        result = daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &target)
            ? daw_control_action_json(bridge, daw_action_get_instrument(state, target)) : reply(false, "invalid_target");
        goto done;
    }
    if (!strcmp(op, "get_midi_region")) {
        DawActionTarget target = {0};
        result = daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &target)
            ? daw_control_midi(bridge, state, request, target, changed) : reply(false, "invalid_target");
        goto done;
    }
    if (strcmp(op, "list_presets") == 0 || strcmp(op, "get_preset") == 0) {
        result = daw_control_presets(bridge, state, request, NULL, changed); goto done;
    }
    if (strcmp(op,"analyze_instrument") == 0) {
        DawActionTarget target;
        result = daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(request,"target"),&target) ? daw_control_analysis(bridge,state,request,&target) : reply(false,"invalid_target"); goto done;
    }
    if (strcmp(op,"analyze_preset") == 0) { result = daw_control_analysis(bridge,state,request,NULL); goto done; }
    if (!strcmp(op, "get_effect_chain") || !strcmp(op, "list_effects") || !strcmp(op,"list_effect_presets") || !strcmp(op,"get_effect_preset")) {
        DawActionTarget target; const DawActionTarget* pointer = NULL;
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(request, "target");
        if (value) { if (!daw_control_parse_target(value, &target)) { result = reply(false, "invalid_target"); goto done; } pointer = &target; }
        result = daw_control_fx(bridge, state, request, pointer, changed); goto done;
    }
    if (!strcmp(op, "analyze_composition")) {
        DawActionTarget target; const DawActionTarget* pointer = NULL;
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(request, "target");
        if (value) { if (!daw_control_parse_target(value, &target)) { result = reply(false, "invalid_target"); goto done; } pointer = &target; }
        result = daw_control_composition(bridge, state, request, pointer); goto done;
    }
    const char* instance = string_at(request, "instance");
    if (!instance || strcmp(instance, bridge->instance) != 0) { result = reply(false, "stale_instance"); goto done; }
    const char* id = string_at(request, "request_id");
    if (!id || !id[0] || strlen(id) > 64) { result = reply(false, "invalid_request_id"); goto done; }
    for (int i = 0; i < bridge->receipt_count; ++i) {
        if (strcmp(id, bridge->receipts[i].id) != 0) continue;
        if (strcmp(op, "request_status") == 0 || strcmp(raw, bridge->receipts[i].request) == 0) {
            char* replay = strdup(bridge->receipts[i].response);
            cJSON_Delete(request);
            return replay;
        }
        result = reply(false, "request_id_conflict");
        goto done;
    }
    if (strcmp(op, "request_status") == 0) { result = reply(false, "unknown_request"); goto done; }
    if (!monitor_write && !export_write && !ab_write && !eq_write && !track_write && !automation_write && !routing_write && !arrangement_write && !composition_write && !fx_library_write && !fx_write && !region_write && !midi_write && strcmp(op, "set_instrument") != 0 && strcmp(op, "undo_instrument") != 0 && strcmp(op, "redo_instrument") != 0 && strcmp(op, "save_preset") != 0 && strcmp(op, "apply_preset") != 0 && strcmp(op, "preview_preset") != 0) { result = reply(false, "unknown_operation"); goto done; }
    if (!bridge->allow_write) { result = reply(false, "read_only"); goto done; }
    if (bridge->receipt_count >= DAW_CONTROL_RECEIPTS) { result = reply(false, "receipt_capacity_reached"); goto done; }
    receipt = &bridge->receipts[bridge->receipt_count];
    receipt->request = strdup(raw);
    receipt->response = malloc(DAW_CONTROL_MAX_RESPONSE);
    if (!receipt->request || !receipt->response) {
        free(receipt->request); free(receipt->response);
        memset(receipt, 0, sizeof(*receipt));
        receipt = NULL;
        result = reply(false, "allocation_failed");
        goto done;
    }
    strcpy(receipt->id, id);
    strcpy(receipt->response, "{\"ok\":false,\"code\":\"result_unavailable\",\"may_have_applied\":true}");
    ++bridge->receipt_count;
    if(monitor_write){result=daw_control_live_meters(bridge,state,request);goto done;}
    if(export_write){result=daw_control_song_export(bridge,state,request);goto done;}
    if(ab_write){result=daw_control_ab_comparison(bridge,state,request);goto done;}
    if (composition_write) { result = daw_control_composition(bridge, state, request, NULL); goto done; }
    if (automation_write) { result = daw_control_automation(bridge, state, request, changed); goto done; }
    if(eq_write){result=daw_control_eq(bridge,state,request,changed);goto done;}
    if (track_write) { result=daw_control_track_controls(bridge,state,request,raw,changed); goto done; }
    if (routing_write) { result=daw_control_routing(bridge,state,request,changed); goto done; }
    if (arrangement_write) { result=daw_control_arrangement(bridge,state,request,changed); goto done; }
    if (fx_write || fx_library_write) {
        DawActionTarget target; const DawActionTarget* pointer = NULL;
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(request, "target");
        if (value) { if (!daw_control_parse_target(value, &target)) { result = reply(false, "invalid_target"); goto done; } pointer = &target; }
        result = daw_control_fx(bridge, state, request, pointer, changed); goto done;
    }
    if (region_write) {
        DawActionTarget target; const cJSON* value = cJSON_GetObjectItemCaseSensitive(request, "target");
        const DawActionTarget* pointer = NULL;
        if (value) {
            if (!daw_control_parse_target(value, &target)) { result = reply(false, "invalid_target"); goto done; }
            pointer = &target;
        }
        result = daw_control_regions(bridge, state, request, pointer, changed); goto done;
    }
    if (midi_write) {
        DawActionTarget target = {0};
        result = daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &target)
            ? daw_control_midi(bridge, state, request, target, changed) : reply(false, "invalid_target");
        goto done;
    }
    if (strcmp(op, "save_preset") == 0 || strcmp(op, "apply_preset") == 0) {
        DawActionTarget target;
        if (!daw_control_parse_target(cJSON_GetObjectItemCaseSensitive(request, "target"), &target)) { result = reply(false, "invalid_target"); goto done; }
        DawActionResult before = daw_action_get_instrument(state, target);
        if (!before.has_snapshot) { result = daw_control_action_json(bridge, before); goto done; }
        cJSON* object = snapshot_json(bridge, before.snapshot); char* current = cJSON_PrintUnformatted(object); cJSON_Delete(object);
        const char* expected = string_at(request, "expected_state");
        bool matches = current && expected && !strcmp(current, expected); free(current);
        if (!matches) { result = reply(false, "state_conflict"); goto done; }
        result = daw_control_presets(bridge, state, request, &before.snapshot, changed); goto done;
    }
    if (strcmp(op, "preview_preset") == 0) { result = daw_control_presets(bridge, state, request, NULL, changed); goto done; }
    result = strcmp(op, "set_instrument") == 0 ? set_instrument(bridge, state, request, changed)
        : instrument_history(bridge, state, request, strcmp(op, "redo_instrument") == 0, changed);
done:
    cJSON_Delete(request);
    if (result) cJSON_AddStringToObject(result, "instance", bridge->instance);
    if (result && receipt) cJSON_AddStringToObject(result, "request_id", receipt->id);
    char* response = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    if (receipt) {
        if (response && strlen(response) < DAW_CONTROL_MAX_RESPONSE) strcpy(receipt->response, response);
        free(response);
        return strdup(receipt->response);
    }
    return response;
}
