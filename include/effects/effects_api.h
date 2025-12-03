#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// v1 targets your current design: float32, INTERLEAVED, in-place by default.
// Backward-compatible refresh that adds OPTIONAL sidechain support.
// Existing effects keep working unchanged (process_sc may be NULL).

#define FX_API_VERSION 1
#define FX_MAX_PARAMS  32

// Flags
#define FX_FLAG_INPLACE_OK   (1u << 0)  // process() may modify input buffer in-place
#define FX_FLAG_HAS_LATENCY  (1u << 1)  // fixed latency in samples is valid

typedef struct FxHandle FxHandle;

// Descriptor returned by each effect factory.
typedef struct {
    const char *name;           // "Gain", "BiquadEQ"
    uint32_t api_version;       // FX_API_VERSION
    uint32_t flags;             // FX_FLAG_*
    uint32_t num_inputs;        // 1 = regular; 2 = sidechain-capable (key on input #2)
    uint32_t num_outputs;       // typically 1
    uint32_t num_params;        // params advertised
    const char *param_names[FX_MAX_PARAMS];
    float       param_defaults[FX_MAX_PARAMS];
    uint32_t latency_samples;   // 0 for most v1 fx (set if lookahead, etc.)
} FxDesc;

// VTable for an instantiated effect.
typedef struct {
    // REAL-TIME SAFE. No allocs, no locks, no logging.
    // Interleaved float32: buffers are length = frames * channels.
    // If FX_FLAG_INPLACE_OK is not set, manager will provide a scratch buffer
    // and call process(in, scratch) then copy back.
    void (*process)(FxHandle*,
                    const float *in_interleaved,
                    float *out_interleaved,
                    int frames,
                    int channels);

    // OPTIONAL sidechain-aware path. If non-NULL AND a sidechain bus is routed,
    // the engine should call this instead of process().
    // sidechain is a separate interleaved buffer; sc_channels is its channel count.
    void (*process_sc)(FxHandle*,
                       const float *in_interleaved,
                       const float *sidechain_interleaved,
                       float *out_interleaved,
                       int frames,
                       int channels,
                       int sc_channels);

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

