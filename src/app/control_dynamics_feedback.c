#include "app_state.h"
#include "daw/fx_actions.h"
#include "cJSON.h"
#include <inttypes.h>
#include <stdio.h>
// Adds ephemeral dynamics observations alongside, never inside, authored chain token state.
void daw_control_dynamics_feedback(cJSON* reply, const AppState* s, const DawFxSnapshot* chain) {
    cJSON* array = cJSON_AddArrayToObject(reply, "dynamics_feedback");
    int track = -1;
    if (chain->track_id) {
        const EngineTrack* tracks = engine_get_tracks(s->engine);
        for (int n = 0; n < engine_get_track_count(s->engine); ++n)
            if (tracks[n].runtime_id == chain->track_id) track = n;
        if (track < 0) return;
    }
    for (int n = 0; n < chain->chain.count; ++n) {
        const FxMasterInstanceInfo* fx = &chain->chain.items[n];
        if (fx->type != 20 && fx->type != 21) continue;
        EngineGainReductionSnapshot v = {0};
        if (!engine_get_gain_reduction(s->engine, !chain->track_id, track, fx->id, &v)) continue;
        cJSON* j = cJSON_CreateObject(); cJSON_AddItemToArray(array, j);
        cJSON_AddNumberToObject(j, "effect_id", fx->id);
        cJSON_AddNumberToObject(j, "type_id", fx->type);
        cJSON_AddBoolToObject(j, "enabled", v.enabled);
        cJSON_AddBoolToObject(j, "available", v.available);
        cJSON_AddBoolToObject(j, "fresh", v.fresh);
        cJSON_AddBoolToObject(j, "playing", engine_transport_requested_playing(s->engine));
        if (v.available) {
            cJSON_AddNumberToObject(j, "gain_db", v.gain_db);
            cJSON_AddNumberToObject(j, "reduction_db", -v.gain_db);
            cJSON_AddNumberToObject(j, "age_ms", v.age_ns / 1e6);
        } else {
            cJSON_AddNullToObject(j, "gain_db");cJSON_AddNullToObject(j, "reduction_db");cJSON_AddNullToObject(j, "age_ms");
        }
        char seq[32];snprintf(seq,sizeof(seq),"%" PRIu64,v.sequence);cJSON_AddStringToObject(j,"sequence",seq);
        cJSON_AddStringToObject(j, "measurement", "Most negative DSP gain in the last process call, before makeup and bypass blending; block-size-dependent maximum, not a 400 ms window");
        cJSON_AddStringToObject(j, "stereo", fx->type == 20 ? "independent channel detectors; maximum reduction across channels" : "linked channels, integer-sample lookahead; sample-peak ceiling, not true peak");
    }
}
