#include "audio/audio_capture_device.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void audio_capture_set_error(AudioCaptureDevice* device, const char* fmt, ...) {
    if (!device || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(device->last_error, sizeof(device->last_error), fmt, args);
    va_end(args);
}

static bool audio_capture_ensure_sdl(void) {
    if (SDL_WasInit(SDL_INIT_AUDIO) != 0) {
        return true;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_InitSubSystem(SDL_INIT_AUDIO) failed for capture: %s", SDL_GetError());
        return false;
    }
    return true;
}

static AudioDeviceSpec audio_capture_normalize_spec(const AudioDeviceSpec* desired) {
    AudioDeviceSpec spec = audio_capture_device_default_spec();
    if (!desired) {
        return spec;
    }
    if (desired->sample_rate > 0) {
        spec.sample_rate = desired->sample_rate;
    }
    if (desired->block_size > 0) {
        spec.block_size = desired->block_size;
    }
    if (desired->channels > 0) {
        spec.channels = desired->channels;
    }
    return spec;
}

static void sdl_capture_trampoline(void* userdata, Uint8* stream, int len) {
    AudioCaptureDevice* device = (AudioCaptureDevice*)userdata;
    if (!device || !device->callback || !stream || len <= 0) {
        return;
    }

    int channels = device->spec.channels > 0 ? device->spec.channels : 1;
    int frames = len / (int)(sizeof(float) * (size_t)channels);
    if (frames <= 0) {
        return;
    }

    device->callback((const float*)stream, frames, channels, device->userdata);
}

AudioDeviceSpec audio_capture_device_default_spec(void) {
    AudioDeviceSpec spec = {
        .sample_rate = 48000,
        .block_size = 512,
        .channels = 1
    };
    return spec;
}

void audio_capture_device_init(AudioCaptureDevice* device) {
    if (!device) {
        return;
    }
    SDL_zero(*device);
}

const char* audio_capture_device_last_error(const AudioCaptureDevice* device) {
    if (!device || device->last_error[0] == '\0') {
        return "";
    }
    return device->last_error;
}

int audio_capture_device_count(void) {
    if (!audio_capture_ensure_sdl()) {
        return -1;
    }
    return SDL_GetNumAudioDevices(SDL_TRUE);
}

const char* audio_capture_device_name(int index) {
    if (index < 0 || !audio_capture_ensure_sdl()) {
        return NULL;
    }
    return SDL_GetAudioDeviceName(index, SDL_TRUE);
}

bool audio_capture_device_open(AudioCaptureDevice* device,
                               const char* device_name,
                               const AudioDeviceSpec* desired,
                               AudioCaptureCallback cb,
                               void* userdata) {
    if (!device || !cb) {
        return false;
    }
    if (device->is_open) {
        audio_capture_device_close(device);
    }
    audio_capture_device_init(device);
    if (!audio_capture_ensure_sdl()) {
        audio_capture_set_error(device, "failed to initialise SDL audio capture: %s", SDL_GetError());
        return false;
    }

    AudioDeviceSpec spec = audio_capture_normalize_spec(desired);
    SDL_AudioSpec want = {0};
    want.freq = spec.sample_rate;
    want.format = AUDIO_F32;
    want.channels = (Uint8)spec.channels;
    want.samples = (Uint16)spec.block_size;
    want.callback = sdl_capture_trampoline;
    want.userdata = device;

    SDL_AudioSpec have = {0};
    const int allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                SDL_AUDIO_ALLOW_CHANNELS_CHANGE |
                                SDL_AUDIO_ALLOW_SAMPLES_CHANGE;
    SDL_AudioDeviceID dev_id = SDL_OpenAudioDevice(device_name, SDL_TRUE, &want, &have, allowed_changes);
    if (dev_id == 0) {
        audio_capture_set_error(device, "SDL_OpenAudioDevice capture failed: %s", SDL_GetError());
        return false;
    }
    if (have.format != AUDIO_F32) {
        SDL_CloseAudioDevice(dev_id);
        audio_capture_set_error(device, "capture device returned unsupported format %u", (unsigned)have.format);
        return false;
    }

    device->device_id = dev_id;
    device->spec.sample_rate = have.freq;
    device->spec.channels = have.channels;
    device->spec.block_size = have.samples;
    device->callback = cb;
    device->userdata = userdata;
    device->is_open = true;
    if (device_name && device_name[0] != '\0') {
        SDL_strlcpy(device->name, device_name, sizeof(device->name));
    } else {
        SDL_strlcpy(device->name, "Default Input", sizeof(device->name));
    }
    return true;
}

bool audio_capture_device_start(AudioCaptureDevice* device) {
    if (!device || !device->is_open) {
        audio_capture_set_error(device, "capture device is not open");
        return false;
    }
    SDL_PauseAudioDevice(device->device_id, 0);
    device->is_started = true;
    return true;
}

void audio_capture_device_stop(AudioCaptureDevice* device) {
    if (!device || !device->is_open) {
        return;
    }
    SDL_PauseAudioDevice(device->device_id, 1);
    device->is_started = false;
}

void audio_capture_device_close(AudioCaptureDevice* device) {
    if (!device) {
        return;
    }
    if (device->is_open) {
        audio_capture_device_stop(device);
        SDL_CloseAudioDevice(device->device_id);
    }
    audio_capture_device_init(device);
}
