#pragma once
#include "daw/control_bridge.h"
#include <pthread.h>
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
};
// Executes a bounded domain request exclusively on the application's control thread.
char* daw_control_dispatch(DawControlBridge* bridge, struct AppState* state, const char* request, bool* changed);
