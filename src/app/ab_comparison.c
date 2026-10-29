#include "app_state.h"
#include "daw/ab_comparison.h"
#include "daw/midi_actions.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Reads only the monotonically owned comparison revision.
uint64_t daw_ab_revision(const AppState* s) {
    return s ? s->ab_revision : 0;
}
// Measures independent channel samples together without cancellation or loudness claims.
static bool meter(const EngineBounceBuffer* b, double* rms, double* peak) {
    if (!b || !b->data || !b->frame_count || b->channels < 1 || b->channels > 8 ||
        b->frame_count > DAW_AB_BYTES / sizeof(float) / (unsigned)b->channels)
        return false;
    double power = 0;
    *peak = 0;
    uint64_t n = b->frame_count * b->channels;
    for (uint64_t i = 0; i < n; ++i) {
        double v = b->data[i];
        if (!isfinite(v))
            return false;
        power += v * v;
        if (fabs(v) > *peak)
            *peak = fabs(v);
    }
    *rms = sqrt(power / n);
    return isfinite(*rms);
}
// Attenuates to the quieter non-silent capture, then applies one shared headroom factor.
bool daw_ab_gains(const EngineBounceBuffer* a, const EngineBounceBuffer* b, double gains[4],
                  bool* available) {
    if (!gains || !available || !a || !b || a->frame_count != b->frame_count ||
        a->sample_rate != b->sample_rate || a->channels != b->channels)
        return false;
    double rms[2], peak[2];
    if (!meter(a, &rms[0], &peak[0]) || !meter(b, &rms[1], &peak[1]))
        return false;
    *available = rms[0] > 1e-12 && rms[1] > 1e-12;
    double quiet = fmin(rms[0], rms[1]);
    for (int k = 0; k < 2; ++k)
        gains[k] = *available ? quiet / rms[k] : 1;
    double matched_peak = fmax(peak[0] * gains[0], peak[1] * gains[1]);
    double common = matched_peak > .95 ? .95 / matched_peak : 1;
    gains[0] *= common;
    gains[1] *= common;
    double original_peak = fmax(peak[0], peak[1]);
    gains[2] = gains[3] = original_peak > .95 ? .95 / original_peak : 1;
    return true;
}
// Requires aligned project, range, scope and stream geometry while permitting processing/version
// differences.
static bool compatible(const DawCompositionRender* a, const DawCompositionRender* b) {
    const DawCompositionRequest* x = &a->request;
    const DawCompositionRequest* y = &b->request;
    return a->generation == b->generation && a->audio.channels == b->audio.channels &&
           a->audio.sample_rate == b->audio.sample_rate &&
           a->audio.frame_count == b->audio.frame_count && x->has_target == y->has_target &&
           x->start == y->start && x->end == y->end && x->tail == y->tail &&
           (!x->has_target ||
            (x->target.kind == y->target.kind && x->target.track_id == y->target.track_id &&
             x->target.clip_id == y->target.clip_id));
}
// Renders a complete candidate before replacing the accepted A/B cache or its independent
// provenance.
DawActionStatus daw_ab_capture(AppState* s, uint64_t revision, const DawMixSnapshot* expected,
                               int side, const DawCompositionRequest* specification) {
    if (!s || !s->engine || !expected || !specification || side < 0 || side > 1)
        return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(s->engine))
        return DAW_ACTION_WRONG_THREAD;
    if (revision != s->ab_revision || !daw_mix_snapshot_current(s, expected))
        return DAW_ACTION_STALE_TARGET;
    if (daw_midi_actions_busy(s) || engine_transport_requested_playing(s->engine) ||
        engine_transport_is_playing(s->engine) || engine_preview_active(s->engine))
        return DAW_ACTION_BUSY;
    if (s->ab_revision == UINT64_MAX)
        return DAW_ACTION_HISTORY_UNAVAILABLE;
    if (side == 1 && (!s->ab || !s->ab->captures[0].audio.data ||
                      s->ab->generation != engine_instance_id(s->engine)))
        return DAW_ACTION_INVALID;
    DawCompositionRender* candidate = calloc(1, sizeof(*candidate));
    DawMixSnapshot* provenance = NULL;
    DawActionStatus copied = daw_action_get_mix_snapshot(s, &provenance);
    if (!candidate || copied != DAW_ACTION_UNCHANGED) {
        free(candidate);
        daw_mix_snapshot_free(provenance);
        return DAW_ACTION_HISTORY_UNAVAILABLE;
    }
    DawCompositionRequest request = *specification;
    request.comparison_context = true;
    DawActionStatus status = daw_action_render_composition(s, &request, candidate);
    if (status != DAW_ACTION_UNCHANGED)
        goto done;
    if (!daw_mix_snapshot_current(s, expected)) {
        status = DAW_ACTION_STALE_TARGET;
        goto done;
    }
    double gains[4] = {1, 1, 1, 1};
    bool matched = false;
    uint64_t bytes =
        candidate->audio.frame_count * (uint64_t)candidate->audio.channels * sizeof(float);
    if (side == 1) {
        const DawCompositionRender* a = &s->ab->captures[0];
        if (!compatible(a, candidate) ||
            bytes >
                DAW_AB_BYTES - a->audio.frame_count * (uint64_t)a->audio.channels * sizeof(float) ||
            !daw_ab_gains(&a->audio, &candidate->audio, gains, &matched)) {
            status = DAW_ACTION_INVALID;
            goto done;
        }
    }
    if (bytes > DAW_AB_BYTES / 2) {
        status = DAW_ACTION_INVALID;
        goto done;
    }
    if (!s->ab) {
        s->ab = calloc(1, sizeof(*s->ab));
        if (!s->ab) {
            status = DAW_ACTION_HISTORY_UNAVAILABLE;
            goto done;
        }
    }
    if (side == 0) {
        daw_composition_render_free(&s->ab->captures[1]);
        daw_mix_snapshot_free(s->ab->provenance[1]);
        s->ab->provenance[1] = NULL;
    }
    daw_composition_render_free(&s->ab->captures[side]);
    daw_mix_snapshot_free(s->ab->provenance[side]);
    s->ab->captures[side] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    s->ab->provenance[side] = provenance;
    provenance = NULL;
    s->ab->generation = engine_instance_id(s->engine);
    s->ab->revision = ++s->ab_revision;
    s->ab->ready = side == 1;
    s->ab->match_available = matched;
    s->ab->selection = 0;
    memcpy(s->ab->gains, gains, sizeof(gains));
    status = DAW_ACTION_APPLIED;
