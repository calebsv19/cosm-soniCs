#pragma once

#include "config.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct Engine Engine;
struct EngineGraph;

Engine* engine_create(const EngineRuntimeConfig* cfg);
void    engine_destroy(Engine* engine);
bool    engine_start(Engine* engine);
void    engine_stop(Engine* engine);
const EngineRuntimeConfig* engine_get_config(const Engine* engine);
bool    engine_is_running(const Engine* engine);
size_t  engine_get_queued_frames(const Engine* engine);
bool    engine_transport_play(Engine* engine);
bool    engine_transport_stop(Engine* engine);
bool    engine_transport_is_playing(const Engine* engine);
bool    engine_queue_graph_swap(Engine* engine, struct EngineGraph* new_graph);
