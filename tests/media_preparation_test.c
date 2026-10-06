#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "audio/media_cache.h"
static int allocation_fail_after = -1;
// Injects deterministic cache allocation failures without affecting decoder allocation.
static void* cache_malloc(size_t n) {
    if (allocation_fail_after == 0) return NULL;
    if (allocation_fail_after > 0) --allocation_fail_after;
    return malloc(n);
}
#define malloc cache_malloc
#include "../src/audio/media_cache.c"
#undef malloc
// Writes a small mono PCM16 fixture with an exact known duration and samples.
static void fixture(const char* path) {
    unsigned char header[44] = {'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0};
    unsigned char pcm[4] = {0,64,0,192};
    FILE* f = fopen(path,"wb"); assert(f); assert(fwrite(header,1,44,f)==44);
    assert(fwrite(pcm,1,4,f)==4); assert(!fclose(f));
}
// Proves metadata validation, parity, prepared ownership, deduplication and retained-byte retirement.
int main(void) {
    char path[] = "/tmp/daw_prepare_XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd); fixture(path);
    AudioMediaInfo info={0}; assert(audio_media_probe(path,&info));
    assert(info.frame_count==2 && info.channels==1 && info.sample_rate==48000);
    AudioMediaClip prepared={0}; assert(audio_media_clip_load_wav(path,48000,&prepared));
    assert(prepared.samples[0]==.5f && prepared.samples[1]==-.5f);
    AudioMediaCache cache; audio_media_cache_init(&cache,false); AudioMediaClip* stored=NULL;
    for (int fail=0;fail<7;++fail) {
        allocation_fail_after=fail;
        assert(!audio_media_cache_adopt(&cache,"id",path,48000,&prepared,&stored));
        assert(prepared.samples && cache.count==0);
        audio_media_cache_shutdown(&cache); audio_media_cache_init(&cache,false);
    }
    allocation_fail_after=-1;
    assert(audio_media_cache_adopt(&cache,"id",path,48000,&prepared,&stored)); assert(!prepared.samples);
    AudioMediaClip duplicate={0}; assert(audio_media_clip_load_wav(path,48000,&duplicate));
    unlink(path); // Adoption and cache hits cannot reopen the source.
    AudioMediaClip* again=NULL;
    assert(audio_media_cache_adopt(&cache,"id",path,48000,&duplicate,&again));
    assert(again==stored && !duplicate.samples && cache.count==1 && cache.refcounts[0]==2);
    cache.refcounts[0]=INT_MAX;
    assert(!audio_media_cache_acquire(&cache,"id",path,48000,&again));
    assert(!audio_media_cache_retain(&cache,stored)); cache.refcounts[0]=2;
    AudioMediaClip private_clip={.samples=malloc(4),.frame_count=1,.sample_rate=48000,.channels=1};
    audio_media_cache_release(&cache,&private_clip); assert(private_clip.samples); audio_media_clip_free(&private_clip);
    assert(audio_media_cache_resident_bytes(&cache)==8);
    audio_media_cache_release(&cache,stored); assert(cache.count==1);
    audio_media_cache_release(&cache,stored); assert(cache.count==0 && audio_media_cache_resident_bytes(&cache)==0);
    audio_media_cache_shutdown(&cache);
    fixture(path); assert(truncate(path,46)==0); assert(!audio_media_probe(path,&info)); unlink(path);
    puts("media_preparation_test: success"); return 0;
}