done:
    daw_composition_render_free(candidate);
    free(candidate);
    daw_mix_snapshot_free(provenance);
    return status;
}
// Shares native/MCP stopped transport and exact immutable comparison admission.
DawActionStatus daw_ab_listen(AppState* s, uint64_t revision, int side, bool matched,
                              bool restart) {
    if (!s || !s->engine || side < 0 || side > 1)
        return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(s->engine))
        return DAW_ACTION_WRONG_THREAD;
    DawAbComparison* c = s->ab;
    if (revision != s->ab_revision || !c || c->generation != engine_instance_id(s->engine))
        return DAW_ACTION_STALE_TARGET;
    if (!c->ready || (matched && !c->match_available))
        return DAW_ACTION_INVALID;
    if (daw_midi_actions_busy(s) || engine_transport_requested_playing(s->engine) ||
        engine_transport_is_playing(s->engine))
        return DAW_ACTION_BUSY;
    int selection = side + (matched ? 0 : 2);
    uint64_t position;
    bool active = engine_comparison_status(s->engine, c->revision, &position);
    bool ok = active && !restart
                  ? engine_comparison_select(s->engine, c->revision, selection)
                  : engine_comparison_play(s->engine, &c->captures[0].audio, &c->captures[1].audio,
                                           c->gains, c->revision, selection);
    if (!ok)
        return engine_preview_active(s->engine) ? DAW_ACTION_BUSY : DAW_ACTION_ENGINE_REJECTED;
    c->selection = selection;
    return DAW_ACTION_APPLIED;
}
// Cancels only the retained pair without moving authored transport or consuming history.
DawActionStatus daw_ab_stop(AppState* s, uint64_t revision) {
    if (!s || !s->engine)
        return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(s->engine))
        return DAW_ACTION_WRONG_THREAD;
    if (revision != s->ab_revision || !s->ab || s->ab->generation != engine_instance_id(s->engine))
        return DAW_ACTION_STALE_TARGET;
    return engine_comparison_stop(s->engine, s->ab->revision) ? DAW_ACTION_APPLIED
                                                              : DAW_ACTION_UNCHANGED;
}
// Frees immutable buffers independently from their device-owned playback copies.
void daw_ab_release(AppState* s) {
    if (!s)
        return;
    if (s->ab) {
        engine_comparison_stop(s->engine, s->ab->revision);
        for (int k = 0; k < 2; ++k) {
            daw_composition_render_free(&s->ab->captures[k]);
            daw_mix_snapshot_free(s->ab->provenance[k]);
        }
        free(s->ab);
        s->ab = NULL;
    }
    if (s->ab_revision < UINT64_MAX)
        ++s->ab_revision;
}
