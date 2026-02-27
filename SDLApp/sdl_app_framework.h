#pragma once
#include <SDL2/SDL.h>
#include "vk_renderer_sdl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RENDER_ALWAYS,
    RENDER_THROTTLED
} RenderMode;

// AppLoopPolicy configures wake-aware loop blocking behavior.
typedef struct {
    bool wake_block_enabled;
    uint32_t heartbeat_ms;
    uint32_t max_wait_ms;
} AppLoopPolicy;

// AppLoopDiagnostics captures loop activity for a diagnostics period.
typedef struct {
    uint64_t period_start_ns;
    uint64_t period_end_ns;
    uint32_t loop_ticks;
    uint32_t waits_called;
    uint64_t blocked_ns;
    uint64_t active_ns;
    uint32_t renders;
    uint32_t internal_events;
} AppLoopDiagnostics;

// AppContext stores SDL/Vulkan handles and per-frame timing state for the main loop.
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    VkRenderer renderer_storage;
    float deltaTime;
    bool quit;
    void* userData;
    SDL_Event current_event;
    bool has_event;
    bool pending_swapchain_recreate;
    int pending_swapchain_width;
    int pending_swapchain_height;

    // NEW: configurable render behavior
    RenderMode renderMode;
    float renderThreshold;      // seconds (e.g., 1.0 / 30.0 for 30 FPS cap)
    float timeSinceLastRender;  // internal counter

    // Wake-aware loop policy.
    AppLoopPolicy loopPolicy;
    bool loopDiagnosticsEnabled;
    uint32_t loopDiagnosticsPeriodMs;
} AppContext;


// AppCallbacks holds the input/update/render hooks driven by App_Run.
typedef struct {
    void (*handleInput)(AppContext* ctx);   // SDL event input handling
    void (*handleUpdate)(AppContext* ctx);  // Per-frame logic
    void (*handleRender)(AppContext* ctx);  // Render function
    void (*handleBackgroundTick)(AppContext* ctx, uint64_t now_ns); // Non-UI loop slice work
    bool (*hasImmediateWork)(AppContext* ctx); // Signals work that should avoid blocking
    uint32_t (*computeWaitTimeoutMs)(AppContext* ctx); // Optional custom wait timeout
    bool (*waitForEvent)(AppContext* ctx, uint32_t timeout_ms, SDL_Event* out_event); // Optional wait bridge
    bool (*isInternalEvent)(AppContext* ctx, const SDL_Event* event); // Wake/internal events
    bool (*shouldRenderNow)(AppContext* ctx, uint64_t now_ns); // Optional render gate override
    void (*onLoopDiagnostics)(AppContext* ctx, const AppLoopDiagnostics* diag); // Diagnostics sink
} AppCallbacks;



// App_Init initializes the SDL window and Vulkan renderer before starting the main loop.
bool App_Init(AppContext* ctx, const char* title, int width, int height, bool vsync);
// App_SetRenderMode configures the render cadence used by App_Run.
void App_SetRenderMode(AppContext* ctx, RenderMode mode, float threshold);
// App_SetLoopPolicy configures wake-blocked loop behavior.
void App_SetLoopPolicy(AppContext* ctx, AppLoopPolicy policy);
// App_EnableLoopDiagnostics enables periodic loop diagnostics callback reports.
void App_EnableLoopDiagnostics(AppContext* ctx, bool enabled, uint32_t period_ms);
// App_RenderOnce runs a single frame of rendering against the Vulkan swapchain.
bool App_RenderOnce(AppContext* ctx, void (*handleRender)(AppContext* ctx));


// App_Run drives the event loop and calls the configured callbacks every frame.
void App_Run(AppContext* ctx, AppCallbacks* callbacks);

// App_Shutdown tears down the Vulkan renderer and SDL window resources.
void App_Shutdown(AppContext* ctx);

#ifdef __cplusplus
}
#endif
