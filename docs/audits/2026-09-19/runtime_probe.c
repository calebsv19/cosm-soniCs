#include "engine/engine_internal.h"
#include "audio/wav_writer.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
extern int limiter_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
/* Reproduces bounded engine defects without opening an audio device. */
int main(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg); cfg.sample_rate=48000; cfg.block_size=128;
    Engine* e=engine_create(&cfg); assert(e);
    EngineCommand cmd={.type=ENGINE_CMD_SEEK,.payload.seek.frame=12345};
    size_t accepted=0;
    while(engine_post_command(e,&cmd)) ++accepted;
    size_t bytes=ringbuf_available_read(&e->command_queue);
    printf("COMMAND: size=%zu capacity=%zu accepted=%zu queued=%zu partial=%zu\n",sizeof(cmd),e->command_queue.capacity,accepted,bytes,bytes-accepted*sizeof(cmd));
    EngineCommand popped; assert(ringbuf_read(&e->command_queue,&popped,sizeof(popped))==sizeof(popped));
    cmd.payload.seek.frame=98765; assert(engine_post_command(e,&cmd)); engine_process_commands(e);
    printf("COMMAND AFTER RETRY: seek=%llu expected=98765 leftover=%zu\n",(unsigned long long)e->transport_frame,ringbuf_available_read(&e->command_queue));
    float fixture[2048]; for(int i=0;i<2048;++i)fixture[i]=0.25f;
    assert(wav_write_f32("tmp/runtime_audit_20260919/constant.wav",fixture,1024,2,48000));
    int clip=-1; assert(engine_add_clip_to_track(e,0,"tmp/runtime_audit_20260919/constant.wav",0,&clip));
    float out[256],track[256];
    engine_mix_tracks(e,0,128,out,track,2); printf("GAIN: normal=%g ",out[0]);
    assert(engine_track_set_gain(e,0,0)); engine_mix_tracks(e,0,128,out,track,2); printf("track_zero=%g ",out[0]);
    assert(engine_track_set_gain(e,0,1)); assert(engine_clip_set_gain(e,0,clip,0));
    engine_mix_tracks(e,0,128,out,track,2); printf("clip_zero=%g expected=0\n",out[0]);
    assert(engine_clip_set_gain(e,0,clip,1));
    assert(engine_clip_set_fades(e,0,clip,128,0));
    assert(engine_clip_set_fade_curves(e,0,clip,ENGINE_FADE_CURVE_LINEAR,ENGINE_FADE_CURVE_LINEAR));
    engine_mix_tracks(e,0,128,out,track,2); float linear=out[64];
    assert(engine_clip_set_fade_curves(e,0,clip,ENGINE_FADE_CURVE_EXPONENTIAL,ENGINE_FADE_CURVE_LINEAR));
    engine_mix_tracks(e,0,128,out,track,2);
    printf("FADE: linear_quarter=%g exponential_quarter=%g expected_exponential=%g\n",linear,out[64],0.25*pow(0.25,2.2));
    atomic_store(&e->spectrum_enabled,true);
    for(int b=0;b<8;++b){for(int i=0;i<128;++i)out[2*i]=out[2*i+1]=(float)(b*128+i);engine_spectrum_begin_block(e);engine_spectrum_update(e,out,128,2);}
    float mono[256]; assert(ringbuf_read(&e->spectrum_queue,mono,sizeof(mono))==sizeof(mono));
    printf("SPECTRUM: window indices 0=%g 127=%g 128=%g 255=%g (gap at window index 128)\n",mono[0],mono[127],mono[128],mono[255]);
    FxHandle* h=NULL; FxVTable vt={0}; FxDesc desc={0}; assert(limiter_create(&desc,&h,&vt,48000,512,2));
    vt.set_param(h,1,5.0f); float impulse[1024]={0}; impulse[0]=impulse[1]=0.1f;
    vt.process(h,impulse,impulse,512,2); int first=-1; for(int i=0;i<512;++i)if(fabsf(impulse[2*i])>0.0001f){first=i;break;}
    printf("LIMITER: lookahead_ms=5 expected_delay_near=240 measured_first_impulse_frame=%d\n",first);
    vt.destroy(h); engine_destroy(e); return 0;
}
