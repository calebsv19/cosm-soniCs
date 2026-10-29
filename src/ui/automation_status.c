#include "ui/automation_status.h"
#include "app_state.h"
#include "ui/font.h"
#include <inttypes.h>
#include <stdio.h>

// Keeps queued text in persistent state storage and draws one compact automation help strip.
void automation_status_draw(SDL_Renderer* renderer, const SDL_Rect* rect, AppState* state) {
    if (!renderer || !rect || !state || !state->timeline_automation_mode || rect->w < 280 || rect->h < 60) return;
    const EngineAutomationLane* lane = NULL;
    engine_clip_get_automation_lane(state->engine, state->automation_ui.track_index,
        state->automation_ui.clip_index, state->automation_ui.target, &lane);
    int index = state->automation_ui.point_index;
    static const char* const curves[] = {"Linear", "Hold", "Smooth"};
    if (lane && index >= 0 && index < lane->point_count) {
        EngineAutomationPoint point = lane->points[index];
        snprintf(state->automation_ui.status_text, sizeof(state->automation_ui.status_text),
            "%s %.2f %s | frame %" PRIu64 " | %s | 1/2/3 curve | arrows adjust | Alt: 1 sample | [ ] boundaries",
            engine_automation_target_display_label(lane->target), point.value, engine_automation_lane_unit(lane),
            point.frame, curves[point.curve]);
    } else snprintf(state->automation_ui.status_text, sizeof(state->automation_ui.status_text),
        "Automation: click to add/select, drag to move | Alt: free timing | 1/2/3: Linear/Hold/Smooth | arrows: precision");
    int height = ui_font_line_height(1) + 6;
    SDL_Rect strip = {rect->x + 2, rect->y + rect->h - height - 2, rect->w - 4, height};
    SDL_SetRenderDrawColor(renderer, 15, 22, 30, 235);
    SDL_RenderFillRect(renderer, &strip);
    ui_draw_text(renderer, strip.x + 5, strip.y + 3, state->automation_ui.status_text, (SDL_Color){190, 218, 232, 255}, 1);
}
