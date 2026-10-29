#pragma once
#include "ui/timeline_view_internal.h"
#include "engine/engine.h"
struct AppState;
// Describes one visible descendant source region without fabricating a group clip.
typedef struct TimelineGroupOverviewItem {
    SDL_Rect rect;
    uint64_t source_start,frames;
    int track,clip;
    bool midi;
} TimelineGroupOverviewItem;
// Projects one source region into its ancestor's lane with trim and viewport bounds applied.
bool timeline_group_overview_item(const struct AppState*,int group,int track,int clip,TimelineGroupOverviewItem*);
// Paints combined MIDI strokes and source waveform previews in separate bands when mixed.
void timeline_group_overview_render(SDL_Renderer*,struct AppState*,int group,const TimelineTheme*);
// Selects a stable track/bus; double activation expands a collapsed group without editing its regions.
void timeline_group_overview_activate(struct AppState*,uint64_t identity,bool expand);
