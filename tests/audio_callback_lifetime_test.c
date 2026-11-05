#define _POSIX_C_SOURCE 200809L
#include "engine/engine_internal.h"
#include "audio/audio_device.h"
#include "core_time.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static SDL_AudioCallback actual_callback;
static int callback_rate;
static atomic_uint_fast64_t calls, frames_seen, max_ns, over_budget, nonzero;
// Measures the complete application SDL trampoline, including preview and monitor adapters.
static void measured(void* user,Uint8* stream,int len){
    uint64_t start=core_time_now_ns();actual_callback(user,stream,len);uint64_t elapsed=core_time_now_ns()-start;
    atomic_fetch_add(&calls,1);uint64_t frames=(unsigned)len/(2*sizeof(float));atomic_fetch_add(&frames_seen,frames);
    if(elapsed>atomic_load(&max_ns))atomic_store(&max_ns,elapsed);
    if(elapsed>frames*UINT64_C(1000000000)/(unsigned)callback_rate)atomic_fetch_add(&over_budget,1);
    for(int i=0;i<len/(int)sizeof(float);++i)if(((float*)stream)[i]!=0)atomic_fetch_add(&nonzero,1);
}
// Keeps the actual SDL device and negotiated format while observing its full callback.
static SDL_AudioDeviceID observed_open(const char* name,int capture,const SDL_AudioSpec* want,SDL_AudioSpec* have,int flags){
    actual_callback=want->callback;SDL_AudioSpec wrapped=*want;wrapped.callback=measured;
    SDL_AudioDeviceID id=SDL_OpenAudioDevice(name,capture,&wrapped,have,flags);if(id)callback_rate=have->freq;return id;
}
#define SDL_OpenAudioDevice observed_open
#include "../src/audio/device_sdl.c"
#undef SDL_OpenAudioDevice
// Delivers only the real engine output-ring callback; physical output intentionally remains silent.
static void render(float* out,int frames,int channels,void* e){engine_audio_callback(out,frames,channels,e);}
// Proves exact wrap/shortage, output bounds and physical callback quiescence without user audio.
int main(int argc,char** argv){
    bool physical=argc==2&&!strcmp(argv[1],"--physical");if(!physical)SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER));EngineRuntimeConfig cfg;config_set_defaults(&cfg);cfg.sample_rate=48000;
    Engine* e=engine_create(&cfg);assert(e&&audio_queue_init(&e->output_queue,2,64));atomic_store(&e->clock_advancing,true);
    float input[84];for(int i=0;i<84;++i)input[i]=(float)i/128;
    struct {float before[8],out[128],after[8];} guarded;
    for(int i=0;i<8;++i)guarded.before[i]=guarded.after[i]=123;
    assert(audio_queue_write(&e->output_queue,input,42)==42);engine_audio_callback(guarded.out,31,2,e);assert(!memcmp(guarded.out,input,62*sizeof(float)));
    assert(audio_queue_write(&e->output_queue,input,40)==40);engine_audio_callback(guarded.out,64,2,e);
    assert(!memcmp(guarded.out,input+62,22*sizeof(float)));assert(!memcmp(guarded.out+22,input,80*sizeof(float)));
    for(int i=102;i<128;++i)assert(guarded.out[i]==0);
    for(int i=0;i<8;++i)assert(guarded.before[i]==123&&guarded.after[i]==123);
    assert(atomic_load(&e->diag_underrun_frames)==13 && atomic_load(&e->clock_consumed)==82);
    for(int pass=0;pass<3;++pass){
        atomic_store(&calls,0);atomic_store(&frames_seen,0);atomic_store(&max_ns,0);atomic_store(&over_budget,0);atomic_store(&nonzero,0);
        uint64_t old_consumed=atomic_load(&e->clock_consumed),old_missing=atomic_load(&e->diag_underrun_frames);
        AudioDevice device={0};AudioDeviceSpec spec={.sample_rate=48000,.channels=2,.block_size=pass==0?64:pass==1?256:1024};
        if(!audio_device_open(&device,&spec,render,e)){fprintf(stderr,"device route unavailable: %s\n",SDL_GetError());engine_destroy(e);SDL_Quit();return 2;}
        assert(audio_device_start(&device));uint64_t end=core_time_now_ns()+UINT64_C(1000000000);while(core_time_now_ns()<end)SDL_Delay(10);
        audio_device_stop(&device);uint64_t n=atomic_load(&calls);assert(n>0);assert(!atomic_load(&nonzero));
        assert(atomic_load(&e->clock_consumed)==old_consumed);assert(atomic_load(&e->diag_underrun_frames)-old_missing==atomic_load(&frames_seen));
        printf("route=%s requested=%d actual=%d rate=%d callbacks=%llu frames=%llu max_ns=%llu over_budget=%llu\n",SDL_GetCurrentAudioDriver(),spec.block_size,device.spec.block_size,device.spec.sample_rate,(unsigned long long)n,(unsigned long long)atomic_load(&frames_seen),(unsigned long long)atomic_load(&max_ns),(unsigned long long)atomic_load(&over_budget));
        assert(!atomic_load(&over_budget));audio_device_close(&device);SDL_Delay(30);assert(atomic_load(&calls)==n);
    }
    printf("audio_callback_lifetime_test: passed (%s, exact wrap/zero-fill/clock, deadlines, close quiescence)\n",physical?"physical silent output":"dummy output");
    engine_destroy(e);SDL_Quit();return 0;
}
