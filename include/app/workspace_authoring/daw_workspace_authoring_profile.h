#ifndef DAW_WORKSPACE_AUTHORING_PROFILE_H
#define DAW_WORKSPACE_AUTHORING_PROFILE_H

#include <stddef.h>

#include "app/workspace_authoring/daw_workspace_authoring_projection.h"

typedef enum DawWorkspaceAuthoringProfileResult {
    DAW_WORKSPACE_AUTHORING_PROFILE_OK = 0,
    DAW_WORKSPACE_AUTHORING_PROFILE_ERR_INVALID_ARG,
    DAW_WORKSPACE_AUTHORING_PROFILE_ERR_IO,
    DAW_WORKSPACE_AUTHORING_PROFILE_ERR_CONTAINER,
    DAW_WORKSPACE_AUTHORING_PROFILE_ERR_SCHEMA,
    DAW_WORKSPACE_AUTHORING_PROFILE_ERR_REQUIREMENTS,
    DAW_WORKSPACE_AUTHORING_PROFILE_ERR_PROJECTION
} DawWorkspaceAuthoringProfileResult;

#define DAW_WORKSPACE_AUTHORING_PROFILE_SCHEMA_MAJOR 1u
#define DAW_WORKSPACE_AUTHORING_PROFILE_SCHEMA_MINOR 0u

DawWorkspaceAuthoringProfileResult daw_workspace_authoring_profile_export_file(
    const char *path, const DawWorkspaceAuthoringProjection *projection);
DawWorkspaceAuthoringProfileResult daw_workspace_authoring_profile_import_file(
    const char *path, DawWorkspaceAuthoringProjection *out_projection);
int daw_workspace_authoring_profile_default_path(char *out_path, size_t out_capacity);
const char *daw_workspace_authoring_profile_result_string(DawWorkspaceAuthoringProfileResult result);

#endif
