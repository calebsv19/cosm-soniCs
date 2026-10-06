// Exercises the actual default output endpoint with a silent project and explicit software telemetry.
#include "engine/engine.h"
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdio.h>

// Opens no capture device and renders no authored signal while checking physical-backend callback delivery.
int main(void) {
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL audio unavailable: %s\n", SDL_GetError());
        return 2;
    }
    printf("driver=%s outputs=%d\n", SDL_GetCurrentAudioDriver(), SDL_GetNumAudioDevices(0));
    for (int i = 0; i < SDL_GetNumAudioDevices(0); ++i)
        printf("output[%d]=%s\n", i, SDL_GetAudioDeviceName(i, 0));
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 512;
    cfg.output_queue_blocks = 4;
    Engine* engine = engine_create(&cfg);
    assert(engine);
    if (!engine_start(engine)) {
        fprintf(stderr, "Default output start failed: %s\n", SDL_GetError());
        engine_destroy(engine);
        SDL_Quit();
        return 2;
    }
    assert(engine_transport_play(engine));
    SDL_Delay(600);
    EngineDiagnostics before, after;
    assert(engine_get_diagnostics(engine, &before));
    SDL_Delay(10000);
    assert(engine_get_diagnostics(engine, &after));
    uint64_t callbacks = after.callback_count - before.callback_count;
    uint64_t missing = after.underrun_frames - before.underrun_frames;
    printf("{\"callbacks\":%llu,\"missing_output_frames\":%llu,\"priority_status\":%d,\"queue_target_frames\":%llu,\"silent_only\":true}\n",
           (unsigned long long)callbacks, (unsigned long long)missing,
           after.worker_priority_status, (unsigned long long)after.queue_target_frames);
    engine_destroy(engine);
    SDL_Quit();
    return callbacks && !missing ? 0 : 1;
}
