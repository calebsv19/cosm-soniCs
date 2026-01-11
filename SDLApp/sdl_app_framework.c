#include "sdl_app_framework.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <stdio.h>

bool App_RenderOnce(AppContext* ctx, void (*handleRender)(AppContext* ctx));

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

    VkResult init = vk_renderer_init(&ctx->renderer_storage, ctx->window, &cfg);
    if (init != VK_SUCCESS) {
        SDL_Log("Failed to initialize Vulkan renderer (code %d)", init);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return false;
    }
    ctx->renderer = &ctx->renderer_storage;

    ctx->deltaTime = 0.0f;
    ctx->quit = false;
    ctx->userData = NULL;
    ctx->has_event = false;
    SDL_zero(ctx->current_event);
    ctx->renderMode = RENDER_ALWAYS;
    ctx->renderThreshold = 0.033f;     // 30 FPS by default
    ctx->timeSinceLastRender = 0.0f;

    return true;
}

void App_Run(AppContext* ctx, AppCallbacks* callbacks) {
    Uint64 lastTime = SDL_GetPerformanceCounter();
    SDL_Event event;

    while (!ctx->quit) {
        // Input Handling
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                ctx->quit = true;
            }
            if (callbacks && callbacks->handleInput) {
                ctx->current_event = event;
                ctx->has_event = true;
                callbacks->handleInput(ctx);
                ctx->has_event = false;
            }
        }

        // Delta Time Calculation
        Uint64 now = SDL_GetPerformanceCounter();
	ctx->deltaTime = (float)(now - lastTime) / SDL_GetPerformanceFrequency();
	lastTime = now;
	
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
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    vk_renderer_set_logical_size(ctx->renderer, (float)winW, (float)winH);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkExtent2D extent = {0};
    VkResult frame = vk_renderer_begin_frame(ctx->renderer, &cmd, &fb, &extent);
    if (frame == VK_ERROR_OUT_OF_DATE_KHR || frame == VK_SUBOPTIMAL_KHR) {
        vk_renderer_recreate_swapchain(ctx->renderer, ctx->window);
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
        vk_renderer_recreate_swapchain(ctx->renderer, ctx->window);
        return false;
    }
    return end == VK_SUCCESS;
}
