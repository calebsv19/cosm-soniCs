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

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    VkRenderer renderer_storage;
    float deltaTime;
    bool quit;
    void* userData;
    SDL_Event current_event;
    bool has_event;

    // NEW: configurable render behavior
    RenderMode renderMode;
    float renderThreshold;      // seconds (e.g., 1.0 / 30.0 for 30 FPS cap)
    float timeSinceLastRender;  // internal counter
} AppContext;


typedef struct {
    void (*handleInput)(AppContext* ctx);   // SDL event input handling
    void (*handleUpdate)(AppContext* ctx);  // Per-frame logic
    void (*handleRender)(AppContext* ctx);  // Render function
} AppCallbacks;



/**
 * Initializes SDL, creates window and renderer.
 * Returns false on failure. Sets up AppContext.
 */
bool App_Init(AppContext* ctx, const char* title, int width, int height, bool vsync);
void App_SetRenderMode(AppContext* ctx, RenderMode mode, float threshold);
bool App_RenderOnce(AppContext* ctx, void (*handleRender)(AppContext* ctx));


/**
 * Runs the main loop using the provided callback structure.
 */
void App_Run(AppContext* ctx, AppCallbacks* callbacks);

/**
 * Cleans up SDL resources (window, renderer) and shuts down SDL.
 */
void App_Shutdown(AppContext* ctx);

#ifdef __cplusplus
}
#endif
