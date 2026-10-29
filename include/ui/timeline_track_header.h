#pragma once
#include "ui/timeline_view.h"
#include "ui/timeline_view_internal.h"
#include "engine/engine.h"
struct AppState;
// Returns a restrained stable lane tint, with an independent selection emphasis.
SDL_Color timeline_track_lane_color(const struct AppState*,int,const TimelineTheme*);
// Paints one compact shared-geometry track entry, including identity, controls and a live meter.
void timeline_track_header_render(SDL_Renderer*,struct AppState*,int,const TimelineTrackHeaderLayout*,const TimelineTheme*);
