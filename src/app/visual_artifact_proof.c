#include "app/visual_artifact_proof.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef enum DawVisualArtifactProofState {
    DAW_VISUAL_ARTIFACT_UNINITIALIZED = 0,
    DAW_VISUAL_ARTIFACT_DISABLED,
    DAW_VISUAL_ARTIFACT_READY,
    DAW_VISUAL_ARTIFACT_REQUESTED,
    DAW_VISUAL_ARTIFACT_DONE,
    DAW_VISUAL_ARTIFACT_FAILED
} DawVisualArtifactProofState;

static DawVisualArtifactProofState s_state = DAW_VISUAL_ARTIFACT_UNINITIALIZED;
static char s_output_path[512] = "visual_artifacts/daw_first_frame.bmp";

static bool parse_bool_env(const char *value, bool *out) {
    if (!value || !value[0] || !out) {
        return false;
    }
    if (strcmp(value, "1") == 0 ||
        strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(value, "0") == 0 ||
        strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static void ensure_initialized(void) {
    if (s_state != DAW_VISUAL_ARTIFACT_UNINITIALIZED) {
        return;
    }

    bool enabled = false;
    const char *enabled_env = getenv("DAW_VISUAL_ARTIFACT_ONCE");
    if (!parse_bool_env(enabled_env, &enabled) || !enabled) {
        s_state = DAW_VISUAL_ARTIFACT_DISABLED;
        return;
    }

    const char *path_env = getenv("DAW_VISUAL_ARTIFACT_PATH");
    if (path_env && path_env[0]) {
        snprintf(s_output_path, sizeof(s_output_path), "%s", path_env);
    }
    s_state = DAW_VISUAL_ARTIFACT_READY;
}

static bool output_file_ready(void) {
    struct stat st;
    if (stat(s_output_path, &st) != 0) {
        return false;
    }
    return st.st_size > 0;
}

bool daw_visual_artifact_proof_enabled(void) {
    ensure_initialized();
    return s_state != DAW_VISUAL_ARTIFACT_DISABLED;
}

bool daw_visual_artifact_proof_should_force_render(void) {
    ensure_initialized();
    return s_state == DAW_VISUAL_ARTIFACT_READY ||
           s_state == DAW_VISUAL_ARTIFACT_REQUESTED;
}

void daw_visual_artifact_proof_begin_frame(AppContext *ctx) {
    ensure_initialized();
    if (s_state != DAW_VISUAL_ARTIFACT_READY) {
        return;
    }
    if (!ctx || !ctx->renderer) {
        fprintf(stderr,
                "[DawVisualArtifact] renderer unavailable for %s\n",
                s_output_path);
        s_state = DAW_VISUAL_ARTIFACT_FAILED;
        return;
    }

    VkResult result = vk_renderer_request_capture(ctx->renderer, s_output_path);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "[DawVisualArtifact] capture request failed for %s: %d\n",
                s_output_path,
                result);
        s_state = DAW_VISUAL_ARTIFACT_FAILED;
        return;
    }

    s_state = DAW_VISUAL_ARTIFACT_REQUESTED;
}

void daw_visual_artifact_proof_after_render(AppContext *ctx) {
    ensure_initialized();
    if (s_state == DAW_VISUAL_ARTIFACT_FAILED) {
        if (ctx) {
            ctx->quit = true;
        }
        return;
    }
    if (s_state != DAW_VISUAL_ARTIFACT_REQUESTED) {
        return;
    }

    if (!output_file_ready()) {
        fprintf(stderr,
                "[DawVisualArtifact] capture output missing or empty: %s\n",
                s_output_path);
        s_state = DAW_VISUAL_ARTIFACT_FAILED;
        if (ctx) {
            ctx->quit = true;
        }
        return;
    }

    fprintf(stderr, "[DawVisualArtifact] wrote %s\n", s_output_path);
    s_state = DAW_VISUAL_ARTIFACT_DONE;
    if (ctx) {
        ctx->quit = true;
    }
}

int daw_visual_artifact_proof_exit_code(void) {
    ensure_initialized();
    return s_state == DAW_VISUAL_ARTIFACT_FAILED ? 1 : 0;
}

const char *daw_visual_artifact_proof_path(void) {
    ensure_initialized();
    return s_output_path;
}
