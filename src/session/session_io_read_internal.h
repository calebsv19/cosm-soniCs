#pragma once

#include "session.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char* data;
    size_t length;
    size_t pos;
} JsonReader;

void json_reader_init(JsonReader* r, const char* data, size_t length);
void json_skip_whitespace(JsonReader* r);
bool json_expect(JsonReader* r, char expected);
bool json_match_literal(JsonReader* r, const char* literal);
bool json_parse_bool(JsonReader* r, bool* out_value);
bool json_parse_string(JsonReader* r, char* dst, size_t dst_len);
bool json_parse_number(JsonReader* r, double* out_value);
bool json_skip_value(JsonReader* r);

bool parse_master_fx(JsonReader* r, SessionDocument* doc);
bool parse_session_document_engine(JsonReader* r, SessionDocument* doc);
bool parse_session_document_effects_panel(JsonReader* r, SessionDocument* doc);
bool parse_session_automation_lanes(JsonReader* r, SessionAutomationLane** out_lanes, int* out_lane_count);
bool parse_session_track_clips(JsonReader* r, SessionTrack* track);
bool parse_session_track_fx(JsonReader* r, SessionTrack* track);
bool parse_session_tracks(JsonReader* r, SessionDocument* doc);
bool parse_session_document(JsonReader* r, SessionDocument* doc);
