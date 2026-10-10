#include "engine/engine_internal.h"
// Starts exact captured playback only on the creating thread while authored transport is stopped.
bool engine_preview_play(Engine* engine,const float* pcm,uint64_t frames,int channels,int rate) {
    return engine_is_control_thread(engine) && !engine_transport_requested_playing(engine) && !engine_transport_is_playing(engine) &&
        audio_device_preview_play(&engine->device,pcm,frames,channels,rate);
}
// Cancels finite listening without changing project state, history or transport position.
void engine_preview_stop(Engine* engine) {
    if(engine_is_control_thread(engine))audio_device_preview_stop(&engine->device);
}
// Reads finite listening completion without entering the callback lock.
bool engine_preview_active(const Engine* engine) {
    return engine && audio_device_preview_active(&engine->device);
}
