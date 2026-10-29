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
    atomic_int monitor_mono; // Listening-only requested fold, separate from music and renders.
    double monitor_blend; // Callback-owned five-millisecond transition.
    float* preview_pair_b; // Owns the second immutable comparison buffer.
    double preview_gains[4]; // A/B matched followed by A/B original safety gains.
    float preview_weights[4], preview_from[4];
    unsigned preview_fade, preview_fade_length;
    int preview_target;
    atomic_int preview_selection;
    atomic_uint_fast64_t preview_pair_id, preview_position;
    float* preview_pcm; // Owns immutable finite playback samples until control-thread retirement.
    uint64_t preview_frames, preview_cursor;
    atomic_bool preview_active;

} AudioDevice;

bool audio_device_open(AudioDevice* device, const AudioDeviceSpec* desired, AudioDeviceCallback cb, void* userdata);
void audio_device_close(AudioDevice* device);
bool audio_device_start(AudioDevice* device);
// Applies a bounded smooth L/R average fold only at the physical listening callback boundary.
void audio_device_monitor_render(AudioDevice*,float*,int frames,int channels);
void audio_device_stop(AudioDevice* device);

// Copies bounded original-level PCM into the open endpoint without altering current playback on failure.
bool audio_device_preview_play(AudioDevice*, const float*, uint64_t frames, int channels, int rate);
// Prepares two bounded, aligned immutable buffers and validated attenuation-only listening gains.
bool audio_device_comparison_play(AudioDevice*,const float*,const float*,uint64_t,int,int,const double gains[4],uint64_t id,int selection);
// Selects one side/matching mode without restarting the callback cursor.
bool audio_device_comparison_select(AudioDevice*,uint64_t id,int selection);
// Stops only the addressed comparison; another preview owner remains intact.
bool audio_device_comparison_stop(AudioDevice*,uint64_t id);
// Renders convex crossfades on the existing shared cursor without callback allocation or locks.
void audio_device_comparison_render(AudioDevice*,float*,int,int);

// Stops finite playback and retires its device-owned samples under callback exclusion.
void audio_device_preview_stop(AudioDevice*);
// Reads completion without taking a renderer or audio-device lock.
bool audio_device_preview_active(const AudioDevice*);
// Replaces one cleared callback buffer with exact captured samples and zero-pads the final block.
void audio_device_preview_render(AudioDevice*, float*, int frames, int channels);
