#pragma once
#include <SDL2/SDL.h>
#include "kit_ui_text_edit_sdl.h"

// Decimal filtering is Sonics tempo policy; UTF-8 mechanics remain shared.
enum { DAW_TEXT_DECIMAL = 4u };
// Begins a fresh bounded caller-owned edit and clears previous selection/preedit.
void daw_text_edit_begin(KitUiTextEdit*, char*, size_t, int*, unsigned);
// Routes shared editing while mirroring the legacy cursor used by product owners.
KitUiTextEventResult daw_text_edit_event(KitUiTextEdit*, char*, size_t, int*, unsigned, const SDL_Event*);
// Moves the caret to a measured UTF-8 boundary without retaining stale selection.
void daw_text_edit_position(KitUiTextEdit*, char*, size_t, int*, int, unsigned);
// Paints selection, preedit and caret synchronously inside an existing text viewport.
void daw_text_edit_draw(SDL_Renderer*, const KitUiTextEdit*, char*, size_t, int,
                        SDL_Rect, SDL_Color, SDL_Color, float);
struct AppState;
// Cancels all staged text when a modal or window supersedes its input owner.
void daw_text_cancel_composition(struct AppState*);
// Uses shared measured presentation to place a mouse caret at a UTF-8 boundary.
void daw_text_edit_click(KitUiTextEdit*, char*, size_t, int*, unsigned,
                         float, int, int, int);
// Restores platform text delivery to the surviving field after a modal closes.
void daw_text_resume_input(struct AppState*);
