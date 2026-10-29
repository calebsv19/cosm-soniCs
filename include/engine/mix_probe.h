#pragma once
#include <stdbool.h>
#include <stdint.h>
struct EngineMixState;
struct EngineInstrumentSource;
// Identifies node input, post-inserts/EQ, post-bus-gain/pan and sanitized master output.
typedef enum EngineMixProbeStage { ENGINE_MIX_INPUT, ENGINE_MIX_INSERTS_EQ, ENGINE_MIX_OUTPUT, ENGINE_MIX_SANITIZED, ENGINE_MIX_STAGE_COUNT } EngineMixProbeStage;
// Borrows samples synchronously from a private export plan, never from live rendering.
typedef struct EngineMixProbe {
    void* user;
    bool (*observe)(void*,int,EngineMixProbeStage,uint64_t,uint64_t,const float*,int,int);
    bool (*effect)(void*,int,int,uint64_t,bool,uint64_t,uint64_t,const float*,const float*,int,int);
    int (*prepare_source)(void*,uint64_t,uint64_t,struct EngineInstrumentSource*);
    bool (*source)(void*,int,uint64_t,const float*,int,int);
    uint64_t block_start; // Absolute render-frame origin of the current production block.
    bool failed;
} EngineMixProbe;
// Observes a produced stage with the corresponding production-path latency.
void engine_mix_probe_emit(struct EngineMixState*,int,EngineMixProbeStage,uint64_t,const float*,int,int);

// Prepares per-insert observation only for detached diagnostic ownership.
bool engine_mix_probe_install(struct EngineMixState*,EngineMixProbe*);
