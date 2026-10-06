#pragma once

// Returns compressor gain change in dB with a continuous value and slope at
// both knee boundaries.
static inline float fx_compressor_gain_db(float input_db, float threshold_db, float ratio, float knee_db) {
    if (ratio <= 1)
        return 0;
    float delta = input_db - threshold_db;
    float slope = 1 / ratio - 1;
    if (knee_db <= 1e-6f)
        return delta > 0 ? slope * delta : 0;
    if (delta <= -knee_db * .5f)
        return 0;
    if (delta >= knee_db * .5f)
        return slope * delta;
    float distance = delta + knee_db * .5f;
    return slope * distance * distance / (2 * knee_db);
}
