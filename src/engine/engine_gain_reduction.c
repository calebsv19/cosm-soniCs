#include "engine/engine_internal.h"
#include "core_time.h"
#include <math.h>
#include <string.h>
// Copies a current compressor/limiter observation without consuming preview history or DSP state.
bool engine_get_gain_reduction(const Engine* e, bool master, int track, FxInstId id,
                              EngineGainReductionSnapshot* out) {
    if (!e || !out || !id || !engine_is_control_thread(e)) return false;
    *out = (EngineGainReductionSnapshot){0};
    FxMasterSnapshot chain = {0};
    if (!(master ? engine_fx_master_snapshot(e, &chain) : engine_fx_track_snapshot(e, track, &chain))) return false;
    const FxMasterInstanceInfo* found = NULL;
    for (int n = 0; n < chain.count; ++n) if (chain.items[n].id == id) found = &chain.items[n];
    if (!found || (found->type != 20 && found->type != 21)) return false;
    out->enabled = found->enabled;
    if (!found->enabled) return true;
    SDL_LockMutex(e->meter_mutex);
    const EngineFxScopeBank* bank = master ? &e->scope_host.master :
        (track >= 0 && track < e->scope_host.track_capacity ? &e->scope_host.tracks[track] : NULL);
    if (bank) for (int n = 0; n < bank->count; ++n) {
        if (bank->taps[n].id == id && bank->taps[n].kind == ENGINE_SCOPE_STREAM_GAIN_REDUCTION) {
            *out = bank->taps[n].latest;
            out->enabled = true;
            break;
        }
    }
    SDL_UnlockMutex(e->meter_mutex);
    uint64_t now = core_time_now_ns();
    out->age_ns = out->measured_ns && now >= out->measured_ns ? now - out->measured_ns : UINT64_MAX;
    out->available = out->available && isfinite(out->gain_db) && out->gain_db <= 0;
    out->fresh = out->available && out->age_ns <= UINT64_C(1000000000);
    return true;
}
