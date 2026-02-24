#include "sdl_app_framework.h"
#include "ui/font.h"
#include "core_time.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <stdio.h>

bool App_RenderOnce(AppContext* ctx, void (*handleRender)(AppContext* ctx));

// app_build_renderer_config returns the default renderer configuration for the app.
static VkRendererConfig app_build_renderer_config(void) {
    VkRendererConfig cfg;
    vk_renderer_config_set_defaults(&cfg);
    cfg.enable_validation = SDL_FALSE;
#ifdef VULKAN_RENDER_DEBUG
    cfg.enable_validation = SDL_TRUE;
#endif
    cfg.clear_color[0] = 18.0f / 255.0f;
    cfg.clear_color[1] = 18.0f / 255.0f;
    cfg.clear_color[2] = 22.0f / 255.0f;
    cfg.clear_color[3] = 1.0f;
    return cfg;
}

// app_init_renderer initializes the Vulkan renderer for the provided AppContext.
static bool app_init_renderer(AppContext* ctx) {
    if (!ctx || !ctx->window) {
        return false;
    }
    VkRendererConfig cfg = app_build_renderer_config();
    VkResult init = vk_renderer_init(&ctx->renderer_storage, ctx->window, &cfg);
    if (init != VK_SUCCESS) {
        SDL_Log("Failed to initialize Vulkan renderer (code %d)", init);
        return false;
    }
    ctx->renderer = &ctx->renderer_storage;
    return true;
}

// app_recover_device_lost rebuilds the Vulkan renderer when a device loss is reported.
static bool app_recover_device_lost(AppContext* ctx, int width, int height) {
    if (!ctx) {
        return false;
    }
    if (ctx->renderer) {
        ui_font_invalidate_cache(ctx->renderer);
        vk_renderer_shutdown(ctx->renderer);
    }
    if (!app_init_renderer(ctx)) {
        return false;
    }
    if (width > 0 && height > 0) {
        vk_renderer_set_logical_size(ctx->renderer, (float)width, (float)height);
    }
    return true;
}

// app_try_recreate_swapchain attempts a safe swapchain rebuild when the window has a valid size.
static bool app_try_recreate_swapchain(AppContext* ctx, int width, int height) {
    if (!ctx || !ctx->renderer || !ctx->window) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }
    VkResult result = vk_renderer_recreate_swapchain(ctx->renderer, ctx->window);
    if (result == VK_ERROR_DEVICE_LOST) {
        SDL_Log("Vulkan device lost during swapchain recreation; rebuilding renderer.");
        return app_recover_device_lost(ctx, width, height);
    }
    if (result != VK_SUCCESS) {
        return false;
    }
    vk_renderer_set_logical_size(ctx->renderer, (float)width, (float)height);
    return true;
}

bool App_Init(AppContext* ctx, const char* title, int width, int height, bool vsync) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                         SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI;
    ctx->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
					width, height, windowFlags);
    if (!ctx->window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    (void)vsync;
    if (!app_init_renderer(ctx)) {
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return false;
    }

    ctx->deltaTime = 0.0f;
    ctx->quit = false;
    ctx->userData = NULL;
    ctx->has_event = false;
    ctx->pending_swapchain_recreate = false;
    ctx->pending_swapchain_width = 0;
    ctx->pending_swapchain_height = 0;
    SDL_zero(ctx->current_event);
    ctx->renderMode = RENDER_ALWAYS;
    ctx->renderThreshold = 0.033f;     // 30 FPS by default
    ctx->timeSinceLastRender = 0.0f;

    return true;
}

