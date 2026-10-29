#include "engine/engine_internal.h"
#include "engine/groups.h"
// Observes only live production/audition output, never a private bounce/diagnostic render.
void engine_live_meter_observe(Engine* engine, EngineMixState* mix, int track, uint64_t start,
                               const float* pcm, int frames, int channels) {
    if (!engine || !mix || !mix->track_meters)
        return;
    uint64_t latency = 0;
    if (track < 0)
        latency = mix->group_mix ? engine_group_mix_latency(mix->group_mix, mix)
                                 : fxm_processing_latency(mix->fxm);
    else
        latency = (mix->group_mix ? engine_group_mix_input_latency(mix->group_mix, track)
                   : mix->tracks[track].is_return ? fxm_source_processing_latency(mix->fxm)
                                                  : 0) +
                  fxm_track_processing_latency(mix->fxm, track, false);
    EngineMeterState* m = track < 0 ? &mix->master_meter : &mix->track_meters[track];
    engine_live_meter_feed(&m->live, pcm, frames, channels, engine->config.sample_rate, start,
                           latency);
}
