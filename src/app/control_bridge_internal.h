#pragma once
#include "daw/control_bridge.h"
#include <pthread.h>
#include "daw/midi_actions.h"
#include "daw/midi_regions.h"
#include "daw/fx_actions.h"
#include "daw/composition.h"
#include "daw/arrangement.h"
#include "daw/routing_actions.h"
#define DAW_CONTROL_ROUTING_STATES 4
// Owns complete routing guards behind bounded endpoint-local tokens.
typedef struct DawControlRoutingState { char token[80]; EngineRoutingSnapshot snapshot; } DawControlRoutingState;
#define DAW_CONTROL_ARRANGEMENT_STATES 4
// Owns full authored arrangement guards behind bounded endpoint tokens.
typedef struct DawControlArrangementState { char token[80]; DawArrangementSnapshot* snapshot; } DawControlArrangementState;
#define DAW_CONTROL_COMPOSITION_STATES 4
#define DAW_CONTROL_COMPOSITION_BYTES UINT64_C(67108864)
// Owns immutable production audio and measurements behind a bounded endpoint-lifetime token.
typedef struct DawControlCompositionState { char token[80]; uint64_t sequence, bytes; DawCompositionRender* render; } DawControlCompositionState;
#define DAW_CONTROL_FX_STATES 8
// Retains exact complete effect chains behind endpoint-lifetime state tokens.
typedef struct DawControlFxState { char token[80]; DawFxSnapshot snapshot; } DawControlFxState;
#define DAW_CONTROL_LAYOUT_STATES 4
// Retains one bounded copied placement snapshot behind an endpoint-lifetime layout token.
typedef struct DawControlLayoutState { char token[80]; DawMidiLayout* snapshot; } DawControlLayoutState;
#define DAW_CONTROL_MIDI_STATES 8
#define DAW_CONTROL_MIDI_NOTE_PAGE 128
#define DAW_CONTROL_MIDI_TEMPO_PAGE 64
#define DAW_CONTROL_MIDI_WRITE_NOTES 64
// Retains an exact copied MIDI state behind a short endpoint-lifetime token.
typedef struct DawControlMidiState { char token[80]; DawMidiSnapshot snapshot; } DawControlMidiState;
#include <stdint.h>
#include <sys/types.h>
#define DAW_CONTROL_MAX_REQUEST 16384
#define DAW_CONTROL_MAX_RESPONSE 131072
#define DAW_CONTROL_RECEIPTS 128
// Retains a request and its terminal response for exact replay during one endpoint lifetime.
typedef struct DawControlReceipt {
    char id[65];
    char* request;
    char* response;
} DawControlReceipt;
// Separates transport-owned buffers from application-owned receipts and engine access.
struct DawControlBridge {
    DawControlRoutingState routing_states[DAW_CONTROL_ROUTING_STATES];
    unsigned routing_cursor;
    uint64_t routing_sequence;
    DawControlCompositionState composition_states[DAW_CONTROL_COMPOSITION_STATES];
    unsigned composition_cursor;
    uint64_t composition_sequence, composition_bytes;
    DawControlFxState fx_states[DAW_CONTROL_FX_STATES];
    unsigned fx_cursor;
    uint64_t fx_sequence;
    DawControlArrangementState arrangement_states[DAW_CONTROL_ARRANGEMENT_STATES];
    unsigned arrangement_cursor;
    uint64_t arrangement_sequence;
    int listener;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    bool stopping;
    bool pending;
    bool allow_write;
    char* request;
    char* response;
    char path[104];
    char instance[33];
    dev_t device;
    ino_t inode;
    DawControlReceipt receipts[DAW_CONTROL_RECEIPTS];
    int receipt_count;
    DawControlMidiState midi_states[DAW_CONTROL_MIDI_STATES];
    unsigned midi_cursor;
    uint64_t midi_sequence;
    DawControlLayoutState layout_states[DAW_CONTROL_LAYOUT_STATES];
    unsigned layout_cursor;
    uint64_t layout_sequence;
};
// Executes a bounded domain request exclusively on the application's control thread.
char* daw_control_dispatch(DawControlBridge* bridge, struct AppState* state, const char* request, bool* changed);

#include "daw/app_actions.h"
struct cJSON;
// Shares the original accepted instrument-state envelope with additive preset actions.
struct cJSON* daw_control_action_json(DawControlBridge*, DawActionResult);
// Routes typed library operations after common endpoint admission and receipt reservation.
struct cJSON* daw_control_analysis(DawControlBridge*, struct AppState*, const struct cJSON*, const DawActionTarget*);
struct cJSON* daw_control_presets(DawControlBridge*, struct AppState*, const struct cJSON*, const DawInstrumentSnapshot*, bool*);

// Routes bounded MIDI pages and receipt-admitted transactions on the application owner.
struct cJSON* daw_control_midi(DawControlBridge*, struct AppState*, const struct cJSON*, DawActionTarget, bool*);

// Adapts copied placement and guarded structural region history after common admission.
struct cJSON* daw_control_regions(DawControlBridge*, struct AppState*, const struct cJSON*, const DawActionTarget*, bool*);

// Routes effect discovery and guarded chain actions on the owner thread.
struct cJSON* daw_control_fx(DawControlBridge*, struct AppState*, const struct cJSON*, const DawActionTarget*, bool*);

// Adapts immutable production render analysis and receipt-backed WAV publication.
struct cJSON* daw_control_composition(DawControlBridge*, struct AppState*, const struct cJSON*, const DawActionTarget*);

// Shares copied effect metadata encoding between chain and composition responses.
void daw_control_fx_values_json(struct cJSON*, const FxMasterSnapshot*);

// Shares strict identity parsing with compound arrangement requests.
bool daw_control_parse_target(const struct cJSON*, DawActionTarget*);
// Routes full-state arrangement reads, batches and guarded history.
struct cJSON* daw_control_arrangement(DawControlBridge*, struct AppState*, const struct cJSON*, bool*);

// Adapts common routing reads, creation, edits and typed history after endpoint admission.
struct cJSON* daw_control_routing(DawControlBridge*, struct AppState*, const struct cJSON*, bool*);
