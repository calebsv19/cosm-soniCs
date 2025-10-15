#include "sdl_app_framework.h"
#include <SDL2/SDL.h>
#include <stdio.h>

bool App_Init(AppContext* ctx, const char* title, int width, int height, bool vsync) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    ctx->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
					width, height, windowFlags);
    if (!ctx->window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, rendererFlags);
    if (!ctx->renderer) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return false;
    }

    ctx->deltaTime = 0.0f;
    ctx->quit = false;
    ctx->userData = NULL;
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
                callbacks->handleInput(ctx);
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
	    callbacks->handleRender(ctx);
	    ctx->timeSinceLastRender = 0.0f;
	}
        // Optional: You could insert a manual delay or frame cap here if needed.
    }
}

void App_Shutdown(AppContext* ctx) {
    if (ctx->renderer) {
        SDL_DestroyRenderer(ctx->renderer);
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

