#include "engine/engine_internal.h"
#include "test_assert.h"

#include <string.h>
#include <math.h>

#define CHECK(value, message) daw_test_expect("engine_command_delivery_test", (value), (message))

// Shares only queue storage and completion flags between the single producer and consumer.
typedef struct DeliveryStress {
    Engine* engine;
    int count;
    atomic_bool failed;
} DeliveryStress;

// Shares a frame queue and completion state for concurrent producer flush validation.
typedef struct AudioFlushStress {
    AudioQueue queue;
    atomic_bool done;
    atomic_bool failed;
} AudioFlushStress;

// Checks channel alignment and monotonically increasing frame identities across concurrent flushes.
static int consume_audio(void* userdata) {
    AudioFlushStress* stress = userdata;
    float samples[21];
    float previous = 0.0f;
    for (;;) {
        size_t count = audio_queue_read(&stress->queue, samples, 7);
        for (size_t i = 0; i < count; ++i) {
            float value = samples[i * 3];
            if (value <= previous || samples[i * 3 + 1] != value || samples[i * 3 + 2] != value)
                atomic_store(&stress->failed, true);
            previous = value;
        }
        if (count == 0 && atomic_load(&stress->done) && audio_queue_available_frames(&stress->queue) == 0) break;
        if (count == 0) SDL_Delay(0);
    }
    return 0;
}

// Checks packet sequence and payload integrity while the producer repeatedly wraps the ring.
static int consume_commands(void* userdata) {
    DeliveryStress* stress = userdata;
    for (int i = 1; i <= stress->count; ++i) {
        EngineCommand received;
        while (!ringbuf_read_exact(&stress->engine->command_queue, &received, sizeof(received))) {
            SDL_Delay(0);
        }
        if (received.type != ENGINE_CMD_SEEK || received.payload.seek.frame != (uint64_t)i) {
            atomic_store(&stress->failed, true);
        }
    }
    return 0;
}

// Verifies that a second producer is refused rather than corrupting SPSC packet publication.
static int post_from_wrong_thread(void* userdata) {
    Engine* engine = userdata;
    TempoState tempo = tempo_state_default(48000);
    if (engine_transport_seek(engine, 42) || engine_transport_set_loop(engine, true, 0, 128) ||
        engine_set_tempo_state(engine, &tempo) || engine_set_record_armed_track(engine, 0)) return 1;
    EngineCommand cmd = {.type = ENGINE_CMD_SEEK, .payload.seek.frame = 999};
    return engine_post_command(userdata, &cmd) ? 1 : 0;
}

// Checks tempo validation, project-rate normalization, and rejection without partial queued state.
static void test_tempo_delivery(Engine* engine) {
    TempoState tempo = tempo_state_default(96000);
    tempo.bpm = 137;
    CHECK(engine_set_tempo_state(engine, &tempo), "offline tempo acceptance");
    CHECK(engine->tempo.bpm == 137 && engine->tempo.sample_rate == engine->config.sample_rate, "project tempo rate");
    tempo.bpm = NAN;
    CHECK(!engine_set_tempo_state(engine, &tempo) && engine->tempo.bpm == 137, "nonfinite tempo mutation");
    engine->device_started = true;
    engine->worker_thread = (SDL_Thread*)engine;
    tempo.bpm = 151;
    CHECK(engine_set_tempo_state(engine, &tempo), "queued tempo acceptance");
    CHECK(engine->tempo.bpm == 137, "control mutated worker tempo");
    engine_process_commands(engine);
    CHECK(engine->tempo.bpm == 151 && engine->tempo.sample_rate == engine->config.sample_rate, "queued tempo adoption");
    EngineCommand filler = {.type = ENGINE_CMD_SET_TEMPO, .payload.tempo.tempo = engine->tempo};
    while (engine_post_command(engine, &filler)) {}
    tempo.bpm = 173;
    CHECK(!engine_set_tempo_state(engine, &tempo), "full queue accepted tempo");
    engine_process_commands(engine);
    CHECK(engine->tempo.bpm == 151, "rejected tempo applied");
    engine->worker_thread = NULL;
    engine->device_started = false;
}

