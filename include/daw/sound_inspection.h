#pragma once
#include "daw/preset_library.h"
#include "daw/app_actions.h"
#define DAW_SOUND_RATE 48000
#define DAW_SOUND_FRAMES 144000
#define DAW_SOUND_GATE 72000
#define DAW_SOUND_POINTS 256
#define DAW_SOUND_BINS 128
// Selects the same bounded time window for waveform and tonal spectrum inspection.
typedef enum DawSoundWindow { DAW_SOUND_ATTACK, DAW_SOUND_SUSTAIN, DAW_SOUND_RELEASE } DawSoundWindow;
// Captures an isolated sound independently of editable project pointers or library files.
typedef struct DawSoundSpec { EngineInstrumentPresetId base; EngineInstrumentParams params; bool chord; } DawSoundSpec;
// Owns one fixed-format float render until the caller releases it.
typedef struct DawSoundRender { float* samples; double peak, rms, dc; uint64_t clipped_samples; } DawSoundRender;
// Carries calibrated source metrics and bounded plots with explicit rendering/window semantics.
typedef struct DawSoundAnalysis {
    DawSoundSpec sound;
    DawSoundWindow window;
    uint64_t window_start;
    double peak, rms, dc;
    uint64_t clipped_samples;
    float waveform[DAW_SOUND_POINTS], envelope_rms[DAW_SOUND_POINTS];
    float frequency_hz[DAW_SOUND_BINS], spectrum_db[DAW_SOUND_BINS];
} DawSoundAnalysis;
// Rejects nonfinite or out-of-range captured settings before rendering or audition publication.
bool daw_sound_spec_valid(DawSoundSpec sound);
// Renders the fixed held C4/chord through the production synthesizer with original level and no effects.
bool daw_sound_render(DawSoundSpec sound, DawSoundRender* out);
// Releases only caller-owned private render storage.
void daw_sound_render_free(DawSoundRender* render);
// Measures a fixed stereo buffer and prepares plot data using the existing Hann tonal transform.
bool daw_sound_measure(DawSoundSpec sound, DawSoundWindow window, const float* samples, DawSoundAnalysis* out);
// Captures and analyzes a private render without writing files or touching device/project/history state.
bool daw_sound_analyze(DawSoundSpec sound, DawSoundWindow window, DawSoundAnalysis* out);
// Analyzes one current owned target and returns the exact captured authored/effective state.
DawActionResult daw_action_analyze_instrument(struct AppState*, DawActionTarget, bool chord, DawSoundWindow, DawSoundAnalysis*);
// Analyzes a current library revision without creating directories or preview files.
DawPresetStatus daw_action_analyze_preset(struct AppState*, const char*, const char*, bool chord, DawSoundWindow, DawSoundAnalysis*);
// Auditions a captured reference sound through the target track without applying or saving its settings.
DawActionResult daw_action_audition_sound(struct AppState*, DawActionTarget, DawSoundSpec);
