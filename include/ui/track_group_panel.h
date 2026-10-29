#pragma once
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
struct AppState;
// Shares responsive group-manager rectangles between painting, pointer ownership and qualification.
typedef struct GroupPanelLayout { SDL_Rect panel,buttons[12],rows[12]; int row_count; } GroupPanelLayout;
// Returns the actual responsive manager geometry for the current window.
GroupPanelLayout daw_group_panel_layout(const struct AppState*);
// Opens the copied-guard native hierarchy editor without changing musical content.
void daw_group_panel_open(struct AppState*);
// Handles exclusive staged-parent and group commands using matching pointer release.
bool daw_group_panel_event(struct AppState*, const SDL_Event*);
// Paints the native hierarchy manager and copies current exact routing guards.
void daw_group_panel_render(SDL_Renderer*, struct AppState*);
// Toggles one group by stable lifetime using the shared routing/history owner.
void daw_group_toggle(struct AppState*, uint64_t identity);
