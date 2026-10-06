#define _POSIX_C_SOURCE 200809L
#include "audio/media_clip.h"
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool slow_child;
static pid_t child_pid;
static char output_path[512];
// Substitutes a sleeping child only for the cancellation lifecycle test; normal decode uses real FFmpeg.
static int test_spawn(pid_t* pid,const char* path,const posix_spawn_file_actions_t* actions,
                      const posix_spawnattr_t* attributes,char* const argv[],char* const env[]) {
    if (!slow_child) return path[0]=='/' ? posix_spawn(pid,path,actions,attributes,argv,env) : posix_spawnp(pid,path,actions,attributes,argv,env);
    int n=0;while(argv[n])++n;snprintf(output_path,sizeof(output_path),"%s",argv[n-1]);
    char* const sleep_args[]={"/bin/sleep","30",NULL};
    int rc=posix_spawn(pid,"/bin/sleep",NULL,NULL,sleep_args,env);if(!rc)child_pid=*pid;return rc;
}
#define posix_spawn test_spawn
#define posix_spawnp test_spawn
#undef __APPLE__
#include "../src/audio/media_clip.c"
#undef posix_spawn
#undef posix_spawnp
// Cancels only after the decoder child has started so kill, reap and temporary-file cleanup are exercised.
static bool child_started(void* unused) {(void)unused;return child_pid>0;}
// Exercises the production FFmpeg fallback and its exact owned-child cancellation path.
int main(int argc,char** argv) {
    assert(argc==2);AudioMediaClip clip={0};AudioMediaLoadControl control={.max_sample_bytes=8*1024*1024};
    assert(audio_media_clip_load_controlled(argv[1],48000,&clip,&control));assert(clip.frame_count>=48000);audio_media_clip_free(&clip);
    slow_child=true;control.cancelled=child_started;
    assert(!audio_media_clip_load_controlled(argv[1],48000,&clip,&control));assert(!clip.samples && child_pid>0);
    int status=0;assert(waitpid(child_pid,&status,WNOHANG)==-1);assert(access(output_path,F_OK)!=0);
    puts("media_ffmpeg_test: decode, cancellation, child reap and temporary cleanup passed");return 0;
}
