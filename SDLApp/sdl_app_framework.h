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
} AppContext;


// AppCallbacks holds the input/update/render hooks driven by App_Run.
typedef struct {
    void (*handleInput)(AppContext* ctx);   // SDL event input handling
    void (*handleUpdate)(AppContext* ctx);  // Per-frame logic
    void (*handleRender)(AppContext* ctx);  // Render function
} AppCallbacks;



// App_Init initializes the SDL window and Vulkan renderer before starting the main loop.
bool App_Init(AppContext* ctx, const char* title, int width, int height, bool vsync);
// App_SetRenderMode configures the render cadence used by App_Run.
void App_SetRenderMode(AppContext* ctx, RenderMode mode, float threshold);
// App_RenderOnce runs a single frame of rendering against the Vulkan swapchain.
bool App_RenderOnce(AppContext* ctx, void (*handleRender)(AppContext* ctx));


// App_Run drives the event loop and calls the configured callbacks every frame.
void App_Run(AppContext* ctx, AppCallbacks* callbacks);

// App_Shutdown tears down the Vulkan renderer and SDL window resources.
void App_Shutdown(AppContext* ctx);

#ifdef __cplusplus
}
#endif
