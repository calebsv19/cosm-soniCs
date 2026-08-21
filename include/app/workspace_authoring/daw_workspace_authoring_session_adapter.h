#ifndef DAW_WORKSPACE_AUTHORING_SESSION_ADAPTER_H
#define DAW_WORKSPACE_AUTHORING_SESSION_ADAPTER_H

#include "core_workspace_authoring_session.h"

typedef struct DawWorkspaceAuthoringSessionAdapter {
    CoreWorkspaceAuthoringSession session;
} DawWorkspaceAuthoringSessionAdapter;

void daw_workspace_authoring_session_adapter_init(
    DawWorkspaceAuthoringSessionAdapter *adapter,
    void *context,
    const CoreWorkspaceAuthoringSessionHooks *hooks);
int daw_workspace_authoring_session_adapter_active(const DawWorkspaceAuthoringSessionAdapter *adapter);
int daw_workspace_authoring_session_adapter_runtime_mutation_allowed(
    const DawWorkspaceAuthoringSessionAdapter *adapter);
CoreWorkspaceAuthoringSessionOutcome daw_workspace_authoring_session_adapter_enter(
    DawWorkspaceAuthoringSessionAdapter *adapter);
CoreWorkspaceAuthoringSessionOutcome daw_workspace_authoring_session_adapter_apply(
    DawWorkspaceAuthoringSessionAdapter *adapter);
CoreWorkspaceAuthoringSessionOutcome daw_workspace_authoring_session_adapter_cancel(
    DawWorkspaceAuthoringSessionAdapter *adapter);

#endif
