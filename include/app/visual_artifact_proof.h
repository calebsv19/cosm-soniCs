#ifndef DAW_VISUAL_ARTIFACT_PROOF_H
#define DAW_VISUAL_ARTIFACT_PROOF_H

#include <stdbool.h>

#include "sdl_app_framework.h"

bool daw_visual_artifact_proof_enabled(void);
bool daw_visual_artifact_proof_should_force_render(void);
void daw_visual_artifact_proof_begin_frame(AppContext *ctx);
void daw_visual_artifact_proof_after_render(AppContext *ctx);
int daw_visual_artifact_proof_exit_code(void);
const char *daw_visual_artifact_proof_path(void);

#endif
