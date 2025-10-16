#pragma once

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SESSION_DOCUMENT_VERSION 1
#define SESSION_PATH_MAX 512
#define SESSION_NAME_MAX 128

typedef struct {
    char media_path[SESSION_PATH_MAX];
    char name[SESSION_NAME_MAX];
    uint64_t start_frame;
    uint64_t duration_frames;
    uint64_t offset_frames;
    float gain;
    bool selected;
} SessionClip;

typedef struct {
    char name[SESSION_NAME_MAX];
    float gain;
    bool muted;
    bool solo;
    int clip_count;
    SessionClip* clips;
} SessionTrack;

typedef struct {
    bool enabled;
    uint64_t start_frame;
    uint64_t end_frame;
} SessionLoopState;

typedef struct {
    float visible_seconds;
    float vertical_scale;
    bool show_all_grid_lines;
    uint64_t playhead_frame;
} SessionTimelineView;

typedef struct {
    float transport_ratio;
    float library_ratio;
    float mixer_ratio;
} SessionLayoutState;

typedef struct {
    char directory[SESSION_PATH_MAX];
    int selected_index;
} SessionLibraryState;

typedef struct {
    uint32_t version;
    EngineRuntimeConfig engine;
    SessionLoopState loop;
    SessionTimelineView timeline;
    SessionLayoutState layout;
    SessionLibraryState library;
    bool transport_playing;
    uint64_t transport_frame;
    SessionTrack* tracks;
    int track_count;
} SessionDocument;

struct AppState;

void session_document_init(SessionDocument* doc);
void session_document_reset(SessionDocument* doc);
void session_document_free(SessionDocument* doc);
bool session_document_capture(const struct AppState* state, SessionDocument* out_doc);
bool session_document_validate(const SessionDocument* doc, char* error_message, size_t error_message_len);
bool session_document_write_file(const SessionDocument* doc, const char* path);
bool session_save_to_file(const struct AppState* state, const char* path);
bool session_document_read_file(const char* path, SessionDocument* out_doc);
bool session_apply_document(struct AppState* state, const SessionDocument* doc);
bool session_load_from_file(struct AppState* state, const char* path);
