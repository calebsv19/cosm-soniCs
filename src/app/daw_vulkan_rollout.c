#include "app/daw_vulkan_rollout.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "vk_renderer.h"
#include "vk_runtime.h"

// rollout_capture_path resolves an optional proof output override.
static const char *rollout_capture_path(const char *variable,
                                        const char *fallback) {
    const char *value = getenv(variable);
    return value && value[0] ? value : fallback;
}

// rollout_minimum_scale parses the minimum accepted logical-to-drawable scale.
static double rollout_minimum_scale(void) {
    const char *value = getenv("DAW_VULKAN_ROLLOUT_MIN_SCALE");
    char *end = NULL;
    double parsed = value && value[0] ? strtod(value, &end) : 1.0;
    if (!isfinite(parsed) || parsed < 1.0 || parsed > 4.0 || !end || *end != '\0') {
        return 1.0;
    }
    return parsed;
}

// rollout_extent verifies swapchain pixels and the current high-DPI scale.
static int rollout_extent(SDL_Window *window,
                          const VkRenderer *renderer,
                          VkExtent2D *out_extent,
                          double *out_scale) {
    int logical_width = 0;
    int logical_height = 0;
    int drawable_width = 0;
    int drawable_height = 0;
    double scale_x;
    double scale_y;

    if (!window || !renderer || !out_extent || !out_scale) {
        return 0;
    }
    SDL_GetWindowSize(window, &logical_width, &logical_height);
    SDL_Vulkan_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (logical_width <= 0 || logical_height <= 0 ||
        drawable_width <= 0 || drawable_height <= 0) {
        return 0;
    }
    scale_x = (double)drawable_width / (double)logical_width;
    scale_y = (double)drawable_height / (double)logical_height;
    if (!isfinite(scale_x) || !isfinite(scale_y) ||
        fabs(scale_x - scale_y) > 0.01 || scale_x < rollout_minimum_scale()) {
        return 0;
    }
    *out_extent = renderer->context.swapchain.extent;
    *out_scale = scale_x;
    return out_extent->width == (uint32_t)drawable_width &&
           out_extent->height == (uint32_t)drawable_height;
}

// rollout_verify_runtime proves renderer handles are owned by embedded vk_runtime.
static int rollout_verify_runtime(const VkRenderer *renderer,
                                  const char *stage) {
    const VkRendererDevice *device;
    const VkRuntimeCapabilityReport *report;
    const char *version;

    if (!renderer || !renderer->context.device) {
        return 0;
    }
    device = renderer->context.device;
    report = vk_runtime_get_capability_report(&device->runtime);
    version = vk_runtime_version_string();
    if (!version || !version[0] || !report ||
        report->status != VK_RUNTIME_STATUS_OK || report->device_count == 0u ||
        report->selected_device_index >= report->device_count ||
        !report->validation_requested || !report->validation_available ||
        !report->validation_enabled || report->validation_load_failed ||
        report->validation_warning_count != 0u ||
        report->validation_error_count != 0u ||
        device->instance != device->runtime.instance ||
        device->device != device->runtime.device ||
        device->graphics_queue != device->runtime.graphics_queue ||
        device->present_queue != device->runtime.present_queue) {
        fprintf(stderr, "DAW_VULKAN_RUNTIME stage=%s status=fail\n", stage);
        return 0;
    }

    printf("DAW_VULKAN_RUNTIME schema=1 stage=%s runtime=%s device=%s "
           "validation_requested=1 validation_enabled=1 warnings=%u errors=%u\n",
           stage,
           version,
           report->devices[report->selected_device_index].device_name,
           (unsigned int)report->validation_warning_count,
           (unsigned int)report->validation_error_count);
    return 1;
}

