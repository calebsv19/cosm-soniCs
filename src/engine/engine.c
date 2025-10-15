#include "engine.h"

#include "audio_device.h"
#include "audio_queue.h"
#include "engine/graph.h"
#include "engine_render.h"
#include "ringbuf.h"

#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    ENGINE_CMD_PLAY = 1,
    ENGINE_CMD_STOP = 2,
    ENGINE_CMD_GRAPH_SWAP = 3,
} EngineCommandType;

typedef struct {
    EngineCommandType type;
    union {
        struct {
            EngineGraph* new_graph;
        } graph_swap;
    } payload;
} EngineCommand;

struct Engine {
    EngineRuntimeConfig config;
    AudioDevice device;
    bool device_started;
    AudioQueue output_queue;
    SDL_Thread* worker_thread;
    atomic_bool worker_running;
    atomic_bool transport_playing;
    RingBuffer command_queue;
    EngineGraph* graph;
    EngineRenderState render_state;
};

static bool engine_post_command(Engine* engine, const EngineCommand* cmd) {
    if (!engine || !cmd) {
        return false;
    }
    return ringbuf_write(&engine->command_queue, cmd, sizeof(*cmd)) == sizeof(*cmd);
}

static void engine_audio_callback(float* output, int frames, int channels, void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!output || frames <= 0 || !engine) {
        return;
    }
    size_t grabbed = audio_queue_read(&engine->output_queue, output, (size_t)frames);
    if (grabbed < (size_t)frames) {
        size_t missing = (size_t)frames - grabbed;
        memset(output + grabbed * channels, 0, missing * (size_t)channels * sizeof(float));
    }
}

static void engine_graph_tone_source(void* userdata, float* interleaved_out, int frames) {
    Engine* engine = (Engine*)userdata;
    if (!engine) {
        return;
    }
    engine_render_render(&engine->render_state, interleaved_out, (size_t)frames);
}

static void engine_process_commands(Engine* engine) {
    EngineCommand cmd;
    while (ringbuf_read(&engine->command_queue, &cmd, sizeof(cmd)) == sizeof(cmd)) {
        switch (cmd.type) {
            case ENGINE_CMD_PLAY:
                atomic_store_explicit(&engine->transport_playing, true, memory_order_release);
                break;
            case ENGINE_CMD_STOP:
                atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
                audio_queue_clear(&engine->output_queue);
                engine_render_reset(&engine->render_state);
                break;
            case ENGINE_CMD_GRAPH_SWAP:
                if (cmd.payload.graph_swap.new_graph) {
                    EngineGraph* old = engine->graph;
                    engine->graph = cmd.payload.graph_swap.new_graph;
                    if (old) {
                        engine_graph_destroy(old);
                    }
                }
                break;
            default:
                break;
        }
    }
}

static int engine_worker_main(void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!engine) {
        return -1;
    }
    int channels = engine->graph ? engine_graph_get_channels(engine->graph) : engine->render_state.channels;
    if (channels <= 0) {
        channels = 2;
    }
    const int block = engine->config.block_size;
    float* block_buffer = (float*)malloc((size_t)block * (size_t)channels * sizeof(float));
    if (!block_buffer) {
        return -1;
    }

    while (atomic_load_explicit(&engine->worker_running, memory_order_acquire)) {
        engine_process_commands(engine);
        if (!atomic_load_explicit(&engine->transport_playing, memory_order_acquire)) {
            SDL_Delay(2);
            continue;
        }

        if (audio_queue_space_frames(&engine->output_queue) < (size_t)block) {
            SDL_Delay(1);
            continue;
        }

        engine_graph_render(engine->graph, block_buffer, block);

        audio_queue_write(&engine->output_queue, block_buffer, (size_t)block);
    }

    free(block_buffer);
    return 0;
}