// Exercises short-space rejection, incomplete reads, and packet wrap without an engine thread.
static void test_packet_boundaries(void) {
    RingBuffer ring = {0};
    CHECK(ringbuf_init(&ring, 256), "ring init");
    unsigned char source[96], dest[96];
    memset(source, 0x5a, sizeof(source));
    CHECK(ringbuf_write_exact(&ring, source, sizeof(source)), "first packet");
    CHECK(ringbuf_write_exact(&ring, source, sizeof(source)), "second packet");
    CHECK(!ringbuf_write_exact(&ring, source, sizeof(source)), "partial packet accepted");
    CHECK(ringbuf_available_read(&ring) == 192, "failed write changed queue");
    CHECK(ringbuf_read_exact(&ring, dest, sizeof(dest)), "first read");
    CHECK(ringbuf_write_exact(&ring, source, sizeof(source)), "wrap write");
    CHECK(ringbuf_read_exact(&ring, dest, sizeof(dest)), "second read");
    CHECK(ringbuf_read_exact(&ring, dest, sizeof(dest)), "wrapped read");
    CHECK(memcmp(source, dest, sizeof(dest)) == 0, "wrapped payload corrupted");
    CHECK(ringbuf_write(&ring, source, 32) == 32, "short fixture");
    CHECK(!ringbuf_read_exact(&ring, dest, sizeof(dest)), "incomplete packet consumed");
    CHECK(ringbuf_available_read(&ring) == 32, "failed read changed queue");
    ringbuf_free(&ring);
}

// Reproduces the original queue-overflow interleaving and proves recovery and safety behavior.
static void test_engine_admission(Engine* engine) {
    EngineCommand cmd = {.type = ENGINE_CMD_SEEK, .payload.seek.frame = 12345};
    size_t accepted = 0;
    while (engine_post_command(engine, &cmd)) ++accepted;
    CHECK(accepted > 0, "no ordinary capacity");
    CHECK(ringbuf_available_read(&engine->command_queue) == accepted * sizeof(cmd), "partial bytes published");
    EngineCommand popped;
    CHECK(ringbuf_read_exact(&engine->command_queue, &popped, sizeof(popped)), "free a packet");
    cmd.payload.seek.frame = 98765;
    CHECK(engine_post_command(engine, &cmd), "retry after space");
    engine_process_commands(engine);
    CHECK(engine_get_transport_frame(engine) == 98765, "accepted retry lost");
    CHECK(ringbuf_available_read(&engine->command_queue) == 0, "queue not aligned");

    while (engine_post_command(engine, &cmd)) {}
    EngineCommand stop = {.type = ENGINE_CMD_STOP};
    for (int i = 0; i < 100; ++i) CHECK(engine_post_command(engine, &stop), "stop rejected on overload");
    EngineCommand off = {.type = ENGINE_CMD_MIDI_AUDITION_NOTE_OFF, .payload.midi_audition.note = 60};
    CHECK(engine_post_command(engine, &off), "note-off rejected on overload");
    EngineCommand play = {.type = ENGINE_CMD_PLAY};
    CHECK(!engine_post_command(engine, &play), "play passed pending safety barrier");
    CHECK(!engine_post_command(engine, &cmd), "parameter traffic can starve safety");
    atomic_store(&engine->transport_playing, true);
    engine_process_commands(engine);
    CHECK(!engine_transport_is_playing(engine), "emergency stop not applied");
    CHECK(atomic_load(&engine->command_safety_pending) == 0, "safety not acknowledged");
    CHECK(engine_post_command(engine, &cmd), "admission not recovered");
    engine_process_commands(engine);
    EngineCommandStats stats;
    CHECK(engine_get_command_stats(engine, &stats), "stats unavailable");
    CHECK(stats.safety_fallbacks > 0 && stats.rejected > 0, "overflow unobservable");

    SDL_Thread* wrong = SDL_CreateThread(post_from_wrong_thread, "wrong_producer", engine);
    CHECK(wrong != NULL, "wrong producer thread");
    int result = 1;
    SDL_WaitThread(wrong, &result);
    CHECK(result == 0, "multiple producers admitted");
}

