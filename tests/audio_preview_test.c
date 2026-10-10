#include "audio/audio_device.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int locks, callback_locks;
static bool fail_allocation, inside_callback;
// Enforces callback exclusion while keeping the test independent of physical devices.
static void fake_lock(SDL_AudioDeviceID id) { assert(id==42 && !inside_callback); ++locks; }
// Requires matching device exclusion on every control-path publication.
static void fake_unlock(SDL_AudioDeviceID id) { assert(id==42 && locks==1); --locks; }
// Injects allocation refusal before any accepted playback state changes.
static void* allocate(size_t bytes) { return fail_allocation?NULL:malloc(bytes); }
#define SDL_LockAudioDevice fake_lock
#define SDL_UnlockAudioDevice fake_unlock
#define malloc allocate
#include "../src/audio/device_preview.c"
#undef malloc
// Verifies exact immutable PCM, final zero padding, refusal preservation and finite replay.
int main(void) {
    AudioDevice device={.device_id=42,.is_open=true,.spec={48000,128,2}};
    float input[]={.1f,-.2f,.3f,-.4f,.5f,-.6f}, output[8];
    assert(audio_device_preview_play(&device,input,3,2,48000) && audio_device_preview_active(&device));
    input[0]=.9f; assert(device.preview_pcm[0]==.1f);
    float* accepted=device.preview_pcm;
    fail_allocation=true; assert(!audio_device_preview_play(&device,input,3,2,48000)); fail_allocation=false;
    assert(device.preview_pcm==accepted && device.preview_cursor==0 && audio_device_preview_active(&device));
    float invalid[]={NAN,0}; assert(!audio_device_preview_play(&device,invalid,1,2,48000)); invalid[0]=1.01f; assert(!audio_device_preview_play(&device,invalid,1,2,48000));
    assert(!audio_device_preview_play(&device,input,3,1,48000) && !audio_device_preview_play(&device,input,3,2,44100));
    assert(!audio_device_preview_play(&device,input,UINT64_MAX,2,48000));
    inside_callback=true; audio_device_preview_render(&device,output,2,2); inside_callback=false;
    assert(output[0]==.1f && output[1]==-.2f && output[2]==.3f && output[3]==-.4f && audio_device_preview_active(&device));
    memset(output,0x7f,sizeof(output)); inside_callback=true; audio_device_preview_render(&device,output,4,2); inside_callback=false;
    assert(output[0]==.5f && output[1]==-.6f && !audio_device_preview_active(&device));
    for(int i=2;i<8;++i)assert(output[i]==0);
    output[0]=.77f; audio_device_preview_render(&device,output,4,2); assert(output[0]==.77f);
    assert(audio_device_preview_play(&device,input,3,2,48000) && device.preview_cursor==0);
    audio_device_preview_stop(&device); assert(!device.preview_pcm && !audio_device_preview_active(&device) && !locks && !callback_locks);
    audio_device_preview_stop(&device); puts("audio_preview_test: success (exact copied PCM, finite boundaries, no callback locks, failure preservation, replay and retirement)"); return 0;
}
