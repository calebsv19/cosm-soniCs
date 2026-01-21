#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>
#include <stdint.h>

#include "vk_renderer_config.h"
#include "vk_renderer_context.h"
#include "vk_renderer_pipeline.h"
#include "vk_renderer_commands.h"
#include "vk_renderer_memory.h"
#include "vk_renderer_textures.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VkRendererFrameState {
    VkCommandBuffer command_buffer;
    VkFence in_flight_fence;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkAllocatedBuffer vertex_buffer;
    VkDeviceSize vertex_offset;
    VkRendererTexture* transient_textures;
    uint32_t transient_texture_count;
    uint32_t transient_texture_capacity;
} VkRendererFrameState;

typedef struct VkRendererDrawState {
    float current_color[4];
    float logical_size[2];
    uint32_t draw_call_count;
} VkRendererDrawState;

typedef struct VkRendererDebugCapture {
    VkAllocatedBuffer buffer;
    VkBool32 requested;
    VkBool32 dumped;
    uint32_t width;
    uint32_t height;
    uint32_t frame_counter;
    uint32_t frame_trigger;
} VkRendererDebugCapture;

typedef struct VkRenderer {
    VkRendererConfig config;
    VkRendererContext context;
    VkRendererCommandPool command_pool;
    VkRendererPipeline pipelines[VK_RENDERER_PIPELINE_COUNT];
    VkRenderPass render_pass;
    VkPipelineCache pipeline_cache;

    VkDescriptorSetLayout sampler_set_layout;
    VkDescriptorPool descriptor_pool;

    VkRendererFrameState* frames;
    uint32_t frame_count;
    uint32_t frame_index;

    VkFramebuffer* swapchain_framebuffers;
    uint32_t swapchain_framebuffer_count;
    uint32_t swapchain_image_index;
    uint32_t current_frame_index;

    VkRendererDrawState draw_state;
    VkRendererDebugCapture debug_capture;
} VkRenderer;

VkResult vk_renderer_init(VkRenderer* renderer,
                          SDL_Window* window,
                          const VkRendererConfig* config);
void vk_renderer_shutdown(VkRenderer* renderer);

VkResult vk_renderer_begin_frame(VkRenderer* renderer,
                                 VkCommandBuffer* out_cmd,
                                 VkFramebuffer* out_framebuffer,
                                 VkExtent2D* out_extent);
VkResult vk_renderer_end_frame(VkRenderer* renderer,
                               VkCommandBuffer cmd);
VkResult vk_renderer_recreate_swapchain(VkRenderer* renderer, SDL_Window* window);

void vk_renderer_set_draw_color(VkRenderer* renderer, float r, float g, float b, float a);
void vk_renderer_set_logical_size(VkRenderer* renderer, float width, float height);
void vk_renderer_draw_line(VkRenderer* renderer, float x0, float y0, float x1, float y1);
void vk_renderer_draw_rect(VkRenderer* renderer, const SDL_Rect* rect);
void vk_renderer_fill_rect(VkRenderer* renderer, const SDL_Rect* rect);
void vk_renderer_draw_texture(VkRenderer* renderer,
                              const VkRendererTexture* texture,
                              const SDL_Rect* src,
                              const SDL_Rect* dst);

VkResult vk_renderer_upload_sdl_surface(VkRenderer* renderer,
                                        SDL_Surface* surface,
                                        VkRendererTexture* out_texture);
VkResult vk_renderer_upload_sdl_surface_with_filter(VkRenderer* renderer,
                                                    SDL_Surface* surface,
                                                    VkRendererTexture* out_texture,
                                                    VkFilter filter);
void vk_renderer_queue_texture_destroy(VkRenderer* renderer,
                                       VkRendererTexture* texture);

#ifdef __cplusplus
}
#endif

#endif // VK_RENDERER_H
