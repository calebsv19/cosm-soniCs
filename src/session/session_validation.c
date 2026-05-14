#include "session.h"

#include <stdarg.h>
#include <stdio.h>

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
    if (doc->version == 0) {
        session_set_error(error_message, error_message_len, "version is 0");
        return false;
    }
    if (doc->engine.sample_rate <= 0) {
        session_set_error(error_message, error_message_len, "invalid sample rate: %d", doc->engine.sample_rate);
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
    if (doc->track_count < 0) {
        session_set_error(error_message, error_message_len, "negative track count");
        return false;
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
                if (clip->instrument_params.level != clamped.level ||
                    clip->instrument_params.tone != clamped.tone ||
                    clip->instrument_params.attack_ms != clamped.attack_ms ||
                    clip->instrument_params.release_ms != clamped.release_ms) {
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
                if (lane->target < 0 || lane->target >= ENGINE_AUTOMATION_TARGET_COUNT) {
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