// rollout_render submits deterministic DAW-colored primitives and an optional capture.
static int rollout_render(VkRenderer *renderer, const char *capture_path) {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkExtent2D extent = {0};
    SDL_Rect timeline;
    SDL_Rect clip;
    VkResult result;

    if (capture_path &&
        vk_renderer_request_capture(renderer, capture_path) != VK_SUCCESS) {
        fprintf(stderr, "DAW_VULKAN_ROLLOUT capture_request=fail\n");
        return 0;
    }
    result = vk_renderer_begin_frame(renderer, &command_buffer, &framebuffer, &extent);
    if (result != VK_SUCCESS || command_buffer == VK_NULL_HANDLE ||
        framebuffer == VK_NULL_HANDLE || extent.width == 0u || extent.height == 0u) {
        fprintf(stderr, "DAW_VULKAN_ROLLOUT begin_frame=%d\n", (int)result);
        return 0;
    }

    vk_renderer_set_logical_size(renderer, (float)extent.width, (float)extent.height);
    timeline = (SDL_Rect){
        (int)(extent.width / 12u),
        (int)(extent.height / 8u),
        (int)(extent.width * 3u / 4u),
        (int)(extent.height / 2u)
    };
    clip = (SDL_Rect){
        timeline.x + timeline.w / 8,
        timeline.y + timeline.h / 3,
        timeline.w / 3,
        timeline.h / 4
    };
    vk_renderer_set_draw_color(renderer, 0.08f, 0.10f, 0.15f, 1.0f);
    vk_renderer_fill_rect(renderer, &timeline);
    vk_renderer_set_draw_color(renderer, 0.20f, 0.55f, 0.82f, 1.0f);
    vk_renderer_fill_rect(renderer, &clip);
    vk_renderer_set_draw_color(renderer, 0.95f, 0.72f, 0.18f, 1.0f);
    vk_renderer_draw_line_thick(renderer,
                                (float)timeline.x,
                                (float)(timeline.y + timeline.h / 2),
                                (float)(timeline.x + timeline.w),
                                (float)(timeline.y + timeline.h / 2),
                                3.0f);
    if (renderer->draw_state.draw_call_count < 3u) {
        fprintf(stderr, "DAW_VULKAN_ROLLOUT draw_calls=%u\n",
                renderer->draw_state.draw_call_count);
        return 0;
    }
    result = vk_renderer_end_frame(renderer, command_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "DAW_VULKAN_ROLLOUT end_frame=%d\n", (int)result);
        return 0;
    }
    vk_renderer_wait_idle(renderer);
    return 1;
}

// daw_vulkan_rollout_self_test runs startup, resize, capture, and restart proof.
int daw_vulkan_rollout_self_test(void) {
    const char *initial_capture = rollout_capture_path(
        "DAW_VULKAN_ROLLOUT_INITIAL_CAPTURE", "daw-initial.bmp");
    const char *resized_capture = rollout_capture_path(
        "DAW_VULKAN_ROLLOUT_RESIZED_CAPTURE", "daw-resized.bmp");
    SDL_Window *window = NULL;
    VkRenderer renderer;
    VkRendererConfig config;
    VkExtent2D initial_extent = {0};
    VkExtent2D resized_extent = {0};
    double initial_scale = 0.0;
    double resized_scale = 0.0;
    int renderer_initialized = 0;
    int exit_code = 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "DAW_VULKAN_ROLLOUT sdl_init=fail error=%s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("soniCs Vulkan Rollout Proof",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              720,
                              450,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "DAW_VULKAN_ROLLOUT window=fail error=%s\n", SDL_GetError());
        goto cleanup;
    }

    vk_renderer_config_set_defaults(&config);
    config.enable_validation = VK_TRUE;
    if (vk_renderer_init(&renderer, window, &config) != VK_SUCCESS) {
        fprintf(stderr, "DAW_VULKAN_ROLLOUT renderer_init=fail\n");
        goto cleanup;
    }
    renderer_initialized = 1;
    if (!rollout_verify_runtime(&renderer, "startup") ||
        !rollout_extent(window, &renderer, &initial_extent, &initial_scale) ||
        !rollout_render(&renderer, initial_capture)) {
        goto cleanup;
    }

    SDL_SetWindowSize(window, 900, 560);
    SDL_Delay(100u);
    SDL_PumpEvents();
    if (vk_renderer_recreate_swapchain(&renderer, window) != VK_SUCCESS ||
        !rollout_extent(window, &renderer, &resized_extent, &resized_scale) ||
        (initial_extent.width == resized_extent.width &&
         initial_extent.height == resized_extent.height) ||
        !rollout_render(&renderer, resized_capture) ||
        !rollout_verify_runtime(&renderer, "resized")) {
        goto cleanup;
    }

    vk_renderer_shutdown(&renderer);
    renderer_initialized = 0;
    if (vk_renderer_init(&renderer, window, &config) != VK_SUCCESS) {
        fprintf(stderr, "DAW_VULKAN_ROLLOUT restart_init=fail\n");
        goto cleanup;
    }
    renderer_initialized = 1;
    if (!rollout_render(&renderer, NULL) ||
        !rollout_verify_runtime(&renderer, "restart")) {
        goto cleanup;
    }

    printf("DAW_VULKAN_ROLLOUT schema=1 status=pass "
           "initial=%ux%u resized=%ux%u initial_scale=%.3f resized_scale=%.3f "
           "initial_capture=%s resized_capture=%s\n",
           initial_extent.width,
           initial_extent.height,
           resized_extent.width,
           resized_extent.height,
           initial_scale,
           resized_scale,
           initial_capture,
           resized_capture);
    exit_code = 0;

cleanup:
    if (renderer_initialized) {
        vk_renderer_shutdown(&renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return exit_code;
}
