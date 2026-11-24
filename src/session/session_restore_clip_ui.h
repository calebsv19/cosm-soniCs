#ifndef DAW_SESSION_RESTORE_CLIP_UI_H
#define DAW_SESSION_RESTORE_CLIP_UI_H

#include "session.h"

// Maps a saved clip position to its stable order in the rebuilt engine track.
int session_restored_clip_index(const SessionDocument* doc, int track_index, int clip_index);

// Applies saved selection and MIDI views to the prepared candidate.
void session_restore_candidate_clip_ui(struct AppState* state,
                                       const SessionDocument* doc,
                                       bool selected_from_clip);

#endif
