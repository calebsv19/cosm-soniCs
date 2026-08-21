#include "app/workspace_authoring/daw_workspace_authoring_profile.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core_pack.h"
#include "core_pane_module.h"
#include "core_pane_snapshot.h"

#define DAW_WAPP_PAYLOAD_SIZE 28u
#define DAW_WAPP_REQUIREMENT_COUNT 4u
#define DAW_WAPP_REQUIREMENT_SIZE 12u

static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8u); }
static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8u); p[2] = (unsigned char)(v >> 16u); p[3] = (unsigned char)(v >> 24u); }
static uint16_t get16(const unsigned char *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8u)); }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u); }
static void put_float(unsigned char *p, float value) { uint32_t bits; memcpy(&bits, &value, sizeof(bits)); put32(p, bits); }
static float get_float(const unsigned char *p) { uint32_t bits = get32(p); float value; memcpy(&value, &bits, sizeof(value)); return value; }

static void daw_wapp_requirements(CorePaneWorkspaceProfileModuleRequirementV1 *out) {
    uint32_t i;
    for (i = 0; i < DAW_WAPP_REQUIREMENT_COUNT; ++i) out[i] = (CorePaneWorkspaceProfileModuleRequirementV1){0x44415701u + i, 1u, 0u, 1u, 0u};
}

static int daw_wapp_validate_requirements(const CorePaneWorkspaceProfileModuleRequirementV1 *requirements) {
    static const char *const keys[] = {"daw_transport", "daw_timeline", "daw_inspector", "daw_library"};
    CorePaneModuleDescriptor entries[DAW_WAPP_REQUIREMENT_COUNT];
    CorePaneModuleProfileRequirement local[DAW_WAPP_REQUIREMENT_COUNT];
    CorePaneModuleRegistry registry;
    CorePaneSnapshotNodeRecordV1 node = {0u, 1u, CORE_PANE_SNAPSHOT_NODE_LEAF, CORE_PANE_SNAPSHOT_AXIS_HORIZONTAL, 0u, 0.0f, UINT32_MAX, UINT32_MAX, 0.0f, 0.0f};
    CorePaneWorkspaceProfileV1 envelope = {0};
    uint32_t i;
    if (!requirements || core_pane_module_registry_init(&registry, entries, DAW_WAPP_REQUIREMENT_COUNT) != CORE_PANE_MODULE_OK) return 0;
    for (i = 0; i < DAW_WAPP_REQUIREMENT_COUNT; ++i) {
        CorePaneModuleDescriptor descriptor = {0x44415701u + i, keys[i], keys[i], 1u, 0u, 1u, 0u, 0u, 0u, CORE_PANE_MODULE_PROVIDER_INTERNAL, NULL, NULL, NULL};
        if (core_pane_module_register(&registry, &descriptor) != CORE_PANE_MODULE_OK) return 0;
        local[i] = (CorePaneModuleProfileRequirement){requirements[i].module_type_id, requirements[i].min_version_major, requirements[i].min_version_minor, requirements[i].state_schema_major, requirements[i].state_schema_minor};
    }
    if (core_pane_module_validate_profile_requirements(&registry, local, DAW_WAPP_REQUIREMENT_COUNT) != CORE_PANE_MODULE_OK) return 0;
    memcpy(envelope.meta.host_id, "daw", sizeof("daw"));
    envelope.meta.schema_major = CORE_PANE_WORKSPACE_PROFILE_SCHEMA_MAJOR_V1;
    envelope.meta.schema_minor = CORE_PANE_WORKSPACE_PROFILE_SCHEMA_MINOR_V1;
    envelope.meta.host_version_major = DAW_WORKSPACE_AUTHORING_PROFILE_SCHEMA_MAJOR;
    envelope.meta.module_requirement_count = DAW_WAPP_REQUIREMENT_COUNT;
    envelope.snapshot.meta = (CorePaneSnapshotMetaV1){CORE_PANE_SNAPSHOT_SCHEMA_MAJOR_V1, CORE_PANE_SNAPSHOT_SCHEMA_MINOR_V1, 0u, 0u, 0u, 0u, 1u, 0u, 0u};
    envelope.snapshot.nodes = &node; envelope.module_requirements = requirements;
    return core_pane_workspace_profile_validate_v1(&envelope) == CORE_PANE_SNAPSHOT_OK;
}

static int daw_wapp_projection_valid(const DawWorkspaceAuthoringProjection *p) {
    return p && p->focus_pane < DAW_WAPP_REQUIREMENT_COUNT && isfinite(p->transport_ratio) && isfinite(p->library_ratio) && isfinite(p->mixer_ratio) && p->transport_ratio >= 0.05f && p->transport_ratio <= 0.70f && p->library_ratio >= 0.05f && p->library_ratio <= 0.70f && p->mixer_ratio >= 0.05f && p->mixer_ratio <= 0.70f;
}

