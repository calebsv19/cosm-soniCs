#pragma once

#include "effects/effects_manager.h"

#include <stdbool.h>

typedef enum {
    ENGINE_SCOPE_STREAM_NONE = 0,
    ENGINE_SCOPE_STREAM_GAIN_REDUCTION = 1
} EngineScopeStreamKind;

typedef enum {
    ENGINE_SCOPE_FORMAT_SCALAR = 0,
    ENGINE_SCOPE_FORMAT_FLOAT_ARRAY = 1,
    ENGINE_SCOPE_FORMAT_XY = 2
} EngineScopeStreamFormat;

// Describes a scope stream and how UI should interpret its samples.
typedef struct {
    EngineScopeStreamKind kind;
    EngineScopeStreamFormat format;
    const char* name;
    int stride;
    int update_rate_hz;
} EngineScopeStreamDesc;

// Returns the descriptor metadata for a known scope stream kind.
bool engine_scope_get_stream_desc(EngineScopeStreamKind kind, EngineScopeStreamDesc* out_desc);
