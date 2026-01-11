#ifndef VK_RENDERER_CONTEXT_H
#define VK_RENDERER_CONTEXT_H

#include <vulkan/vulkan.h>
#include <SDL2/SDL_vulkan.h>

#include "vk_renderer_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VkRendererSwapchain {
    VkSwapchainKHR handle;
    VkFormat image_format;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage* images;
    VkImageView* image_views;
} VkRendererSwapchain;

typedef struct VkRendererContext {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR surface;

    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceMemoryProperties memory_properties;

    VkDevice device;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;
    VkQueue graphics_queue;
    VkQueue present_queue;

    VkRendererSwapchain swapchain;
} VkRendererContext;

VkResult vk_renderer_context_create(VkRendererContext* ctx,
                                    SDL_Window* window,
                                    const VkRendererConfig* config);
void vk_renderer_context_destroy(VkRendererContext* ctx);
VkResult vk_renderer_context_recreate_swapchain(VkRendererContext* ctx,
                                                SDL_Window* window,
                                                const VkRendererConfig* config);

#ifdef __cplusplus
}
#endif

#endif // VK_RENDERER_CONTEXT_H
