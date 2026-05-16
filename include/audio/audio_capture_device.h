#pragma once

#include "audio/audio_device.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

#define AUDIO_CAPTURE_DEVICE_NAME_MAX 128
#define AUDIO_CAPTURE_DEVICE_ERROR_MAX 256

typedef void (*AudioCaptureCallback)(const float* input, int frames, int channels, void* userdata);

typedef struct {
    SDL_AudioDeviceID device_id;
    AudioDeviceSpec spec;
    AudioCaptureCallback callback;
    void* userdata;
    bool is_open;
    bool is_started;
    char name[AUDIO_CAPTURE_DEVICE_NAME_MAX];
    char last_error[AUDIO_CAPTURE_DEVICE_ERROR_MAX];
} AudioCaptureDevice;

AudioDeviceSpec audio_capture_device_default_spec(void);
void audio_capture_device_init(AudioCaptureDevice* device);
const char* audio_capture_device_last_error(const AudioCaptureDevice* device);
int audio_capture_device_count(void);
const char* audio_capture_device_name(int index);
bool audio_capture_device_open(AudioCaptureDevice* device,
                               const char* device_name,
                               const AudioDeviceSpec* desired,
                               AudioCaptureCallback cb,
                               void* userdata);
bool audio_capture_device_start(AudioCaptureDevice* device);
void audio_capture_device_stop(AudioCaptureDevice* device);
void audio_capture_device_close(AudioCaptureDevice* device);
