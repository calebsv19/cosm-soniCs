#include "app/workspace_authoring/daw_workspace_authoring_session_adapter.h"

void daw_workspace_authoring_session_adapter_init(
    DawWorkspaceAuthoringSessionAdapter *adapter,
    void *context,
    const CoreWorkspaceAuthoringSessionHooks *hooks) {
    if (!adapter) return;
    core_workspace_authoring_session_init(
        &adapter->session,
        CORE_WORKSPACE_AUTHORING_CAP_FONT_THEME_DRAFT |
            CORE_WORKSPACE_AUTHORING_CAP_SAFE_RUNTIME_GATE,
        context,
        hooks);
}

int daw_workspace_authoring_session_adapter_active(const DawWorkspaceAuthoringSessionAdapter *adapter) {
    return adapter && core_workspace_authoring_session_authoring_active(&adapter->session);
}

int daw_workspace_authoring_session_adapter_runtime_mutation_allowed(
    const DawWorkspaceAuthoringSessionAdapter *adapter) {
    return adapter && core_workspace_authoring_session_runtime_mutation_allowed(&adapter->session);
}

CoreWorkspaceAuthoringSessionOutcome daw_workspace_authoring_session_adapter_enter(
    DawWorkspaceAuthoringSessionAdapter *adapter) {
    return adapter ? core_workspace_authoring_session_enter(&adapter->session)
                   : CORE_WORKSPACE_AUTHORING_SESSION_OUTCOME_REJECTED;
}

CoreWorkspaceAuthoringSessionOutcome daw_workspace_authoring_session_adapter_apply(
    DawWorkspaceAuthoringSessionAdapter *adapter) {
    return adapter ? core_workspace_authoring_session_apply(&adapter->session)
                   : CORE_WORKSPACE_AUTHORING_SESSION_OUTCOME_REJECTED;
}

CoreWorkspaceAuthoringSessionOutcome daw_workspace_authoring_session_adapter_cancel(
    DawWorkspaceAuthoringSessionAdapter *adapter) {
    return adapter ? core_workspace_authoring_session_cancel(&adapter->session)
                   : CORE_WORKSPACE_AUTHORING_SESSION_OUTCOME_REJECTED;
}