void App_Run(AppContext* ctx, AppCallbacks* callbacks) {
    uint64_t last_time_ns = core_time_now_ns();
    SDL_Event event;

    while (!ctx->quit) {
        // Input Handling
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                ctx->quit = true;
            }
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    ctx->pending_swapchain_recreate = true;
                    ctx->pending_swapchain_width = event.window.data1;
                    ctx->pending_swapchain_height = event.window.data2;
                }
            }
            if (callbacks && callbacks->handleInput) {
                ctx->current_event = event;
                ctx->has_event = true;
                callbacks->handleInput(ctx);
                ctx->has_event = false;
            }
        }

        if (ctx->pending_swapchain_recreate) {
            if (app_try_recreate_swapchain(ctx,
                                           ctx->pending_swapchain_width,
                                           ctx->pending_swapchain_height)) {
                ctx->pending_swapchain_recreate = false;
            }
        }

        // Delta Time Calculation
        uint64_t now_ns = core_time_now_ns();
        ctx->deltaTime = (float)core_time_ns_to_seconds(core_time_diff_ns(now_ns, last_time_ns));
        last_time_ns = now_ns;
	
	ctx->timeSinceLastRender += ctx->deltaTime;
	
	// Update logic
	if (callbacks && callbacks->handleUpdate) {
	    callbacks->handleUpdate(ctx);
	}
	
	// Render logic
	bool shouldRender = false;
	
	if (ctx->renderMode == RENDER_ALWAYS) {
	    shouldRender = true;
	} else if (ctx->renderMode == RENDER_THROTTLED) {
	    if (ctx->timeSinceLastRender >= ctx->renderThreshold) {
	        shouldRender = true;
	    }
	}
	
	if (shouldRender && callbacks && callbacks->handleRender) {
	    App_RenderOnce(ctx, callbacks->handleRender);
	    ctx->timeSinceLastRender = 0.0f;
	}
        // Optional: You could insert a manual delay or frame cap here if needed.
    }
}

void App_Shutdown(AppContext* ctx) {
    if (ctx->renderer) {
        vk_renderer_shutdown(ctx->renderer);
        ctx->renderer = NULL;
    }
    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
        ctx->window = NULL;
    }
    SDL_Quit();
}


void App_SetRenderMode(AppContext* ctx, RenderMode mode, float threshold) {
    ctx->renderMode = mode;
    ctx->renderThreshold = threshold;
}

bool App_RenderOnce(AppContext* ctx, void (*handleRender)(AppContext* ctx)) {
    if (!ctx || !ctx->renderer || !ctx->window) {
        return false;
    }
    if (ctx->pending_swapchain_recreate) {
        return false;
    }
    Uint32 window_flags = SDL_GetWindowFlags(ctx->window);
    if (window_flags & SDL_WINDOW_MINIMIZED) {
        return false;
    }
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    if (winW <= 0 || winH <= 0) {
        return false;
    }
    vk_renderer_set_logical_size(ctx->renderer, (float)winW, (float)winH);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkExtent2D extent = {0};
    VkResult frame = vk_renderer_begin_frame(ctx->renderer, &cmd, &fb, &extent);
    if (frame == VK_ERROR_OUT_OF_DATE_KHR || frame == VK_SUBOPTIMAL_KHR) {
        if (vk_renderer_recreate_swapchain(ctx->renderer, ctx->window) == VK_SUCCESS) {
            vk_renderer_set_logical_size(ctx->renderer, (float)winW, (float)winH);
        }
        return false;
    }
    if (frame == VK_ERROR_DEVICE_LOST) {
        SDL_Log("Vulkan device lost during frame start; rebuilding renderer.");
        app_recover_device_lost(ctx, winW, winH);
        return false;
    }
    if (frame != VK_SUCCESS) {
        return false;
    }

    if (handleRender) {
        handleRender(ctx);
    }

    VkResult end = vk_renderer_end_frame(ctx->renderer, cmd);
    if (end == VK_ERROR_OUT_OF_DATE_KHR || end == VK_SUBOPTIMAL_KHR) {
        if (vk_renderer_recreate_swapchain(ctx->renderer, ctx->window) == VK_SUCCESS) {
            vk_renderer_set_logical_size(ctx->renderer, (float)winW, (float)winH);
        }
        return false;
    }
    if (end == VK_ERROR_DEVICE_LOST) {
        SDL_Log("Vulkan device lost during frame submit; rebuilding renderer.");
        app_recover_device_lost(ctx, winW, winH);
        return false;
    }
    return end == VK_SUCCESS;
}
