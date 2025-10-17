#pragma once

#include <stdbool.h>
#include <SDL2/SDL.h>

typedef struct {
    int sample_rate;
    int block_size;
    int channels;
} AudioDeviceSpec;

typedef void (*AudioDeviceCallback)(float* output, int frames, int channels, void* userdata);

typedef struct {
    SDL_AudioDeviceID device_id;
    AudioDeviceSpec spec;
    AudioDeviceCallback callback;
    void* userdata;
    bool is_open;
} AudioDevice;

bool audio_device_open(AudioDevice* device, const AudioDeviceSpec* desired, AudioDeviceCallback cb, void* userdata);
void audio_device_close(AudioDevice* device);
bool audio_device_start(AudioDevice* device);
void audio_device_stop(AudioDevice* device);
