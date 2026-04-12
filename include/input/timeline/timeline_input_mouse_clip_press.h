#pragma once

#include <stdbool.h>

struct AppState;
struct InputManager;
struct TimelineGeometry;

bool timeline_input_mouse_handle_clip_press(struct InputManager* manager,
                                            struct AppState* state,
                                            const struct TimelineGeometry* geom,
                                            int sample_rate,
                                            int hit_track,
                                            int hit_clip,
                                            bool hit_left,
                                            bool hit_right,
                                            bool shift_held,
                                            bool alt_held,
                                            bool over_timeline);
