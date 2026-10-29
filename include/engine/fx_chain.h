#pragma once
#include "effects/effects_manager.h"
struct Engine;
// Selects one prepared chain edit a portable replacement, or an identity-preserving history restore.
typedef enum EngineFxChainOp { ENGINE_FX_CHAIN_ADD, ENGINE_FX_CHAIN_REMOVE, ENGINE_FX_CHAIN_REORDER, ENGINE_FX_CHAIN_ENABLE, ENGINE_FX_CHAIN_PARAM, ENGINE_FX_CHAIN_REPLACE, ENGINE_FX_CHAIN_AUTOMATION, ENGINE_FX_CHAIN_RESTORE } EngineFxChainOp;
// Carries one bounded effect edit expressed in native physical values and optional beat units.
typedef struct EngineFxChainRequest {
    EngineFxChainOp op;
    FxInstId id;
    FxTypeId type;
    int position;
    bool enabled;
    uint32_t parameter;
    float value, beats;
    FxParamMode mode;
    EngineMixerAutomation automation;
    const FxMasterSnapshot* restore;
} EngineFxChainRequest;
// Prepares a private chain and its complete readback before one render publication.
bool engine_fx_chain_transaction(struct Engine*, int track, const EngineFxChainRequest*, FxMasterSnapshot*);