DawWorkspaceAuthoringProfileResult daw_workspace_authoring_profile_export_file(const char *path, const DawWorkspaceAuthoringProjection *p) {
    unsigned char payload[DAW_WAPP_PAYLOAD_SIZE] = {0}, requirements_bytes[DAW_WAPP_REQUIREMENT_COUNT * DAW_WAPP_REQUIREMENT_SIZE];
    CorePaneWorkspaceProfileModuleRequirementV1 requirements[DAW_WAPP_REQUIREMENT_COUNT]; CorePackWriter writer; CoreResult r; uint32_t i;
    if (!path || !daw_wapp_projection_valid(p)) return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_INVALID_ARG;
    daw_wapp_requirements(requirements); if (!daw_wapp_validate_requirements(requirements)) return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_REQUIREMENTS;
    put16(payload, DAW_WORKSPACE_AUTHORING_PROFILE_SCHEMA_MAJOR); put16(payload + 2u, DAW_WORKSPACE_AUTHORING_PROFILE_SCHEMA_MINOR);
    payload[4] = p->library_visible ? 1u : 0u; payload[5] = p->inspector_visible ? 1u : 0u; payload[6] = p->focus_pane; put16(payload + 8u, DAW_WAPP_REQUIREMENT_COUNT);
    put_float(payload + 12u, p->transport_ratio); put_float(payload + 16u, p->library_ratio); put_float(payload + 20u, p->mixer_ratio);
    for (i = 0; i < DAW_WAPP_REQUIREMENT_COUNT; ++i) { unsigned char *b = requirements_bytes + i * DAW_WAPP_REQUIREMENT_SIZE; put32(b, requirements[i].module_type_id); put16(b + 4u, requirements[i].min_version_major); put16(b + 6u, requirements[i].min_version_minor); put16(b + 8u, requirements[i].state_schema_major); put16(b + 10u, requirements[i].state_schema_minor); }
    r = core_pack_writer_open(path, &writer); if (r.code != CORE_OK) return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_IO;
    r = core_pack_writer_add_chunk(&writer, "DWAP", payload, sizeof(payload)); if (r.code == CORE_OK) r = core_pack_writer_add_chunk(&writer, "DMOD", requirements_bytes, sizeof(requirements_bytes));
    if (core_pack_writer_close(&writer).code != CORE_OK && r.code == CORE_OK) r.code = CORE_ERR_IO;
    return r.code == CORE_OK ? DAW_WORKSPACE_AUTHORING_PROFILE_OK : DAW_WORKSPACE_AUTHORING_PROFILE_ERR_IO;
}

DawWorkspaceAuthoringProfileResult daw_workspace_authoring_profile_import_file(const char *path, DawWorkspaceAuthoringProjection *out) {
    CorePackReader reader; CorePackChunkInfo a, b; unsigned char payload[DAW_WAPP_PAYLOAD_SIZE], bytes[DAW_WAPP_REQUIREMENT_COUNT * DAW_WAPP_REQUIREMENT_SIZE]; CorePaneWorkspaceProfileModuleRequirementV1 requirements[DAW_WAPP_REQUIREMENT_COUNT]; DawWorkspaceAuthoringProjection decoded = {0}; uint32_t i;
    if (!path || !out) return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_INVALID_ARG;
    if (core_pack_reader_open(path, &reader).code != CORE_OK) return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_CONTAINER;
    if (core_pack_reader_chunk_count(&reader) != 2u || core_pack_reader_find_chunk(&reader, "DWAP", 0u, &a).code != CORE_OK || core_pack_reader_find_chunk(&reader, "DMOD", 0u, &b).code != CORE_OK || a.size != sizeof(payload) || b.size != sizeof(bytes) || core_pack_reader_read_chunk_data(&reader, &a, payload, sizeof(payload)).code != CORE_OK || core_pack_reader_read_chunk_data(&reader, &b, bytes, sizeof(bytes)).code != CORE_OK) { (void)core_pack_reader_close(&reader); return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_CONTAINER; }
    (void)core_pack_reader_close(&reader);
    if (get16(payload) != 1u || get16(payload + 2u) != 0u || payload[4] > 1u || payload[5] > 1u || payload[7] != 0u || get16(payload + 8u) != DAW_WAPP_REQUIREMENT_COUNT || get32(payload + 24u) != 0u) return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_SCHEMA;
    decoded.library_visible = payload[4]; decoded.inspector_visible = payload[5]; decoded.focus_pane = payload[6]; decoded.transport_ratio = get_float(payload + 12u); decoded.library_ratio = get_float(payload + 16u); decoded.mixer_ratio = get_float(payload + 20u);
    if (!daw_wapp_projection_valid(&decoded)) return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_PROJECTION;
    for (i = 0; i < DAW_WAPP_REQUIREMENT_COUNT; ++i) { const unsigned char *b0 = bytes + i * DAW_WAPP_REQUIREMENT_SIZE; requirements[i] = (CorePaneWorkspaceProfileModuleRequirementV1){get32(b0), get16(b0 + 4u), get16(b0 + 6u), get16(b0 + 8u), get16(b0 + 10u)}; }
    if (!daw_wapp_validate_requirements(requirements)) return DAW_WORKSPACE_AUTHORING_PROFILE_ERR_REQUIREMENTS;
    *out = decoded; return DAW_WORKSPACE_AUTHORING_PROFILE_OK;
}

int daw_workspace_authoring_profile_default_path(char *out, size_t cap) { return out && cap && snprintf(out, cap, "config/runtime/workspace_authoring.wapp") < (int)cap; }
const char *daw_workspace_authoring_profile_result_string(DawWorkspaceAuthoringProfileResult r) { static const char *const n[] = {"ok","invalid_arg","io","container","schema","requirements","projection"}; return r <= DAW_WORKSPACE_AUTHORING_PROFILE_ERR_PROJECTION ? n[r] : "unknown"; }
