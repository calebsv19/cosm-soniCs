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

// Publishes an immutable comparison only on the owner with stopped authored transport.
bool engine_comparison_play(Engine* e,const EngineBounceBuffer* a,const EngineBounceBuffer* b,const double gains[4],uint64_t id,int selection) {
    return e && a && b && a->frame_count==b->frame_count && a->channels==b->channels && a->sample_rate==b->sample_rate && engine_is_control_thread(e) && !engine_transport_requested_playing(e) && !engine_transport_is_playing(e) && audio_device_comparison_play(&e->device,a->data,b->data,a->frame_count,a->channels,a->sample_rate,gains,id,selection);
}
// Selects only an owned active pair without changing authored transport or position.
bool engine_comparison_select(Engine* e,uint64_t id,int selection) {return engine_is_control_thread(e) && !engine_transport_requested_playing(e) && !engine_transport_is_playing(e) && audio_device_comparison_select(&e->device,id,selection);}
// Stops only an owned pair, including completed finite playback buffers.
bool engine_comparison_stop(Engine* e,uint64_t id){return engine_is_control_thread(e) && audio_device_comparison_stop(&e->device,id);}
// Exposes finite comparison completion while keeping callback state race-free.
bool engine_comparison_status(const Engine* e,uint64_t id,uint64_t* position){if(!e || !id || atomic_load(&e->device.preview_pair_id)!=id){if(position)*position=0;return false;}if(position)*position=atomic_load(&e->device.preview_position);return audio_device_preview_active(&e->device);}
