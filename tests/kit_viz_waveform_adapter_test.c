#include "ui/kit_viz_waveform_adapter.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>

static void fail(const char *msg) {
    fprintf(stderr, "kit_viz_waveform_adapter_test: %s\n", msg);
    exit(1);
}

static void expect(int cond, const char *msg) {
    if (!cond) {
        fail(msg);
    }
}

int main(void) {
    WaveformCache cache;
    waveform_cache_init(&cache);

    const int channels = 2;
    const uint64_t frames = 128;
    float samples[frames * channels];
    for (uint64_t i = 0; i < frames; ++i) {
        float t = (float)i / (float)frames;
        float v = (t * 2.0f) - 1.0f;
        samples[i * 2] = v;
        samples[i * 2 + 1] = -v;
    }

    AudioMediaClip clip = {
        .samples = samples,
        .frame_count = frames,
        .channels = channels,
        .sample_rate = 48000,
    };

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 256, 80, 32, SDL_PIXELFORMAT_RGBA32);
    expect(surface != NULL, "failed to create software surface");

    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    expect(renderer != NULL, "failed to create software renderer");

    SDL_Rect rect = {0, 0, 256, 80};
    SDL_Color color = {130, 180, 230, 255};

    DawKitVizWaveformRequest good = {
        .renderer = renderer,
        .cache = &cache,
        .clip = &clip,
        .source_path = "inmem://clip",
        .target_rect = &rect,
        .view_start_frame = 0,
        .view_frame_count = frames,
        .color = color,
    };

    expect(daw_kit_viz_render_waveform_ex(&good) == DAW_KIT_VIZ_WAVEFORM_RENDERED,
           "expected valid adapter request to render");

    DawKitVizWaveformRequest missing_path = good;
    missing_path.source_path = NULL;
    expect(daw_kit_viz_render_waveform_ex(&missing_path) == DAW_KIT_VIZ_WAVEFORM_INVALID_REQUEST,
           "expected missing path to fail safely");

    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    waveform_cache_shutdown(&cache);

    puts("kit_viz_waveform_adapter_test: success");
    return 0;
}
