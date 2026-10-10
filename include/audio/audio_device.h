#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
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
    float* preview_pcm; // Owns immutable finite playback samples until control-thread retirement.
    uint64_t preview_frames, preview_cursor;
    atomic_bool preview_active;

} AudioDevice;

bool audio_device_open(AudioDevice* device, const AudioDeviceSpec* desired, AudioDeviceCallback cb, void* userdata);
void audio_device_close(AudioDevice* device);
bool audio_device_start(AudioDevice* device);
void audio_device_stop(AudioDevice* device);

// Copies bounded original-level PCM into the open endpoint without altering current playback on failure.
bool audio_device_preview_play(AudioDevice*, const float*, uint64_t frames, int channels, int rate);
// Stops finite playback and retires its device-owned samples under callback exclusion.
void audio_device_preview_stop(AudioDevice*);
// Reads completion without taking a renderer or audio-device lock.
bool audio_device_preview_active(const AudioDevice*);
// Replaces one cleared callback buffer with exact captured samples and zero-pads the final block.
void audio_device_preview_render(AudioDevice*, float*, int frames, int channels);