// Verifies queued graph adoption, incompatible rejection, and cancellation of owned payloads.
static void test_graph_payloads(Engine* engine) {
    EngineGraph* graph = engine_graph_create(engine->config.sample_rate, 2, engine->config.block_size);
    CHECK(graph != NULL && engine_queue_graph_swap(engine, graph), "queue graph");
    engine_process_commands(engine);
    CHECK(engine_render_source_graph(engine) == graph, "queued graph ignored");
    CHECK(!engine_queue_graph_swap(engine, engine_graph_create(12345, 1, 16)), "incompatible graph accepted");
    for (int i = 0; i < 200; ++i) {
        graph = engine_graph_create(engine->config.sample_rate, 2, engine->config.block_size);
        CHECK(graph != NULL, "graph allocation");
        (void)engine_queue_graph_swap(engine, graph);
    }
    engine_cancel_commands(engine);
    CHECK(ringbuf_available_read(&engine->command_queue) == 0, "cancel left payloads queued");
    engine_source_plan_collect(engine);
}

// Checks frame alignment and flush boundaries without rewinding a consumer or discarding new audio.
static void test_audio_queue_flush(void) {
    AudioQueue queue = {0};
    CHECK(audio_queue_init(&queue, 3, 4), "three-channel queue init");
    float old[15], fresh[15], output[15];
    for (int i = 0; i < 15; ++i) { old[i] = 1.0f; fresh[i] = 2.0f; }
    CHECK(audio_queue_write(&queue, old, 4) == 4, "old frames write");
    size_t head = atomic_load(&queue.buffer.head);
    audio_queue_clear(&queue);
    CHECK(atomic_load(&queue.buffer.head) == head && atomic_load(&queue.buffer.tail) == 0,
          "producer flush modified live reader indices");
    CHECK(audio_queue_write(&queue, fresh, 2) == 1, "partial frame capacity admitted");
    CHECK(audio_queue_read(&queue, output, 5) == 1, "flush discarded new audio or retained old audio");
    for (int i = 0; i < 3; ++i) CHECK(output[i] == 2.0f, "flush output contents");
    for (int n = 0; n < 1000; ++n) {
        CHECK(audio_queue_write(&queue, fresh, 5) == 5, "whole-frame wrapped write");
        CHECK(audio_queue_read(&queue, output, 5) == 5, "old flush rewound reader");
        for (int i = 0; i < 15; ++i) CHECK(output[i] == 2.0f, "wrapped frame corruption");
    }
    audio_queue_free(&queue);

    AudioFlushStress stress = {0};
    atomic_init(&stress.done, false);
    atomic_init(&stress.failed, false);
    CHECK(audio_queue_init(&stress.queue, 3, 127), "flush stress init");
    SDL_Thread* reader = SDL_CreateThread(consume_audio, "audio_flush", &stress);
    CHECK(reader != NULL, "flush reader create");
    for (int frame = 1; frame <= 100000; ++frame) {
        float value[3] = {(float)frame, (float)frame, (float)frame};
        if (frame % 13 == 0) audio_queue_clear(&stress.queue);
        while (audio_queue_write(&stress.queue, value, 1) == 0) SDL_Delay(0);
    }
    atomic_store(&stress.done, true);
    SDL_WaitThread(reader, NULL);
    CHECK(!atomic_load(&stress.failed), "concurrent flush corrupted frames or rewound reader");
    audio_queue_free(&stress.queue);
}

