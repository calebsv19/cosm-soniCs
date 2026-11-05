#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "../src/app/control_bridge_internal.h"
#include "core_time.h"
#include "test_temp_path.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
static atomic_bool sending;
static unsigned dispatches;
static bool pressure;
// Supplies a bounded terminal response after the actual control-owner dispatch boundary.
static char* dispatch(DawControlBridge* b,AppState* s,const char* text,bool* changed){
    (void)b;(void)s;assert(!strcmp(text,"{}"));*changed=false;++dispatches;
    if(!pressure)return strdup("{\"ok\":true}");
    size_t n=8u*1024u*1024u;char* p=malloc(n+1);assert(p);memset(p,'x',n);p[n]=0;return p;
}
// Observes entry to actual socket output without replacing its pressure or timeout semantics.
static ssize_t observed_send(int fd,const void* p,size_t n,int flags){atomic_store(&sending,true);return send(fd,p,n,flags);}
#define daw_control_dispatch dispatch
#define send observed_send
#include "../src/app/control_bridge.c"
#undef send
#undef daw_control_dispatch
// Waits for a real transport-published request before dispatch or shutdown.
static void pending(DawControlBridge* b){uint64_t end=core_time_now_ns()+UINT64_C(10000000000);for(;;){pthread_mutex_lock(&b->mutex);bool ready=b->pending;pthread_mutex_unlock(&b->mutex);if(ready)return;assert(core_time_now_ns()<end);SDL_Delay(1);}}
// Connects a private local client and submits one complete frame.
static int connect_client(const char* path){int fd=socket(AF_UNIX,SOCK_STREAM,0);assert(fd>=0);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,path);assert(!connect(fd,(struct sockaddr*)&a,sizeof(a)));struct timeval tv={.tv_sec=6};assert(!setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)));assert(send(fd,"{}\n",3,0)==3);return fd;}
// Proves unexecuted pending shutdown, executed reply pressure and ordinary endpoint reuse.
int main(void){
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);AppState* s=calloc(1,sizeof(*s));assert(s);config_set_defaults(&s->runtime_cfg);s->engine=engine_create(&s->runtime_cfg);assert(s->engine);
    DAW_TEST_TEMP_PATH(root,"bridge-lifetime-XXXXXX");assert(mkdtemp(root));char path[104];assert(snprintf(path,sizeof(path),"%s/control.sock",root)<(int)sizeof(path));
    for(int mode=0;mode<3;++mode){
        DawControlBridge* b=daw_control_bridge_start(path,true);assert(b);int fd=connect_client(path);pending(b);unsigned before=dispatches;
        if(mode){pressure=mode==1;atomic_store(&sending,false);assert(!daw_control_bridge_poll(b,s));assert(dispatches==before+1);
            uint64_t end=core_time_now_ns()+UINT64_C(10000000000);while(!atomic_load(&sending)){assert(core_time_now_ns()<end);SDL_Delay(1);}
            if(mode==2){char answer[32]={0};assert(recv(fd,answer,sizeof(answer)-1,0)>0);assert(strstr(answer,"true"));}
        }
        uint64_t begin=core_time_now_ns();daw_control_bridge_stop(b);uint64_t elapsed=core_time_now_ns()-begin;
        assert(elapsed<UINT64_C(6000000000));if(!mode)assert(dispatches==before);assert(access(path,F_OK));close(fd);
        printf("control bridge mode=%d shutdown_ns=%llu\n",mode,(unsigned long long)elapsed);
    }
    engine_destroy(s->engine);free(s);assert(!rmdir(root));SDL_Quit();puts("control_bridge_lifetime_test: passed (pending request, slow reply reader, joined transport, endpoint reuse)");return 0;
}
