#pragma once

#include <stdbool.h>

#define ENGINE_EQ_BANDS 4

typedef struct {
    bool enabled;
    float freq_hz;
    float gain_db;
    float q_width;
} EngineEqBand;

typedef struct {
    bool enabled;
    float freq_hz;
} EngineEqCut;

typedef struct {
    EngineEqCut low_cut;
    EngineEqCut high_cut;
    EngineEqBand bands[ENGINE_EQ_BANDS];
} EngineEqCurve;

typedef struct {
    bool enabled;
    float b0, b1, b2, a1, a2;
    float* z1;
    float* z2;
} EngineEqFilter;

typedef struct {
    float sample_rate;
    int max_channels;
    bool initialized;
    bool active;
    EngineEqFilter low_cut;
    EngineEqFilter high_cut;
    EngineEqFilter bands[ENGINE_EQ_BANDS];
} EngineEqState;

void engine_eq_init(EngineEqState* eq, float sample_rate, int max_channels);
void engine_eq_free(EngineEqState* eq);
void engine_eq_reset(EngineEqState* eq);
void engine_eq_set_curve(EngineEqState* eq, const EngineEqCurve* curve);
void engine_eq_process(EngineEqState* eq, float* buffer, int frames, int channels);
