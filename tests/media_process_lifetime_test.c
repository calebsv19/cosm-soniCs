#include "audio/media_clip.h"
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test_temp_path.h"
static int mode;
static pid_t children[8];static unsigned child_count;
// Runs real short-lived or held children at the production spawn boundary.
static int spawn_child(pid_t* pid,const char* path,const posix_spawn_file_actions_t* actions,const posix_spawnattr_t* attrs,char* const argv[],char* const env[]){
    if(mode==4)return ENOENT;
    if(mode==5)return posix_spawn(pid,path,actions,attrs,argv,env);
    if(mode==2){const char* output=NULL;for(int i=0;argv[i];++i)output=argv[i];FILE* f=fopen(output,"wb");assert(f);assert(!ftruncate(fileno(f),100000));assert(!fclose(f));}
    char* sleep_args[]={"sleep","20",NULL};char* true_args[]={"true",NULL};char* false_args[]={"false",NULL};
    int result=posix_spawn(pid,mode==1||mode==2?"/bin/sleep":mode==3?"/usr/bin/false":"/usr/bin/true",NULL,NULL,mode==1||mode==2?sleep_args:mode==3?false_args:true_args,env);
    if(!result){assert(child_count<8);children[child_count++]=*pid;}return result;
}
// Routes PATH fallback through the same observed child fixture.
static int spawn_path(pid_t* pid,const char* path,const posix_spawn_file_actions_t* a,const posix_spawnattr_t* b,char* const argv[],char* const env[]){if(mode==5)return posix_spawnp(pid,path,a,b,argv,env);return spawn_child(pid,path,a,b,argv,env);}
#define posix_spawn spawn_child
#define posix_spawnp spawn_path
#include "../src/audio/media_clip.c"
#undef posix_spawn
#undef posix_spawnp
// Requests cancellation only after a real child identity has been obtained.
static bool cancelled(void* unused){(void)unused;return mode==1&&child_count>0;}
// Verifies actual child reap, malformed success rejection and successful ordinary fallback reuse.
int main(int argc,char** argv){
    DAW_TEST_TEMP_PATH(output,"media-child-XXXXXX");int fd=mkstemp(output);assert(fd>=0);close(fd);
    AudioMediaLoadControl control={.cancelled=cancelled,.max_sample_bytes=1024};load_control=&control;
    for(mode=1;mode<=4;++mode){child_count=0;assert(!run_mp3_ffmpeg_decode_to_wav("fixture",48000,output));
        if(mode!=4)assert(child_count>0);for(unsigned i=0;i<child_count;++i){int status;errno=0;assert(waitpid(children[i],&status,WNOHANG)==-1&&errno==ECHILD);}
        assert(!truncate(output,0));
    }
    mode=0;child_count=0;AudioMediaClip clip={0};assert(!audio_media_clip_load_mp3_ffmpeg_fallback("fixture",48000,&clip));assert(!clip.samples);
    for(unsigned i=0;i<child_count;++i){int status;assert(waitpid(children[i],&status,WNOHANG)==-1&&errno==ECHILD);}
    load_control=NULL;
    if(argc==2){mode=5;assert(audio_media_clip_load_mp3_ffmpeg_fallback(argv[1],48000,&clip));assert(clip.frame_count>0&&clip.samples);audio_media_clip_free(&clip);puts("ordinary FFmpeg fallback decode passed");}
    assert(!unlink(output));puts("media_process_lifetime_test: passed (cancel, oversized output, exit/spawn failure, malformed success, children reaped)");return 0;
}
