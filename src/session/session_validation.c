#include "session_automation.h"
#include "session.h"
#include "ui/library_browser.h"

#include <stdarg.h>
#include <stdio.h>
#include <math.h>

static void session_set_error(char* buffer, size_t buffer_len, const char* fmt, ...) {
    if (!buffer || buffer_len == 0 || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, buffer_len, fmt, args);
    va_end(args);
}

static bool session_midi_note_fits_duration(const EngineMidiNote* note, uint64_t duration_frames) {
    if (!engine_midi_note_is_valid(note)) {
        return false;
    }
    if (note->start_frame > duration_frames) {
        return false;
    }
    return note->duration_frames <= duration_frames - note->start_frame;
}

bool session_document_validate(const SessionDocument* doc, char* error_message, size_t error_message_len) {
    if (!doc) {
        session_set_error(error_message, error_message_len, "document is null");
        return false;
    }
    if(doc->track_count<0 || (doc->track_count && !doc->tracks))return false;
    int effect_points=0;
    for(int t=-1;t<doc->track_count;++t){int count=t<0?doc->master_fx_count:doc->tracks[t].fx_count;const SessionFxInstance* effects=t<0?doc->master_fx:doc->tracks[t].fx;
        if(count<0 || count>FX_MASTER_MAX || (count && !effects))return false;
        for(int i=0;i<count;++i){if(!fx_parameter_automation_valid(effects[i].type,&effects[i].automation))return false;effect_points+=effects[i].automation.count;}
    }
    if(effect_points>FX_AUTOMATION_TOTAL_POINTS)return false;
    if (doc->version == 0) {
        session_set_error(error_message, error_message_len, "version is 0");
        return false;
    }
    if (doc->engine.sample_rate <= 0) {
        session_set_error(error_message, error_message_len, "invalid sample rate: %d", doc->engine.sample_rate);
        return false;
    }
    if (doc->engine.output_queue_blocks != 0 &&
        (doc->engine.output_queue_blocks < 2 || doc->engine.output_queue_blocks > 32)) {
        session_set_error(error_message, error_message_len, "invalid output queue blocks: %d", doc->engine.output_queue_blocks);
        return false;
    }
    if (doc->engine.block_size <= 0) {
        session_set_error(error_message, error_message_len, "invalid block size: %d", doc->engine.block_size);
        return false;
    }
    if (doc->tempo.bpm <= 0.0f) {
        session_set_error(error_message, error_message_len, "invalid tempo bpm: %.3f", doc->tempo.bpm);
        return false;
    }
    if (doc->tempo.ts_num <= 0) {
        session_set_error(error_message, error_message_len, "invalid tempo numerator: %d", doc->tempo.ts_num);
        return false;
    }
    if (doc->tempo.ts_den <= 0) {
        session_set_error(error_message, error_message_len, "invalid tempo denominator: %d", doc->tempo.ts_den);
        return false;
    }
    bool ts_den_power_of_two = (doc->tempo.ts_den & (doc->tempo.ts_den - 1)) == 0;
    if (!ts_den_power_of_two) {
        session_set_error(error_message, error_message_len, "tempo denominator must be power of two: %d", doc->tempo.ts_den);
        return false;
    }
    if (doc->tempo_event_count < 0) {
        session_set_error(error_message, error_message_len, "negative tempo event count");
        return false;
    }
    if (doc->tempo_event_count > 0 && !doc->tempo_events) {
        session_set_error(error_message, error_message_len, "tempo events missing");
        return false;
    }
    for (int i = 0; i < doc->tempo_event_count; ++i) {
        const SessionTempoEvent* evt = &doc->tempo_events[i];
        if (evt->beat < 0.0f) {
            session_set_error(error_message, error_message_len, "tempo event %d negative beat", i);
            return false;
        }
        if (evt->bpm <= 0.0f) {
            session_set_error(error_message, error_message_len, "tempo event %d bpm invalid", i);
            return false;
        }
    }
    if (doc->time_signature_event_count < 0) {
        session_set_error(error_message, error_message_len, "negative time signature event count");
        return false;
    }
    if (doc->time_signature_event_count > 0 && !doc->time_signature_events) {
        session_set_error(error_message, error_message_len, "time signature events missing");
        return false;
    }
    for (int i = 0; i < doc->time_signature_event_count; ++i) {
        const SessionTimeSignatureEvent* evt = &doc->time_signature_events[i];
        if (evt->beat < 0.0f) {
            session_set_error(error_message, error_message_len, "time signature event %d negative beat", i);
            return false;
        }
        if (evt->ts_num <= 0 || evt->ts_den <= 0) {
            session_set_error(error_message, error_message_len, "time signature event %d invalid", i);
            return false;
        }
        if (evt->ts_num > 64 || evt->ts_den > 64) {
            session_set_error(error_message, error_message_len,
                              "time signature event %d values out of range", i);
            return false;
        }
    }
    if (doc->engine.default_fade_in_ms < 0.0f || doc->engine.default_fade_out_ms < 0.0f) {
        session_set_error(error_message, error_message_len, "default fades must be non-negative");
        return false;
    }
    if (doc->engine.fade_preset_count < 0 || doc->engine.fade_preset_count > CONFIG_FADE_PRESET_MAX) {
        session_set_error(error_message, error_message_len, "invalid fade preset count: %d", doc->engine.fade_preset_count);
        return false;
    }
    for (int i = 0; i < doc->engine.fade_preset_count; ++i) {
        if (doc->engine.fade_preset_ms[i] < 0.0f) {
            session_set_error(error_message, error_message_len, "fade preset %d negative", i);
            return false;
        }
    }
    if (doc->loop.enabled && doc->loop.end_frame <= doc->loop.start_frame) {
        session_set_error(error_message, error_message_len, "loop end <= start");
        return false;
    }
    if (doc->midi_editor.panel_mode < 0 || doc->midi_editor.panel_mode > 1) {
        session_set_error(error_message, error_message_len,
                          "invalid MIDI editor panel mode: %d",
                          doc->midi_editor.panel_mode);
        return false;
    }
    if (doc->midi_editor.instrument_active_group < 0 ||
        doc->midi_editor.instrument_active_group >= engine_instrument_param_group_count()) {
        session_set_error(error_message, error_message_len,
                          "invalid MIDI instrument group: %d",
                          doc->midi_editor.instrument_active_group);
        return false;
    }
    if (doc->selection_count < 0 || doc->selection_count > SESSION_TIMELINE_SELECTION_MAX) {
        session_set_error(error_message, error_message_len, "invalid selection count: %d", doc->selection_count);
        return false;
    }
    if (doc->library.panel_mode < LIBRARY_PANEL_MODE_SOURCE ||
        doc->library.panel_mode > LIBRARY_PANEL_MODE_IN_PROJECT) {
        session_set_error(error_message, error_message_len, "invalid library panel mode: %d", doc->library.panel_mode);
        return false;
    }
    if (doc->track_count > 0 && !doc->tracks) {
        session_set_error(error_message, error_message_len, "track array missing");
        return false;
    }
    if (doc->track_count < 0) {
        session_set_error(error_message, error_message_len, "negative track count");
        return false;
    }
    int return_count = 0,mixer_points=0;
    bool has_routing = false;
    for (int t = 0; t < doc->track_count; ++t) {
        const SessionTrack* track = &doc->tracks[t];
        if ((track->is_group && track->is_return) || (!track->is_group && track->folded) || track->parent_track < 0 ||
            track->parent_track > doc->track_count || track->sibling_order < 0 || track->sibling_order > 1000000 ||
            (track->is_return && track->parent_track) || (track->is_group && (track->clip_count || track->midi_instrument_enabled))) {
            session_set_error(error_message,error_message_len,"invalid group lane %d",t); return false;
        }
        int parent=track->parent_track;
        for (int depth=0; parent; ++depth) {
            if(depth>=doc->track_count || parent==t+1 || !doc->tracks[parent-1].is_group) {
                session_set_error(error_message,error_message_len,"invalid group cycle %d",t); return false;
            }
            parent=doc->tracks[parent-1].parent_track;
            if(parent<0 || parent>doc->track_count)return false;
        }
        if(track->persistent_id>INT64_MAX)return false;
        for(int prior=0;track->persistent_id && prior<t;++prior) if(doc->tracks[prior].persistent_id==track->persistent_id) {
            session_set_error(error_message,error_message_len,"duplicate persistent track identity");return false;
        }
        if (track->is_group || track->parent_track) has_routing=true;
        if (track->is_return) {
            has_routing = true;
            if (++return_count > ENGINE_RETURN_CAP || track->clip_count || track->midi_instrument_enabled) {
                session_set_error(error_message, error_message_len, "invalid return lane %d", t); return false;
            }
        }
        if(!engine_mixer_automation_valid(&track->volume_automation,10) || (!track->is_group && track->volume_automation.count)){session_set_error(error_message,error_message_len,"invalid group automation %d",t);return false;}
        mixer_points+=track->volume_automation.count;
        for (int send = 0; send < ENGINE_SEND_CAP; ++send) {
            SessionSend slot = track->sends[send];mixer_points+=slot.automation.count;
            if(mixer_points>ENGINE_MIX_AUTOMATION_TOTAL){session_set_error(error_message,error_message_len,"mixer automation point limit exceeded");return false;}
            if (!engine_mixer_automation_valid(&slot.automation,1) || (!slot.destination_track && slot.automation.count) || !isfinite(slot.gain) || slot.gain < 0 || slot.gain > 1 || slot.destination_track < 0 ||
                slot.destination_track > doc->track_count || (!slot.destination_track && slot.gain) ||
                (track->is_return && (slot.destination_track || slot.gain))) {
                session_set_error(error_message, error_message_len, "invalid send %d:%d", t, send); return false;
            }
            if (!slot.destination_track) continue;
            has_routing = true;
            if (!doc->tracks[slot.destination_track - 1].is_return || slot.destination_track == t + 1) {
                session_set_error(error_message, error_message_len, "invalid send destination %d:%d", t, send); return false;
            }
            for (int previous = 0; previous < send; ++previous)
                if (track->sends[previous].destination_track == slot.destination_track) {
                    session_set_error(error_message, error_message_len, "duplicate send %d:%d", t, send); return false;
                }
        }
    }
    if (has_routing && doc->track_count > ENGINE_ROUTING_TRACK_CAP) {
        session_set_error(error_message, error_message_len, "routing track limit exceeded"); return false;
    }
    for (int t = 0; t < doc->track_count; ++t) {
        const SessionTrack* track = &doc->tracks[t];
        if (!track) {
            session_set_error(error_message, error_message_len, "track %d is null", t);
            return false;
        }
        if (track->clip_count < 0) {
            session_set_error(error_message, error_message_len, "track %d has negative clip count", t);
            return false;
        }
        if (track->clip_count > 0 && !track->clips) {
            session_set_error(error_message, error_message_len, "track %d clips missing", t);
            return false;
        }
        if (track->fx_count < 0 || track->fx_count > FX_MASTER_MAX) {
            session_set_error(error_message, error_message_len, "track %d fx count invalid", t);
            return false;
        }
        if (track->fx_count > 0 && !track->fx) {
            session_set_error(error_message, error_message_len, "track %d fx array missing", t);
            return false;
        }
        if (track->midi_instrument_enabled &&
            track->midi_instrument_preset != engine_instrument_preset_clamp(track->midi_instrument_preset)) {
            session_set_error(error_message, error_message_len, "track %d midi instrument preset invalid", t);
            return false;
        }
        if (track->midi_instrument_enabled) {
            EngineInstrumentParams clamped =
                engine_instrument_params_sanitize(track->midi_instrument_preset, track->midi_instrument_params);
            bool params_valid = true;
            for (int p = 0; p < ENGINE_INSTRUMENT_PARAM_COUNT; ++p) {
                EngineInstrumentParamId param = (EngineInstrumentParamId)p;
                if (engine_instrument_params_get(track->midi_instrument_params, param) !=
                    engine_instrument_params_get(clamped, param)) {
                    params_valid = false;
                    break;
                }
            }
            if (!params_valid) {
                session_set_error(error_message, error_message_len, "track %d midi instrument params invalid", t);
                return false;
            }
        }
        if (track->midi_instrument_automation_lane_count < 0) {
            session_set_error(error_message, error_message_len, "track %d midi automation lane count invalid", t);
            return false;
        }
        if (track->midi_instrument_automation_lane_count > 0 && !track->midi_instrument_automation_lanes) {
            session_set_error(error_message, error_message_len, "track %d midi automation lanes missing", t);
            return false;
        }
        for (int l = 0; l < track->midi_instrument_automation_lane_count; ++l) {
            const SessionAutomationLane* lane = &track->midi_instrument_automation_lanes[l];
            if (!engine_automation_target_is_instrument_param(lane->target) ||
                !session_automation_lane_validate(lane, UINT64_MAX)) {
                session_set_error(error_message, error_message_len, "track %d midi automation target invalid", t);
                return false;
            }
            if (lane->point_count < 0) {
                session_set_error(error_message, error_message_len, "track %d midi automation point count invalid", t);
                return false;
            }
            if (lane->point_count > 0 && !lane->points) {
                session_set_error(error_message, error_message_len, "track %d midi automation points missing", t);
                return false;
            }
        }
        for (int f = 0; f < track->fx_count; ++f) {
            const SessionFxInstance* fx = &track->fx[f];
            if (fx->param_count > FX_MAX_PARAMS) {
                session_set_error(error_message, error_message_len, "track %d fx %d param count too large", t, f);
                return false;
            }
        }
        for (int c = 0; c < track->clip_count; ++c) {
            const SessionClip* clip = &track->clips[c];
            if (!clip) {
                session_set_error(error_message, error_message_len, "track %d clip %d is null", t, c);
                return false;
            }
            if (clip->kind < ENGINE_CLIP_KIND_AUDIO || clip->kind > ENGINE_CLIP_KIND_MIDI) {
                session_set_error(error_message, error_message_len, "track %d clip %d kind invalid", t, c);
                return false;
            }
            if (clip->kind == ENGINE_CLIP_KIND_AUDIO &&
                clip->media_path[0] == '\0' &&
                clip->media_id[0] == '\0') {
                session_set_error(error_message, error_message_len, "track %d clip %d missing media id/path", t, c);
                return false;
            }
            if (clip->duration_frames == 0) {
                session_set_error(error_message, error_message_len, "track %d clip %d has zero duration", t, c);
                return false;
            }
            if (clip->fade_in_frames > clip->duration_frames) {
                session_set_error(error_message, error_message_len, "track %d clip %d fade-in exceeds duration", t, c);
                return false;
            }
            if (clip->fade_out_frames > clip->duration_frames) {
                session_set_error(error_message, error_message_len, "track %d clip %d fade-out exceeds duration", t, c);
                return false;
            }
            if (clip->fade_in_curve < 0 || clip->fade_in_curve >= ENGINE_FADE_CURVE_COUNT) {
                session_set_error(error_message, error_message_len, "track %d clip %d fade-in curve invalid", t, c);
                return false;
            }
            if (clip->fade_out_curve < 0 || clip->fade_out_curve >= ENGINE_FADE_CURVE_COUNT) {
                session_set_error(error_message, error_message_len, "track %d clip %d fade-out curve invalid", t, c);
                return false;
            }
            if (clip->midi_note_count < 0 || clip->midi_note_count > ENGINE_MIDI_NOTE_CAP) {
                session_set_error(error_message, error_message_len, "track %d clip %d midi note count invalid", t, c);
                return false;
            }
            if (clip->midi_note_count > 0 && !clip->midi_notes) {
                session_set_error(error_message, error_message_len, "track %d clip %d midi notes missing", t, c);
                return false;
            }
            if (clip->kind == ENGINE_CLIP_KIND_AUDIO && clip->midi_note_count > 0) {
                session_set_error(error_message, error_message_len, "track %d clip %d audio clip has midi notes", t, c);
                return false;
            }
            if (clip->kind == ENGINE_CLIP_KIND_MIDI &&
                clip->instrument_preset != engine_instrument_preset_clamp(clip->instrument_preset)) {
                session_set_error(error_message, error_message_len, "track %d clip %d instrument preset invalid", t, c);
                return false;
            }
            if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
                EngineInstrumentParams clamped =
                    engine_instrument_params_sanitize(clip->instrument_preset, clip->instrument_params);
                bool params_valid = true;
                for (int p = 0; p < ENGINE_INSTRUMENT_PARAM_COUNT; ++p) {
                    EngineInstrumentParamId param = (EngineInstrumentParamId)p;
                    if (engine_instrument_params_get(clip->instrument_params, param) !=
                        engine_instrument_params_get(clamped, param)) {
                        params_valid = false;
                        break;
                    }
                }
                if (!params_valid) {
                    session_set_error(error_message,
                                      error_message_len,
                                      "track %d clip %d instrument params invalid",
                                      t,
                                      c);
                    return false;
                }
            }
            for (int n = 0; n < clip->midi_note_count; ++n) {
                if (!session_midi_note_fits_duration(&clip->midi_notes[n], clip->duration_frames)) {
                    session_set_error(error_message, error_message_len, "track %d clip %d midi note out of range", t, c);
                    return false;
                }
            }
            if (clip->automation_lane_count < 0) {
                session_set_error(error_message, error_message_len, "track %d clip %d automation lane count invalid", t, c);
                return false;
            }
            for (int l = 0; l < clip->automation_lane_count; ++l) {
                const SessionAutomationLane* lane = &clip->automation_lanes[l];
                if (!session_automation_lane_validate(lane, clip->duration_frames)) {
                    session_set_error(error_message, error_message_len, "track %d clip %d automation lane invalid", t, c);
                    return false;
                }
                if (lane->point_count < 0) {
                    session_set_error(error_message, error_message_len, "track %d clip %d automation point count invalid", t, c);
                    return false;
                }
                if (lane->point_count > 0 && !lane->points) {
                    session_set_error(error_message, error_message_len, "track %d clip %d automation points missing", t, c);
                    return false;
                }
                for (int p = 0; p < lane->point_count; ++p) {
                    if (lane->points[p].frame > clip->duration_frames) {
                        session_set_error(error_message, error_message_len, "track %d clip %d automation point out of range", t, c);
                        return false;
                    }
                }
            }
        }
    }
    if (doc->master_fx_count < 0 || doc->master_fx_count > FX_MASTER_MAX) {
        session_set_error(error_message, error_message_len, "invalid master fx count %d", doc->master_fx_count);
        return false;
    }
    if (doc->master_fx_count > 0 && !doc->master_fx) {
        session_set_error(error_message, error_message_len, "master fx array missing");
        return false;
    }
    for (int i = 0; i < doc->master_fx_count; ++i) {
        const SessionFxInstance* fx = &doc->master_fx[i];
        if (fx->param_count > FX_MAX_PARAMS) {
            session_set_error(error_message, error_message_len, "master fx %d param count too large", i);
            return false;
        }
    }
    return true;
}
