#pragma once

// Converts a relative vertical gesture into a bounded, size-independent knob position.
static inline float daw_knob_drag_position(float initial, int start_y, int y, float scale) {
    float value = initial + ((float)start_y - (float)y) * scale / 1000.0f;
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}
