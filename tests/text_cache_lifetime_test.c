#include "ui/text_draw.h"
#include "kit_render_external_text.h"
#include "vk_renderer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Models descriptor reuse without submitting invalid work to the GPU used by the active app.
typedef struct Resource { bool alive; unsigned version; VkRenderer* owner; } Resource;
// Copies a recorded binding so later cache eviction cannot alter the expected resource generation.
typedef struct Binding { unsigned id,version; } Binding;
static Resource resources[4096];static Binding bindings[4096];static unsigned pending[4096];
static int bound_count,pending_count,uploads,cross_owner,last_width,last_height;
// Finds a reusable descriptor slot just as a freed descriptor pool allocation may be reused.
VkResult vk_renderer_upload_sdl_surface_with_filter(VkRenderer* renderer,SDL_Surface* surface,VkRendererTexture* texture,VkFilter filter){
    (void)filter;unsigned id=1;while(id<4096 && resources[id].alive)++id;assert(id<4096);
    resources[id].alive=true;resources[id].owner=renderer;++resources[id].version;++uploads;
    memset(texture,0,sizeof(*texture));texture->descriptor_set=(VkDescriptorSet)(uintptr_t)id;texture->width=surface->w;texture->height=surface->h;return VK_SUCCESS;
}
// Frees immediately, allowing an already recorded binding to accidentally reference another label.
void vk_renderer_texture_destroy(VkRenderer* renderer,VkRendererTexture* texture){(void)renderer;unsigned id=(unsigned)(uintptr_t)texture->descriptor_set;if(id)resources[id].alive=false;memset(texture,0,sizeof(*texture));}
// Retains a texture until the modeled submitted frame completes, matching the renderer retirement API.
void vk_renderer_queue_texture_destroy(VkRenderer* renderer,VkRendererTexture* texture){
    if(renderer->current_frame_index==UINT32_MAX){vk_renderer_texture_destroy(renderer,texture);return;}
    unsigned id=(unsigned)(uintptr_t)texture->descriptor_set;if(id){assert(pending_count<4096);pending[pending_count++]=id;}memset(texture,0,sizeof(*texture));
}
// Records descriptor ownership/generation and logical/pixel dimensions without drawing the active UI.
void vk_renderer_draw_texture(VkRenderer* renderer,const VkRendererTexture* texture,const SDL_Rect* src,const SDL_Rect* dst){
    (void)src;unsigned id=(unsigned)(uintptr_t)texture->descriptor_set;assert(id && resources[id].alive && bound_count<4096);
    if(resources[id].owner!=renderer)++cross_owner;bindings[bound_count++]=(Binding){id,resources[id].version};last_width=texture->width;last_height=dst->h;
}
// Settles earlier modeled GPU bindings before out-of-frame invalidation frees resources.
void vk_renderer_wait_idle(VkRenderer* renderer){(void)renderer;bound_count=0;for(int i=0;i<pending_count;++i)resources[pending[i]].alive=false;pending_count=0;}
// Keeps shared registrations inert so this harness exclusively tests the app's production clipped path.
void kit_render_external_text_register_font_source(TTF_Font* f,const char* p,int l,int r,int k){(void)f;(void)p;(void)l;(void)r;(void)k;}
// Avoids mutating a second shared font/cache implementation during the isolated lifetime model.
void kit_render_external_text_unregister_font_source(TTF_Font* f){(void)f;}
// Provides the external reset seam without touching another renderer.
void kit_render_external_text_reset_renderer(SDL_Renderer* r){(void)r;}
// Uses real TTF metrics for the modeled clipped text rasterization.
int kit_render_external_text_measure_utf8(SDL_Renderer* r,TTF_Font* f,const char* t,int* w,int* h){(void)r;return TTF_SizeUTF8(f,t,w,h)==0;}
// Rejects unintended use of the shared unclipped route in this app-local regression.
int kit_render_external_text_draw_utf8_at(SDL_Renderer* r,TTF_Font* f,const char* t,int x,int y,SDL_Color c){(void)r;(void)f;(void)t;(void)x;(void)y;(void)c;assert(false);return 0;}
// Starts one independent frame with a known raster density.
static void begin(VkRenderer* r,int density){r->current_frame_index=0;r->draw_state.logical_size[0]=1600;r->draw_state.logical_size[1]=1000;r->context.swapchain.extent.width=1600*density;r->context.swapchain.extent.height=1000*density;bound_count=0;}
// Checks the recorded frame still owns every label it submitted before safely retiring textures.
static int finish(VkRenderer* r){int stale=0;for(int i=0;i<bound_count;++i){Resource* p=&resources[bindings[i].id];if(!p->alive || p->version!=bindings[i].version)++stale;}for(int i=0;i<pending_count;++i)resources[pending[i]].alive=false;pending_count=0;bound_count=0;r->current_frame_index=UINT32_MAX;return stale;}
// Draws a label through real TTF and the unmodified production clipped-cache entry point.
static void label(VkRenderer* r,TTF_Font* f,const char* t){assert(daw_text_draw_utf8_clipped((SDL_Renderer*)r,f,t,0,0,(SDL_Color){230,230,230,255},400));}
// Reproduces dense eviction safely and qualifies font reuse, renderer ownership and Retina density changes.
int main(int argc,char** argv){bool repro=argc==2 && !strcmp(argv[1],"--reproduce");assert(SDL_Init(0)==0 && TTF_Init()==0);
    const char* path="include/fonts/Montserrat/Montserrat-Regular.ttf";TTF_Font* font=TTF_OpenFont(path,9);assert(font);daw_text_register_font_source(font,path,9,9,1);
    VkRenderer* a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));assert(a && b);a->current_frame_index=b->current_frame_index=UINT32_MAX;
    begin(a,1);const char* sentinels[]={"LOAD","SAVE","PLAY","STOP","W","H","Strings - Playful Theme","Woodwinds - Mischievous Reply"};
    for(int i=0;i<8;++i)label(a,font,sentinels[i]);for(int i=0;i<640;++i){char text[64];snprintf(text,sizeof(text),"Region %03d - distinct title",i);label(a,font,text);}
    int stale=finish(a);printf("dense recorded bindings corrupted before completion: %d / 648\n",stale);
    if(repro){assert(stale>0);daw_text_shutdown();TTF_CloseFont(font);free(a);free(b);TTF_Quit();SDL_Quit();return 0;}assert(!stale && !cross_owner);
    for(int frame=0;frame<12;++frame){begin(a,1);for(int i=0;i<640;++i){char text[64];snprintf(text,sizeof(text),"Playback %d region %03d",frame,i);label(a,font,text);}assert(!finish(a));}
    begin(a,1);label(a,font,"FONT IDENTITY");int old_height=last_height;finish(a);
    daw_text_unregister_font_source(font);assert(!TTF_SetFontSize(font,20));daw_text_register_font_source(font,path,20,20,1);
    begin(a,1);int old_uploads=uploads;label(a,font,"FONT IDENTITY");assert(uploads>old_uploads && last_height>old_height);int width=last_width;assert(!finish(a));
    begin(a,2);old_uploads=uploads;label(a,font,"FONT IDENTITY");assert(uploads>old_uploads && last_width>width*1.8);assert(!finish(a));
    begin(b,1);old_uploads=uploads;label(b,font,"FONT IDENTITY");assert(uploads>old_uploads && !cross_owner);assert(!finish(b));
    begin(a,1);label(a,font,"AFTER RESET");daw_text_invalidate_cache((SDL_Renderer*)a);label(a,font,"AFTER RESET");assert(!finish(a));
    daw_text_unregister_font_source(font);TTF_CloseFont(font);daw_text_shutdown();for(int i=1;i<4096;++i)assert(!resources[i].alive);free(a);free(b);TTF_Quit();SDL_Quit();
    puts("text_cache_lifetime_test: passed (dense playback eviction, exact resource lifetime, recycled font, renderer ownership, Retina density and in-frame invalidation)");return 0;
}
