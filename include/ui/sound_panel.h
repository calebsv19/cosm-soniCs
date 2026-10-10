#pragma once
#include "daw/sound_inspection.h"
#include "daw/composition.h"
#include <SDL2/SDL.h>
struct AppState;
// Keeps explicit inspection results and one immutable reference outside project persistence/history.
typedef struct DawSoundUI {
    bool open, has_current, has_reference;
    int view, pressed, listening;
    bool chord, composition, dry;
    DawCompositionRender captured, captured_reference;
    char published_path[1024];
    DawSoundWindow window;
    DawActionTarget current_target;
    DawSoundAnalysis current, reference;
    char message[160], metrics[320], settings[160], reference_metrics[320];
} DawSoundUI;
// Shares bounded inspection controls between native drawing and the discrete action owner.
typedef struct DawSoundPanelLayout { bool compact; SDL_Rect panel, plot, buttons[17]; } DawSoundPanelLayout;
// Computes the inspection surface or its compact open button inside the instrument preview.
void daw_sound_panel_layout(const struct AppState*, DawSoundPanelLayout*);
// Draws cached original-level plots and calibrated metrics without rendering audio in the painter.
void daw_sound_panel_render(SDL_Renderer*, struct AppState*);
// Activates analysis, reference capture, plot/window/mode selection and finite comparison auditions.
bool daw_sound_panel_activate(struct AppState*,int,int);
// Intercepts open inspection input and protects its underlying instrument controls.
bool daw_sound_panel_event(struct AppState*,const SDL_Event*);

// Releases private composition captures when closing the inspector or shutting down the app.
void daw_sound_panel_release(struct AppState*);
