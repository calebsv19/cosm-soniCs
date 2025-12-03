\
    // effects_builtin.h - central registration of built-in effects
    #pragma once
    #include "effects/effects_manager.h"

    #ifdef __cplusplus
    extern "C" {
    #endif

    // Call once after fxm_create() to register every compiled-in effect.
    // Safe to call multiple times on fresh managers.
    void fx_register_builtins_all(struct EffectsManager* fm);

    // Optional: expose the table for UI tests/tools (read-only).
    // Returns pointer to internal registry array and count.
    // NOTE: Valid only after fx_register_builtins_all() has been called on some manager.
    const FxRegistryEntry* fx_get_builtin_registry(int* out_count);

    #ifdef __cplusplus
    } // extern "C"
    #endif