Engine* engine_create(const EngineRuntimeConfig* cfg) {
    Engine* engine = (Engine*)calloc(1, sizeof(Engine));
    if (!engine) {
        return NULL;
    }
    if (cfg) {
        engine->config = *cfg;
    } else {
        config_set_defaults(&engine->config);
    }
    engine->device_started = false;
    engine->worker_thread = NULL;
    atomic_init(&engine->worker_running, false);
    atomic_init(&engine->transport_playing, false);
    if (!ringbuf_init(&engine->command_queue, sizeof(EngineCommand) * 64)) {
        free(engine);
        return NULL;
    }
    engine_render_init(&engine->render_state, engine->config.sample_rate, 2);
    engine->graph = engine_graph_create(engine->config.sample_rate, 2, engine->config.block_size);
    if (!engine->graph) {
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine_graph_set_source(engine->graph, engine_graph_tone_source, engine);
    return engine;
}

void engine_destroy(Engine* engine) {
    if (!engine) {
        return;
    }
    engine_stop(engine);
    audio_device_close(&engine->device);
    if (engine->graph) {
        engine_graph_destroy(engine->graph);
        engine->graph = NULL;
    }
    ringbuf_free(&engine->command_queue);
    audio_queue_free(&engine->output_queue);
    free(engine);
}

bool engine_start(Engine* engine) {
    if (!engine) {
        return false;
    }

    AudioDeviceSpec want = {
        .sample_rate = engine->config.sample_rate,
        .block_size = engine->config.block_size,
        .channels = 2
    };
    if (!engine->device.is_open) {
        if (!audio_device_open(&engine->device, &want, engine_audio_callback, engine)) {
            SDL_Log("engine_start: failed to open audio device");
            return false;
        }
    }
    const AudioDeviceSpec* have = &engine->device.spec;
    engine->config.sample_rate = have->sample_rate;
    engine->config.block_size = have->block_size;

    size_t capacity_frames = (size_t)engine->config.block_size * 32;
    if (engine->output_queue.channels != have->channels || engine->output_queue.buffer.data == NULL) {
        audio_queue_free(&engine->output_queue);
        if (!audio_queue_init(&engine->output_queue, have->channels, capacity_frames)) {
            SDL_Log("engine_start: failed to init audio queue");
            return false;
        }
    } else {
        audio_queue_clear(&engine->output_queue);
    }

    ringbuf_reset(&engine->command_queue);
    engine_render_init(&engine->render_state, have->sample_rate, have->channels);
    if (engine_graph_configure(engine->graph, have->sample_rate, have->channels, engine->config.block_size) != 0) {
        SDL_Log("engine_start: failed to configure graph");
        return false;
    }
    engine_graph_set_source(engine->graph, engine_graph_tone_source, engine);

    atomic_store_explicit(&engine->worker_running, true, memory_order_release);
    engine->worker_thread = SDL_CreateThread(engine_worker_main, "engine_worker", engine);
    if (!engine->worker_thread) {
        SDL_Log("engine_start: failed to create worker thread: %s", SDL_GetError());
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        return false;
    }

    if (!audio_device_start(&engine->device)) {
        SDL_Log("engine_start: failed to start audio device");
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        SDL_WaitThread(engine->worker_thread, NULL);
        engine->worker_thread = NULL;
        return false;
    }
    engine->device_started = true;

    SDL_Log("Audio device running: %d Hz, %d channels, block size %d",
            have->sample_rate, have->channels, have->block_size);

    engine_transport_play(engine);
    return true;
}

void engine_stop(Engine* engine) {
    if (!engine) {
        return;
    }
    if (engine->device_started) {
        audio_device_stop(&engine->device);
        engine->device_started = false;
    }
    if (engine->worker_thread) {
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        SDL_WaitThread(engine->worker_thread, NULL);
        engine->worker_thread = NULL;
    }
    atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
    engine_render_reset(&engine->render_state);
}

const EngineRuntimeConfig* engine_get_config(const Engine* engine) {
    if (!engine) {
        return NULL;
    }
    return &engine->config;
}

bool engine_is_running(const Engine* engine) {
    if (!engine) {
        return false;
    }
    return engine->device_started;
}

size_t engine_get_queued_frames(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    return audio_queue_available_frames(&engine->output_queue);
}

bool engine_transport_play(Engine* engine) {
    if (!engine) {
        return false;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_PLAY};
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_transport_play: command queue full");
        return false;
    }
    atomic_store_explicit(&engine->transport_playing, true, memory_order_release);
    return true;
}

bool engine_transport_stop(Engine* engine) {
    if (!engine) {
        return false;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_STOP};
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_transport_stop: command queue full");
        return false;
    }
    atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
    return true;
}

bool engine_transport_is_playing(const Engine* engine) {
    if (!engine) {
        return false;
    }
    return atomic_load_explicit(&engine->transport_playing, memory_order_acquire);
}

bool engine_queue_graph_swap(Engine* engine, EngineGraph* new_graph) {
    if (!engine || !new_graph) {
        return false;
    }
    EngineCommand cmd = {
        .type = ENGINE_CMD_GRAPH_SWAP,
        .payload.graph_swap.new_graph = new_graph,
    };
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_queue_graph_swap: command queue full");
        engine_graph_destroy(new_graph);
        return false;
    }
    return true;
}
