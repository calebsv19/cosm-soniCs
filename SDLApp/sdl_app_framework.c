#include "sdl_app_framework.h"
#include "ui/font.h"
#include "core_time.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

bool App_RenderOnce(AppContext* ctx, void (*handleRender)(AppContext* ctx));

typedef struct AppLoopDiagAccum {
    uint64_t period_start_ns;
    uint32_t loop_ticks;
    uint32_t waits_called;
    uint64_t blocked_ns;
    uint64_t active_ns;
    uint32_t renders;
    uint32_t internal_events;
} AppLoopDiagAccum;

static bool app_env_headless_enabled(void) {
    const char* v = getenv("DAW_HEADLESS");
    if (!v || !v[0]) return false;
    return (strcmp(v, "1") == 0 ||
            strcmp(v, "true") == 0 ||
            strcmp(v, "TRUE") == 0 ||
            strcmp(v, "yes") == 0 ||
            strcmp(v, "on") == 0);
}

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
    if (!ctx) {
        return false;
    }

    const bool headless = app_env_headless_enabled();
    Uint32 sdl_flags = SDL_INIT_TIMER;
    if (headless) {
        sdl_flags |= SDL_INIT_EVENTS;
    } else {
        sdl_flags |= SDL_INIT_VIDEO;
    }

    if (SDL_Init(sdl_flags) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (headless) {
        (void)title;
        (void)width;
        (void)height;
        (void)vsync;
        ctx->window = NULL;
        ctx->renderer = NULL;
        ctx->deltaTime = 0.0f;
        ctx->quit = false;
        ctx->userData = NULL;
        ctx->has_event = false;
        ctx->pending_swapchain_recreate = false;
        ctx->pending_swapchain_width = 0;
        ctx->pending_swapchain_height = 0;
        SDL_zero(ctx->current_event);
        ctx->renderMode = RENDER_ALWAYS;
        ctx->renderThreshold = 0.033f;
        ctx->timeSinceLastRender = 0.0f;
        ctx->loopPolicy.wake_block_enabled = false;
        ctx->loopPolicy.heartbeat_ms = 1000;
        ctx->loopPolicy.max_wait_ms = 16;
        ctx->loopDiagnosticsEnabled = false;
        ctx->loopDiagnosticsPeriodMs = 1000;
        SDL_Log("SDLApp running in headless loop mode (DAW_HEADLESS=1).");
        return true;
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
    ctx->loopPolicy.wake_block_enabled = false;
    ctx->loopPolicy.heartbeat_ms = 1000;
    ctx->loopPolicy.max_wait_ms = 16;
    ctx->loopDiagnosticsEnabled = false;
    ctx->loopDiagnosticsPeriodMs = 1000;

    return true;
}

// Routes one SDL event through framework state updates and client input callback dispatch.
static bool app_process_event(AppContext* ctx, AppCallbacks* callbacks, const SDL_Event* event) {
    if (!ctx || !event) {
        return false;
    }

    if (event->type == SDL_QUIT && !app_env_headless_enabled()) {
        ctx->quit = true;
    }

    if (event->type == SDL_WINDOWEVENT) {
        if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            event->window.event == SDL_WINDOWEVENT_RESIZED) {
            ctx->pending_swapchain_recreate = true;
            ctx->pending_swapchain_width = event->window.data1;
            ctx->pending_swapchain_height = event->window.data2;
        }
    }

    if (callbacks && callbacks->isInternalEvent && callbacks->isInternalEvent(ctx, event)) {
        return true;
    }

    if (callbacks && callbacks->handleInput) {
        ctx->current_event = *event;
        ctx->has_event = true;
        callbacks->handleInput(ctx);
        ctx->has_event = false;
    }

    return false;
}

// Computes render cadence when no custom render predicate is installed.
static bool app_should_render_default(AppContext* ctx) {
    if (!ctx) {
        return false;
    }

    if (ctx->renderMode == RENDER_ALWAYS) {
        return true;
    }

    if (ctx->renderMode == RENDER_THROTTLED &&
        ctx->timeSinceLastRender >= ctx->renderThreshold) {
        return true;
    }

    return false;
}

