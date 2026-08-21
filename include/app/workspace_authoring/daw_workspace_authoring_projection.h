#ifndef DAW_WORKSPACE_AUTHORING_PROJECTION_H
#define DAW_WORKSPACE_AUTHORING_PROJECTION_H

#include <stdint.h>

typedef enum DawWorkspaceAuthoringProjectionAction {
    DAW_WORKSPACE_AUTHORING_PROJECTION_NONE = 0,
    DAW_WORKSPACE_AUTHORING_PROJECTION_TOGGLE_LIBRARY,
    DAW_WORKSPACE_AUTHORING_PROJECTION_TOGGLE_INSPECTOR,
    DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_TRANSPORT_INC,
    DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_TRANSPORT_DEC,
    DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_LIBRARY_INC,
    DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_LIBRARY_DEC,
    DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_MIXER_INC,
    DAW_WORKSPACE_AUTHORING_PROJECTION_RATIO_MIXER_DEC,
    DAW_WORKSPACE_AUTHORING_PROJECTION_FOCUS_TRANSPORT,
    DAW_WORKSPACE_AUTHORING_PROJECTION_FOCUS_TIMELINE,
    DAW_WORKSPACE_AUTHORING_PROJECTION_FOCUS_INSPECTOR,
    DAW_WORKSPACE_AUTHORING_PROJECTION_FOCUS_LIBRARY
} DawWorkspaceAuthoringProjectionAction;

typedef struct DawWorkspaceAuthoringProjection {
    DawWorkspaceAuthoringProjectionAction pending_action;
    uint8_t baseline_valid;
    uint8_t library_visible;
    uint8_t inspector_visible;
    uint8_t focus_pane;
    float transport_ratio;
    float library_ratio;
    float mixer_ratio;
    uint8_t baseline_library_visible;
    uint8_t baseline_inspector_visible;
    float baseline_transport_ratio;
    float baseline_library_ratio;
    float baseline_mixer_ratio;
} DawWorkspaceAuthoringProjection;

void daw_workspace_authoring_projection_capture(DawWorkspaceAuthoringProjection *projection,
                                                int library_visible,
                                                int inspector_visible,
                                                float transport_ratio,
                                                float library_ratio,
                                                float mixer_ratio);
int daw_workspace_authoring_projection_take_action(DawWorkspaceAuthoringProjection *projection,
                                                   DawWorkspaceAuthoringProjectionAction *out_action);
void daw_workspace_authoring_projection_apply_action(DawWorkspaceAuthoringProjection *projection,
                                                     DawWorkspaceAuthoringProjectionAction action);
void daw_workspace_authoring_projection_restore_baseline(DawWorkspaceAuthoringProjection *projection);

#endif
