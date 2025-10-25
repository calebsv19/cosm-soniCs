#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// v1 targets your current design: float32, INTERLEAVED, in-place by default.
// (We can later add a planar path without breaking ABI via flags.)
#define FX_API_VERSION 1
#define FX_MAX_PARAMS  32
#define FX_FLAG_INPLACE_OK   (1u << 0)  // process() may modify input buffer in-place
#define FX_FLAG_HAS_LATENCY  (1u << 1)  // fixed latency in samples is valid

typedef struct FxHandle FxHandle;

// Descriptor returned by each effect factory.
typedef struct {
    const char *name;           // "Gain", "BiquadEQ"
    uint32_t api_version;       // FX_API_VERSION
    uint32_t flags;             // FX_FLAG_*
    uint32_t num_inputs;        // for now always 1 (stereo/mono in a single interleaved bus)
    uint32_t num_outputs;       // for now always 1
    uint32_t num_params;        // params advertised
    const char *param_names[FX_MAX_PARAMS];
    float param_defaults[FX_MAX_PARAMS];
    uint32_t latency_samples;   // 0 for most v1 fx
} FxDesc;

// VTable for an instantiated effect.
typedef struct {
    // REAL-TIME SAFE. No allocs, no locks, no logging.
    // Interleaved float32: 'io' is length = frames * channels.
    // If FX_FLAG_INPLACE_OK is not set, manager will provide a scratch buffer
    // and call process(io, scratch) then copy back.
    void (*process)(FxHandle*,
                    const float *in_interleaved,
                    float *out_interleaved,
                    int frames,
                    int channels);

    // NON-RT. Safe to allocate/recompute (e.g., biquad coeffs).
    void (*set_param)(FxHandle*, uint32_t param_idx, float value);
    void (*reset)(FxHandle*);     // clear histories, z-states
    void (*destroy)(FxHandle*);   // free heap owned by the instance
} FxVTable;

// Factory symbols each effect translation unit exposes.
typedef int (*fx_get_desc_fn)(FxDesc *out);
typedef int (*fx_create_fn)(const FxDesc*,
                            FxHandle **out_handle,
                            FxVTable  *out_vt,
                            uint32_t sample_rate,
                            uint32_t max_block,
                            uint32_t max_channels);

#ifdef __cplusplus
} // extern "C"
#endif