// Computes the next wait timeout in milliseconds with heartbeat and clamp.
static uint32_t app_compute_wait_timeout_ms(AppContext* ctx, AppCallbacks* callbacks) {
    if (!ctx) {
        return 0;
    }

    uint32_t timeout_ms = ctx->loopPolicy.max_wait_ms > 0 ? ctx->loopPolicy.max_wait_ms : 1u;
    if (callbacks && callbacks->computeWaitTimeoutMs) {
        timeout_ms = callbacks->computeWaitTimeoutMs(ctx);
    }
    if (ctx->loopPolicy.max_wait_ms > 0 && timeout_ms > ctx->loopPolicy.max_wait_ms) {
        timeout_ms = ctx->loopPolicy.max_wait_ms;
    }
    return timeout_ms;
}

// Emits loop diagnostics on configured period boundaries.
static void app_maybe_emit_loop_diagnostics(AppContext* ctx,
                                            AppCallbacks* callbacks,
                                            AppLoopDiagAccum* accum,
                                            uint64_t now_ns) {
    if (!ctx || !accum || !ctx->loopDiagnosticsEnabled || !callbacks || !callbacks->onLoopDiagnostics) {
        return;
    }

    uint64_t period_ns = core_time_seconds_to_ns((double)ctx->loopDiagnosticsPeriodMs / 1000.0);
    if (period_ns == 0) {
        period_ns = core_time_seconds_to_ns(1.0);
    }

    if (accum->period_start_ns == 0) {
        accum->period_start_ns = now_ns;
    }

    if (core_time_diff_ns(now_ns, accum->period_start_ns) < period_ns) {
        return;
    }

    AppLoopDiagnostics diag = {0};
    diag.period_start_ns = accum->period_start_ns;
    diag.period_end_ns = now_ns;
    diag.loop_ticks = accum->loop_ticks;
    diag.waits_called = accum->waits_called;
    diag.blocked_ns = accum->blocked_ns;
    diag.active_ns = accum->active_ns;
    diag.renders = accum->renders;
    diag.internal_events = accum->internal_events;
    callbacks->onLoopDiagnostics(ctx, &diag);

    accum->period_start_ns = now_ns;
    accum->loop_ticks = 0;
    accum->waits_called = 0;
    accum->blocked_ns = 0;
    accum->active_ns = 0;
    accum->renders = 0;
    accum->internal_events = 0;
}

