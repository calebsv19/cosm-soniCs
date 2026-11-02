#include "ui/font.h"
#include "ui/font_bridge.h"
#include "ui/shared_theme_font_adapter.h"
#include "app/workspace_authoring/daw_workspace_authoring_host.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail_after=-1,opens;
/* Inject deterministic partial loader failures while retaining real TTF ownership. */
TTF_Font *TTF_OpenFont(const char *path,int size) {
    static TTF_Font *(*real_open)(const char*,int);
    if(!real_open){*(void**)(&real_open)=dlsym(RTLD_NEXT,"TTF_OpenFont");assert(real_open);}
    opens++;if(fail_after==0)return NULL;if(fail_after>0)--fail_after;
    return real_open(path,size);
}
/* Read the current preset through the production settings owner. */
static void preset(char *out) {assert(daw_shared_font_current_preset(out,64));}
/* Enter authoring through its production entry contract. */
static void enter(DawWorkspaceAuthoringHostState *host) {
    assert(daw_workspace_authoring_host_enter(host).code==CORE_OK);
    host->overlay_mode=DAW_WORKSPACE_AUTHORING_OVERLAY_FONT_THEME;
}
int main(int argc,char **argv) {
    assert(SDL_Init(0)==0 && TTF_Init()==0);char path[256],name[64];int size;
    SDL_setenv("DAW_USE_SHARED_FONT","1",1);SDL_setenv("DAW_FONT_PRESET","daw_default",1);SDL_setenv("DAW_FONT_ZOOM_STEP","0",1);
    if(argc==2 && !strcmp(argv[1],"--reopen")) {
        assert(daw_shared_font_load_persisted());assert(daw_shared_font_zoom_load_persisted());
        preset(name);assert(!strcmp(name,"ide") && daw_shared_font_zoom_step()==2);
        assert(daw_shared_font_resolve_ui_regular(path,sizeof(path),&size));assert(ui_font_set(path,size));
        ui_font_shutdown();TTF_Quit();SDL_Quit();puts("font settings separate-process reopen PASS");return 0;
    }
    assert(daw_shared_font_set_preset("daw_default"));assert(daw_shared_font_resolve_ui_regular(path,sizeof(path),&size));assert(ui_font_set(path,size));
    DawResolvedFont entry,a,b;assert(daw_font_bridge_acquire(1,&entry));assert(daw_font_bridge_acquire(.8f,&a));assert(daw_font_bridge_acquire(1.3f,&b));
    for(int i=0;i<3;i++) {
        fail_after=i;assert(!daw_shared_font_set_preset("ide"));preset(name);assert(!strcmp(name,"daw_default"));
        DawResolvedFont check;assert(daw_font_bridge_acquire(1,&check)&&check.font==entry.font);
    }
    fail_after=0;assert(!daw_shared_font_step_by(1));assert(daw_shared_font_zoom_step()==0);
    FILE *persisted=fopen("config/font_zoom_step.txt","wb");assert(persisted);assert(fputs("2\n",persisted)>=0 && !fclose(persisted));
    assert(!daw_shared_font_zoom_load_persisted() && daw_shared_font_zoom_step()==0);
    fail_after=-1;DawWorkspaceAuthoringHostState host;daw_workspace_authoring_host_reset(&host);enter(&host);
    fail_after=1;assert(daw_workspace_authoring_host_apply_font_theme_button(&host,KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_TEXT_SIZE_INC));assert(!host.font_theme_pending_changes && daw_shared_font_zoom_step()==0);
    fail_after=0;
    assert(daw_workspace_authoring_host_apply_font_theme_button(&host,KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_FONT_PRESET_IDE));
    assert(!host.font_theme_pending_changes && strstr(host.font_theme_status,"failed"));
    fail_after=-1;assert(daw_workspace_authoring_host_apply_font_theme_button(&host,KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_TEXT_SIZE_INC));assert(daw_shared_font_zoom_step()==1);
    for(int i=0;i<8;i++){assert(daw_shared_font_set_preset(i%2?"ide":"daw_default"));(void)daw_shared_font_set_zoom_step(i%4);}
    fail_after=0;int calls=opens;assert(daw_workspace_authoring_host_cancel(&host).code==CORE_OK);
    DawResolvedFont restored;assert(daw_font_bridge_acquire(1,&restored));assert(restored.font==entry.font && opens==calls && daw_shared_font_zoom_step()==0);preset(name);assert(!strcmp(name,"daw_default"));
    fail_after=-1;enter(&host);assert(daw_shared_font_set_preset("ide"));assert(daw_shared_font_set_zoom_step(2));
    assert(daw_workspace_authoring_host_apply(&host).code==CORE_OK);assert(daw_shared_font_save_persisted() && daw_shared_font_zoom_save_persisted());
    /* Qualify both presets and all supported zoom steps with retained scale users. */
    for(int f=0;f<2;f++)for(int z=-4;z<=5;z++) {
        assert(daw_shared_font_set_preset(f?"ide":"daw_default"));(void)daw_shared_font_set_zoom_step(z);
        for(int i=0;i<3;i++) {DawResolvedFont q;assert(daw_font_bridge_acquire(i==0?.8f:i==1?1:1.3f,&q));assert(q.font);}
    }
    assert(daw_shared_font_set_preset("ide"));(void)daw_shared_font_set_zoom_step(2);
    assert(daw_shared_font_save_persisted()&&daw_shared_font_zoom_save_persisted());
    /* Legacy-disabled shared font policy keeps its existing fixed fallback size. */
    SDL_setenv("DAW_USE_SHARED_FONT","0",1);
    assert(daw_shared_font_set_preset("daw_default"));assert(daw_shared_font_set_zoom_step(3));
    assert(daw_font_bridge_acquire(1,&restored)&&restored.logical_point_size==9);
    SDL_setenv("DAW_USE_SHARED_FONT","1",1);
    ui_font_shutdown();TTF_Quit();SDL_Quit();puts("font settings PASS: seven loader refusals, partial working-set rollback, exact no-open Cancel, Apply/save, 20 preset/zoom combinations");return 0;
}
