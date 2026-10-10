#include "audio/audio_device.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define PREVIEW_MAX_BYTES (64u * 1024u * 1024u)
// Validates and prepares immutable playback before excluding the callback for pointer replacement.
bool audio_device_preview_play(AudioDevice* device, const float* pcm, uint64_t frames, int channels, int rate) {
    if (!device || !device->is_open || !pcm || !frames || channels!=device->spec.channels || rate!=device->spec.sample_rate ||
        channels<1 || frames>PREVIEW_MAX_BYTES/sizeof(float)/(unsigned)channels) return false;
    size_t samples=(size_t)frames*channels, bytes=samples*sizeof(float);
    for(size_t i=0;i<samples;++i) if(!isfinite(pcm[i]) || fabsf(pcm[i])>1) return false;
    float* copy=malloc(bytes); if(!copy)return false; memcpy(copy,pcm,bytes);
    SDL_LockAudioDevice(device->device_id);
    float* previous=device->preview_pcm; device->preview_pcm=copy; device->preview_frames=frames; device->preview_cursor=0;
    atomic_store_explicit(&device->preview_active,true,memory_order_release);
    SDL_UnlockAudioDevice(device->device_id); free(previous); return true;
}
// Retires samples only while the device callback cannot retain or consume them.
void audio_device_preview_stop(AudioDevice* device) {
    if(!device)return;
    if(!device->preview_pcm){atomic_store(&device->preview_active,false);return;}
    if(device->is_open)SDL_LockAudioDevice(device->device_id);
    atomic_store(&device->preview_active,false); float* previous=device->preview_pcm; device->preview_pcm=NULL;
    device->preview_frames=device->preview_cursor=0;
    if(device->is_open)SDL_UnlockAudioDevice(device->device_id);
    free(previous);
}
// Exposes an atomic finite-playback status for native control feedback.
bool audio_device_preview_active(const AudioDevice* device) {
    return device && atomic_load_explicit(&device->preview_active,memory_order_acquire);
}
// Runs only inside the device callback and never allocates, frees, locks or applies processors.
void audio_device_preview_render(AudioDevice* device,float* output,int frames,int channels) {
    if(!device || !output || frames<=0 || channels!=device->spec.channels || !audio_device_preview_active(device))return;
    uint64_t available=device->preview_frames-device->preview_cursor;
    size_t count=available<(uint64_t)frames?(size_t)available:(size_t)frames;
    memset(output,0,(size_t)frames*channels*sizeof(float));
    memcpy(output,device->preview_pcm+(size_t)device->preview_cursor*channels,count*channels*sizeof(float));
    device->preview_cursor+=count;
    if(device->preview_cursor==device->preview_frames)atomic_store_explicit(&device->preview_active,false,memory_order_release);
}