void App_Run(AppContext* ctx, AppCallbacks* callbacks) {
    uint64_t last_time_ns = core_time_now_ns();
    uint64_t last_heartbeat_ms = SDL_GetTicks64();
    AppLoopDiagAccum diag = {0};
    diag.period_start_ns = last_time_ns;

    while (!ctx->quit) {
        uint64_t loop_start_ns = core_time_now_ns();
        SDL_Event event;
        bool had_polled_events = false;

        // Step 1: drain SDL events without blocking.
        while (SDL_PollEvent(&event)) {
            had_polled_events = true;
            if (app_process_event(ctx, callbacks, &event)) {
                diag.internal_events++;
            }
        }

        if (ctx->pending_swapchain_recreate) {
            if (app_try_recreate_swapchain(ctx,
                                           ctx->pending_swapchain_width,
                                           ctx->pending_swapchain_height)) {
                ctx->pending_swapchain_recreate = false;
            }
        }

        // Step 2: compute delta time for update/render consumers.
        uint64_t now_ns = core_time_now_ns();
        ctx->deltaTime = (float)core_time_ns_to_seconds(core_time_diff_ns(now_ns, last_time_ns));
        last_time_ns = now_ns;
        ctx->timeSinceLastRender += ctx->deltaTime;

        // Step 3: background non-UI slice (timers/jobs/messages/kernel).
        if (callbacks && callbacks->handleBackgroundTick) {
            callbacks->handleBackgroundTick(ctx, now_ns);
        }

        // Step 4: determine immediate work and render cadence state.
        bool immediate_work = had_polled_events || ctx->pending_swapchain_recreate;
        if (callbacks && callbacks->hasImmediateWork && callbacks->hasImmediateWork(ctx)) {
            immediate_work = true;
        }

        bool should_render = false;
        if (callbacks && callbacks->shouldRenderNow) {
            should_render = callbacks->shouldRenderNow(ctx, now_ns);
        } else {
            should_render = app_should_render_default(ctx);
        }

        // Run updates for urgent work and also on render cadence ticks.
        if (callbacks && callbacks->handleUpdate) {
            if (!callbacks->hasImmediateWork || immediate_work || should_render) {
                callbacks->handleUpdate(ctx);
            }
        }

        // Step 5: render gate (re-evaluate after update).
        if (callbacks && callbacks->shouldRenderNow) {
            should_render = callbacks->shouldRenderNow(ctx, now_ns);
        } else {
            should_render = app_should_render_default(ctx);
        }
        if (should_render && callbacks && callbacks->handleRender) {
            bool rendered = false;
            if (ctx->window == NULL && ctx->renderer == NULL && app_env_headless_enabled()) {
                callbacks->handleRender(ctx);
                rendered = true;
            } else if (App_RenderOnce(ctx, callbacks->handleRender)) {
                rendered = true;
            }
            if (rendered) {
                ctx->timeSinceLastRender = 0.0f;
                last_heartbeat_ms = SDL_GetTicks64();
                diag.renders++;
            }
        }

        // Step 6: wait path when idle and wake-block mode is enabled.
        bool immediate_after = ctx->pending_swapchain_recreate;
        if (callbacks && callbacks->hasImmediateWork && callbacks->hasImmediateWork(ctx)) {
            immediate_after = true;
        }
        uint64_t active_end_ns = core_time_now_ns();
        diag.loop_ticks++;
        diag.active_ns += core_time_diff_ns(active_end_ns, loop_start_ns);

        if (ctx->loopPolicy.wake_block_enabled && !ctx->quit && !immediate_after && !should_render) {
            uint32_t timeout_ms = app_compute_wait_timeout_ms(ctx, callbacks);
            if (ctx->loopPolicy.heartbeat_ms > 0) {
                uint64_t now_ms64 = SDL_GetTicks64();
                uint64_t elapsed_ms = now_ms64 >= last_heartbeat_ms ? (now_ms64 - last_heartbeat_ms) : 0;
                uint32_t until_heartbeat = elapsed_ms >= ctx->loopPolicy.heartbeat_ms
                                               ? 0u
                                               : (uint32_t)(ctx->loopPolicy.heartbeat_ms - elapsed_ms);
                if (until_heartbeat < timeout_ms) {
                    timeout_ms = until_heartbeat;
                }
            }

            diag.waits_called++;
            uint64_t blocked_start_ns = core_time_now_ns();

            SDL_Event waited_event;
            SDL_zero(waited_event);
            bool got_event = false;
            if (timeout_ms > 0) {
                if (callbacks && callbacks->waitForEvent) {
                    got_event = callbacks->waitForEvent(ctx, timeout_ms, &waited_event);
                } else {
                    got_event = SDL_WaitEventTimeout(&waited_event, (int)timeout_ms) == SDL_TRUE;
                }
            }

            uint64_t blocked_end_ns = core_time_now_ns();
            diag.blocked_ns += core_time_diff_ns(blocked_end_ns, blocked_start_ns);

            if (got_event) {
                if (app_process_event(ctx, callbacks, &waited_event)) {
                    diag.internal_events++;
                }
                while (SDL_PollEvent(&event)) {
                    if (app_process_event(ctx, callbacks, &event)) {
                        diag.internal_events++;
                    }
                }
            }
        }

        app_maybe_emit_loop_diagnostics(ctx, callbacks, &diag, core_time_now_ns());
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

void App_SetLoopPolicy(AppContext* ctx, AppLoopPolicy policy) {
    if (!ctx) {
        return;
    }
    ctx->loopPolicy = policy;
    if (ctx->loopPolicy.max_wait_ms == 0) {
        ctx->loopPolicy.max_wait_ms = 1;
    }
}

void App_EnableLoopDiagnostics(AppContext* ctx, bool enabled, uint32_t period_ms) {
    if (!ctx) {
        return;
    }
    ctx->loopDiagnosticsEnabled = enabled;
    ctx->loopDiagnosticsPeriodMs = period_ms > 0 ? period_ms : 1000;
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
