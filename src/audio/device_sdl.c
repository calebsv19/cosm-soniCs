#include "audio/audio_device.h"

#include <SDL2/SDL.h>
#include <string.h>

// Clears each callback buffer before rendering complete float frames into it.
static void sdl_audio_trampoline(void* userdata, Uint8* stream, int len) {
    AudioDevice* device = (AudioDevice*)userdata;
    if (!device) {
        SDL_memset(stream, 0, len);
        return;
    }

    int channels = device->spec.channels > 0 ? device->spec.channels : 1;
    int frames = len / (sizeof(float) * channels);
    float* output = (float*)stream;
    SDL_memset(stream, 0, len);
    if (device->callback) {
        device->callback(output, frames, channels, device->userdata);
    }
}

// Opens a paused float endpoint with a fixed engine rate and channel layout.
bool audio_device_open(AudioDevice* device, const AudioDeviceSpec* desired, AudioDeviceCallback cb, void* userdata) {
    if (!device || !desired || !cb || desired->sample_rate <= 0 ||
        desired->channels < 1 || desired->channels > 255 ||
        desired->block_size < 1 || desired->block_size > 65535) {
        return false;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SDL_Log("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s", SDL_GetError());
            return false;
        }
    }

    SDL_zero(*device);
    device->callback = cb;
    device->userdata = userdata;

    SDL_AudioSpec want = {0};
    want.freq = desired->sample_rate;
    want.format = AUDIO_F32;
    want.channels = (Uint8)desired->channels;
    want.samples = (Uint16)desired->block_size;
    want.callback = sdl_audio_trampoline;
    want.userdata = device;

    SDL_AudioSpec have = {0};
    SDL_AudioDeviceID dev_id = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                                   SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (dev_id == 0) {
        SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return false;
    }

    // SDL may convert the hardware format internally; our callback must remain float.
    if (have.format != AUDIO_F32 || have.freq != want.freq ||
        have.channels != want.channels || have.samples == 0) {
        SDL_Log("SDL output endpoint violated the requested float/rate/channel contract");
        SDL_CloseAudioDevice(dev_id);
        SDL_zero(*device);
        return false;
    }
    device->device_id = dev_id;
    device->spec.sample_rate = have.freq;
    device->spec.channels = have.channels;
    device->spec.block_size = have.samples;
    device->is_open = true;

    return true;
}

void audio_device_close(AudioDevice* device) {
    if (!device || !device->is_open) {
        return;
    }
    audio_device_stop(device);
    SDL_CloseAudioDevice(device->device_id);
    device->device_id = 0;
    device->is_open = false;
    device->callback = NULL;
    device->userdata = NULL;
}

bool audio_device_start(AudioDevice* device) {
    if (!device || !device->is_open) {
        return false;
    }
    SDL_PauseAudioDevice(device->device_id, 0);
    return true;
}

void audio_device_stop(AudioDevice* device) {
    if (!device || !device->is_open) {
        return;
    }
    SDL_PauseAudioDevice(device->device_id, 1);
}
