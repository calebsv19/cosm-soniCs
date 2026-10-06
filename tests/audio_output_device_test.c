#include "audio/audio_device.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SDL_AudioSpec requested;
static int mode, closes, opens, callback_frames, callback_channels;

// Supplies deterministic endpoint negotiation without opening physical audio hardware.
static SDL_AudioDeviceID fake_open(const char* name, int capture, const SDL_AudioSpec* want,
                                     SDL_AudioSpec* have, int flags) {
    (void)name;
    assert(capture == 0);
    assert(flags == SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    requested = *want;
    *have = *want;
    ++opens;
    have->samples = 256;
    if (mode == 1) have->format = AUDIO_S16;
    if (mode == 2) have->freq = 44100;
    if (mode == 3) have->channels = 1;
    if (mode == 4) have->samples = 0;
    return mode == 5 ? 0 : 17;
}

// Records resource release for both rejected and valid endpoints.
static void fake_close(SDL_AudioDeviceID id) { assert(id == 17); ++closes; }
// Keeps device lifecycle operations deterministic in this format contract test.
static void fake_pause(SDL_AudioDeviceID id, int paused) { assert(id == 17); (void)paused; }
// Pretends the audio subsystem is initialized so no platform backend is touched.
static Uint32 fake_was_init(Uint32 flags) { return flags; }

#define SDL_OpenAudioDevice fake_open
#define SDL_CloseAudioDevice fake_close
#define SDL_PauseAudioDevice fake_pause
#define SDL_WasInit fake_was_init
#include "../src/audio/device_sdl.c"

// Verifies callback frame interpretation and writes a recognizable float sample.
static void render(float* output, int frames, int channels, void* userdata) {
    assert(userdata == &requested);
    callback_frames = frames;
    callback_channels = channels;
    for (int i = 0; i < frames * channels; ++i) assert(output[i] == 0.0f);
    output[0] = 0.25f;
}

// Checks invalid inputs, negotiated format failures, callback clearing, and reopen cleanup.
int main(void) {
    AudioDevice device = {0};
    AudioDeviceSpec spec = {.sample_rate = 48000, .channels = 2, .block_size = 128};
    assert(!audio_device_open(&device, &spec, NULL, NULL));
    spec.channels = 256;
    assert(!audio_device_open(&device, &spec, render, &requested));
    spec.channels = 2;
    assert(opens == 0);
    for (mode = 1; mode <= 5; ++mode) {
        assert(!audio_device_open(&device, &spec, render, &requested));
        assert(!device.is_open && device.device_id == 0);
    }
    assert(closes == 4);
    mode = 0;
    for (int n = 0; n < 10; ++n) {
        assert(audio_device_open(&device, &spec, render, &requested));
        assert(device.spec.sample_rate == 48000 && device.spec.channels == 2);
        assert(device.spec.block_size == 256);
        float output[17];
        memset(output, 0x7f, sizeof(output));
        requested.callback(requested.userdata, (Uint8*)output, sizeof(output));
        assert(callback_frames == 8 && callback_channels == 2);
        assert(output[0] == 0.25f && output[16] == 0.0f);
        assert(audio_device_start(&device));
        audio_device_close(&device);
        audio_device_close(&device);
        assert(!device.is_open && !device.callback && !device.userdata);
    }
    assert(closes == 14);
    puts("audio_output_device_test: success");
    return 0;
}
