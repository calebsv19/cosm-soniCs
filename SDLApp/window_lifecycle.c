#include "window_lifecycle.h"
#include <stdio.h>
void daw_window_refresh(AppContext* ctx) {
    if(!ctx || !ctx->window)return;
    uint32_t changes=0;
    if(kit_ui_window_refresh_sdl(&ctx->window_state,ctx->window,&changes).code!=CORE_OK)return;
    if(changes&(KIT_UI_WINDOW_GEOMETRY|KIT_UI_WINDOW_MODE|KIT_UI_WINDOW_DISPLAY|KIT_UI_WINDOW_VISIBILITY)) {
        ctx->pending_swapchain_recreate=true;
        ctx->pending_swapchain_width=ctx->window_state.logical_width;
        ctx->pending_swapchain_height=ctx->window_state.logical_height;
    }
}
bool daw_window_event(AppContext* ctx,const SDL_Event* event) {
    if(!ctx || !ctx->window)return false;
    int mode=kit_ui_window_fullscreen_key_sdl(ctx->window,event);
    if(mode<0)SDL_Log("Fullscreen transition failed: %s",SDL_GetError());
    if(mode || event->type==SDL_WINDOWEVENT)daw_window_refresh(ctx);
    return mode!=0;
}
static int capture(void* user,const char* path) {
    AppContext* ctx=user;
    return ctx->renderer && vk_renderer_request_capture(ctx->renderer,path)==VK_SUCCESS;
}
void daw_window_probe_tick(AppContext* ctx) {
    if(!ctx || !ctx->window)return;
    int status=kit_ui_window_probe_tick_sdl(&ctx->window_probe,ctx->window,ctx->presented_frames,capture,ctx);
    if(status){ctx->window_proof_status=status;ctx->quit=true;}
}
