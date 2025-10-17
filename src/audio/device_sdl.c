#include "audio/audio_device.h"

#include <SDL2/SDL.h>
#include <string.h>

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

bool audio_device_open(AudioDevice* device, const AudioDeviceSpec* desired, AudioDeviceCallback cb, void* userdata) {
    if (!device || !desired) {
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
                                                   SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (dev_id == 0) {
        SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return false;
    }

    device->device_id = dev_id;
    device->spec.sample_rate = have.freq;
    device->spec.channels = have.channels;
    device->spec.block_size = have.samples;
    device->is_open = true;

    if (have.format != AUDIO_F32) {
        SDL_Log("Warning: obtained audio format is not float32, got format %u", (unsigned)have.format);
    }
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