// Verifies queued audition cannot target a replacement track and follows an inserted track's movement.
static void test_audition_identity(Engine* engine) {
    EngineCommand command = {.type = ENGINE_CMD_MIDI_AUDITION_NOTE_ON};
    command.payload.midi_audition.track_index = 0;
    command.payload.midi_audition.track_runtime_id = engine->tracks[0].runtime_id;
    command.payload.midi_audition.preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    command.payload.midi_audition.params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    command.payload.midi_audition.note = 60;
    command.payload.midi_audition.velocity = 1.0f;
    CHECK(engine_post_command(engine, &command), "queue original track note");
    CHECK(engine_remove_track(engine, 0) && engine_insert_track(engine, 0), "replace note target");
    engine_process_commands(engine);
    CHECK(engine->midi_audition_notes.note_count == 0, "old note targeted replacement track");
    command.payload.midi_audition.track_runtime_id = engine->tracks[0].runtime_id;
    CHECK(engine_post_command(engine, &command), "queue surviving track note");
    CHECK(engine_insert_track(engine, 0), "shift note target");
    engine_process_commands(engine);
    CHECK(engine->midi_audition_notes.note_count == 1 && engine->midi_audition_track_index == 1,
          "note failed to follow track identity");
    CHECK(engine_remove_track(engine, 1), "remove active audition track");
    CHECK(engine->midi_audition_notes.note_count == 0, "removed track left audition voices active");
}

// Runs deterministic overflow tests and a real concurrent FIFO stress test without devices.
// Coordinates serial-tagged playback requests with a real command consumer.
typedef struct PlaybackStress {
    Engine* engine;
    atomic_bool done;
    atomic_bool failed;
} PlaybackStress;

// Checks snapshot invariants and monotonic acknowledgements while applying concurrent requests.
static int consume_playback(void* userdata) {
    PlaybackStress* stress = userdata;
    uint64_t previous = 0;
    for (;;) {
        engine_process_commands(stress->engine);
        EnginePlaybackSnapshot view = engine_transport_get_playback_snapshot(stress->engine);
        if (view.applied_serial < previous || view.requested_serial < view.applied_serial ||
            view.pending != (view.requested_serial > view.applied_serial) ||
            (!view.pending && view.requested_playing != view.applied_playing)) atomic_store(&stress->failed, true);
        previous = view.applied_serial;
        if (atomic_load(&stress->done) && ringbuf_available_read(&stress->engine->command_queue) == 0 &&
            atomic_load(&stress->engine->command_safety_pending) == 0) break;
        SDL_Delay(0);
    }
    return 0;
}

// Exercises rapid intent/application races and overload acknowledgement with independent producer and consumer.
static void test_playback_snapshot_stress(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine != NULL, "playback stress fixture");
    PlaybackStress stress = {.engine = engine};
    atomic_init(&stress.done, false);
    atomic_init(&stress.failed, false);
    engine->device_started = true;
    engine->worker_thread = SDL_CreateThread(consume_playback, "playback_snapshot", &stress);
    CHECK(engine->worker_thread != NULL, "playback stress thread");
    for (int i = 1; i <= 10000; ++i) {
        while (!(i & 1 ? engine_transport_play(engine) : engine_transport_stop(engine))) SDL_Delay(0);
    }
    atomic_store(&stress.done, true);
    SDL_WaitThread(engine->worker_thread, NULL);
    engine->worker_thread = NULL;
    engine->device_started = false;
    EnginePlaybackSnapshot view = engine_transport_get_playback_snapshot(engine);
    CHECK(!atomic_load(&stress.failed) && !view.pending && !view.applied_playing && view.applied_serial == 10000,
          "concurrent playback acknowledgements inconsistent");
    engine_destroy(engine);
}

