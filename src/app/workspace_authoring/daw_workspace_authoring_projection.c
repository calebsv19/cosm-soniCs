#include "app/workspace_authoring/daw_workspace_authoring_projection.h"

static float daw_workspace_authoring_projection_clamp_ratio(float value) {
    if (value < 0.05f) return 0.05f;
    if (value > 0.70f) return 0.70f;
    return value;
}

void daw_workspace_authoring_projection_capture(DawWorkspaceAuthoringProjection *projection,
                                                int library_visible,
                                                int inspector_visible,
                                                float transport_ratio,
                                                float library_ratio,
                                                float mixer_ratio) {
    if (!projection) return;
    projection->baseline_library_visible = library_visible ? 1u : 0u;
    projection->baseline_inspector_visible = inspector_visible ? 1u : 0u;
    projection->baseline_transport_ratio = transport_ratio;
    projection->baseline_library_ratio = library_ratio;
    projection->baseline_mixer_ratio = mixer_ratio;
    projection->baseline_valid = 1u;
    daw_workspace_authoring_projection_restore_baseline(projection);
    projection->focus_pane = 1u;
}

int daw_workspace_authoring_projection_take_action(DawWorkspaceAuthoringProjection *projection,
                                                   DawWorkspaceAuthoringProjectionAction *out_action) {
    if (!projection || projection->pending_action == DAW_WORKSPACE_AUTHORING_PROJECTION_NONE) return 0;
    if (out_action) *out_action = projection->pending_action;
    projection->pending_action = DAW_WORKSPACE_AUTHORING_PROJECTION_NONE;
    return 1;
}

void daw_workspace_authoring_projection_apply_action(DawWorkspaceAuthoringProjection *projection,
                                                     DawWorkspaceAuthoringProjectionAction action) {
    if (!projection || !projection->baseline_valid) return;
    switch (action) {
        case DAW_WORKSPACE_AUTHORING_PROJECTION_TOGGLE_LIBRARY: projection->library_visible = !projection->library_visible; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_TOGGLE_INSPECTOR: projection->inspector_visible = !projection->inspector_visible; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_TRANSPORT_INC: projection->transport_ratio += 0.02f; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_TRANSPORT_DEC: projection->transport_ratio -= 0.02f; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_LIBRARY_INC: projection->library_ratio += 0.02f; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_LIBRARY_DEC: projection->library_ratio -= 0.02f; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_MIXER_INC: projection->mixer_ratio += 0.02f; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_MIXER_DEC: projection->mixer_ratio -= 0.02f; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_FOCUS_TRANSPORT: projection->focus_pane = 0u; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_FOCUS_TIMELINE: projection->focus_pane = 1u; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_FOCUS_INSPECTOR: projection->focus_pane = 2u; break;
        case DAW_WORKSPACE_AUTHORING_PROJECTION_FOCUS_LIBRARY: projection->focus_pane = 3u; break;
        default: break;
    }
    projection->transport_ratio = daw_workspace_authoring_projection_clamp_ratio(projection->transport_ratio);
    projection->library_ratio = daw_workspace_authoring_projection_clamp_ratio(projection->library_ratio);
    projection->mixer_ratio = daw_workspace_authoring_projection_clamp_ratio(projection->mixer_ratio);
}

void daw_workspace_authoring_projection_restore_baseline(DawWorkspaceAuthoringProjection *projection) {
    if (!projection || !projection->baseline_valid) return;
    projection->library_visible = projection->baseline_library_visible;
    projection->inspector_visible = projection->baseline_inspector_visible;
    projection->transport_ratio = projection->baseline_transport_ratio;
    projection->library_ratio = projection->baseline_library_ratio;
    projection->mixer_ratio = projection->baseline_mixer_ratio;
}
