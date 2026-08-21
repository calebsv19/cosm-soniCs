#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "app/workspace_authoring/daw_workspace_authoring_profile.h"
int main(void) { char p[]="/tmp/daw_wapp_XXXXXX"; DawWorkspaceAuthoringProjection a={0},b={0},u={0},e; int fd=mkstemp(p); assert(fd>=0); close(fd); a.focus_pane=3u; a.inspector_visible=1u; a.transport_ratio=.12f; a.library_ratio=.24f; a.mixer_ratio=.31f; assert(daw_workspace_authoring_profile_export_file(p,&a)==DAW_WORKSPACE_AUTHORING_PROFILE_OK); assert(daw_workspace_authoring_profile_import_file(p,&b)==DAW_WORKSPACE_AUTHORING_PROFILE_OK); assert(b.focus_pane==3u && b.library_ratio==.24f); unlink(p); fd=mkstemp(p); assert(fd>=0); assert(write(fd,"bad",3)==3); close(fd); u.focus_pane=2u; u.transport_ratio=u.library_ratio=u.mixer_ratio=.2f; e=u; assert(daw_workspace_authoring_profile_import_file(p,&u)!=DAW_WORKSPACE_AUTHORING_PROFILE_OK); assert(memcmp(&u,&e,sizeof(u))==0); unlink(p); return 0; }