// Distinguishes live command acceptance from application while preserving synchronous offline transport.
static void test_transport_application(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine != NULL, "transport application fixture");
    CHECK(engine_transport_play(engine) && engine_transport_is_playing(engine), "offline play not applied");
    CHECK(engine_transport_stop(engine) && !engine_transport_is_playing(engine), "offline stop not applied");
    engine->device_started = true;
    engine->worker_thread = (SDL_Thread*)engine;
    CHECK(engine_transport_play(engine), "live play rejected");
    CHECK(!engine_transport_is_playing(engine), "accepted play appeared applied before consumption");
    EnginePlaybackSnapshot snapshot = engine_transport_get_playback_snapshot(engine);
    CHECK(snapshot.pending && snapshot.requested_playing && !snapshot.applied_playing &&
          snapshot.requested_serial > snapshot.applied_serial, "pending play snapshot incorrect");
    CHECK(engine_transport_requested_playing(engine), "toggle cannot see accepted play intent");
    engine_process_commands(engine);
    CHECK(engine_transport_is_playing(engine), "consumed play not reflected");
    CHECK(engine_transport_stop(engine), "live stop rejected");
    CHECK(engine_transport_is_playing(engine), "accepted stop appeared applied before consumption");
    engine_process_commands(engine);
    CHECK(!engine_transport_is_playing(engine), "consumed stop not reflected");
    snapshot = engine_transport_get_playback_snapshot(engine);
    CHECK(!snapshot.pending && !snapshot.applied_playing && !snapshot.requested_playing &&
          snapshot.requested_serial == snapshot.applied_serial, "stop acknowledgement missing");
    CHECK(engine_transport_play(engine) && engine_transport_stop(engine), "rapid toggle submission failed");
    snapshot = engine_transport_get_playback_snapshot(engine);
    CHECK(snapshot.pending && !snapshot.requested_playing && !snapshot.applied_playing,
          "rapid toggle lost pending command ordering");
    engine_process_commands(engine);
    CHECK(!engine_transport_get_playback_snapshot(engine).pending, "rapid toggle not acknowledged");
    CHECK(engine_transport_play(engine), "safety fixture play");
    engine_process_commands(engine);
    EngineCommand seek = {.type = ENGINE_CMD_SEEK};
    while (engine_post_command(engine, &seek)) { }
    for (int i = 0; i < 10; ++i) CHECK(engine_transport_stop(engine), "safety stop rejected");
    snapshot = engine_transport_get_playback_snapshot(engine);
    uint64_t accepted_serial = snapshot.requested_serial;
    CHECK(snapshot.pending && !snapshot.requested_playing && snapshot.applied_playing, "safety stop intent missing");
    CHECK(!engine_transport_play(engine), "play admitted during safety stop");
    CHECK(engine_transport_get_playback_snapshot(engine).requested_serial == accepted_serial,
          "rejected play changed requested serial");
    engine_process_commands(engine);
    snapshot = engine_transport_get_playback_snapshot(engine);
    CHECK(!snapshot.pending && !snapshot.applied_playing && snapshot.applied_serial == accepted_serial,
          "safety stop did not acknowledge latest accepted intent");
    engine->worker_thread = NULL;
    engine->device_started = false;
    engine_destroy(engine);
}

int main(void) {
    test_transport_application();
    test_playback_snapshot_stress();
    test_packet_boundaries();
    test_audio_queue_flush();
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    Engine* engine = engine_create(&cfg);
    CHECK(engine != NULL, "engine create");
    test_engine_admission(engine);
    test_tempo_delivery(engine);
    test_graph_payloads(engine);
    test_audition_identity(engine);
    DeliveryStress stress = {.engine = engine, .count = 100000};
    atomic_init(&stress.failed, false);
    SDL_Thread* consumer = SDL_CreateThread(consume_commands, "command_consumer", &stress);
    CHECK(consumer != NULL, "consumer thread");
    for (int i = 1; i <= stress.count; ++i) {
        EngineCommand cmd = {.type = ENGINE_CMD_SEEK, .payload.seek.frame = (uint64_t)i};
        while (!engine_post_command(engine, &cmd)) SDL_Delay(0);
    }
    SDL_WaitThread(consumer, NULL);
    CHECK(!atomic_load(&stress.failed), "concurrent FIFO corrupted");
    CHECK(ringbuf_available_read(&engine->command_queue) == 0, "stress residue");
    engine_destroy(engine);
    puts("engine_command_delivery_test: success (100000 concurrent packets)");
    return 0;
}
