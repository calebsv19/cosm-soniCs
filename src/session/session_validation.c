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
            if (clip->media_path[0] == '\0') {
                session_set_error(error_message, error_message_len, "track %d clip %d missing media path", t, c);
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
