#pragma once
#include "engine/engine.h"
// Owns a privately captured graph until the control thread releases it after worker completion.
typedef struct EngineExportSnapshot EngineExportSnapshot;
// Captures bounded range metadata and private graph on the editable engine control owner.
EngineExportSnapshot* engine_export_prepare(Engine*, uint64_t start, uint64_t end,
                                            const EngineBounceOptions*, EngineBounceWavFormat,
                                            bool no_replace);
// Runs a captured streaming export without touching the originating engine or live UI.
DawSaveResult engine_export_run(EngineExportSnapshot*, const char*,
                                const EngineBounceStreamCallbacks*);
// Releases captured cache pins only on the original control thread after worker completion.
void engine_export_free(EngineExportSnapshot*);
