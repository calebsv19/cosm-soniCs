#include "audio/audio_capture_device.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void capture_noop(const float* input, int frames, int channels, void* userdata) {
    (void)input;
    (void)frames;
    (void)channels;
    (void)userdata;
}

static void test_default_spec_and_init(void) {
    AudioDeviceSpec spec = audio_capture_device_default_spec();
    assert(spec.sample_rate == 48000);
    assert(spec.block_size == 512);
    assert(spec.channels == 1);

    AudioCaptureDevice device;
    memset(&device, 0x7f, sizeof(device));
    audio_capture_device_init(&device);
    assert(device.device_id == 0);
    assert(!device.is_open);
    assert(!device.is_started);
    assert(audio_capture_device_last_error(&device)[0] == '\0');
}

static void test_closed_start_fails_with_error(void) {
    AudioCaptureDevice device;
    audio_capture_device_init(&device);
    assert(!audio_capture_device_start(&device));
    assert(strstr(audio_capture_device_last_error(&device), "not open") != NULL);
    audio_capture_device_close(&device);
    assert(!device.is_open);
}

static void test_open_rejects_missing_callback_without_hardware(void) {
    AudioCaptureDevice device;
    audio_capture_device_init(&device);
    assert(!audio_capture_device_open(&device, NULL, NULL, NULL, NULL));
    assert(!device.is_open);

    (void)capture_noop;
}

int main(void) {
    test_default_spec_and_init();
    test_closed_start_fails_with_error();
    test_open_rejects_missing_callback_without_hardware();
    puts("audio_capture_device_test: success");
    return 0;
}
