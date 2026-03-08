#include "mem_console_prefs.h"

#include <stdio.h>
#include <string.h>

#include "core_pack.h"

typedef struct MemConsoleUiPrefsV1 {
    uint32_t version;
    int32_t theme_preset_id;
    int32_t font_preset_id;
} MemConsoleUiPrefsV1;

enum {
    MEM_CONSOLE_UI_PREFS_VERSION = 1u
};

int mem_console_build_prefs_path(const char *db_path, char *out_path, size_t out_cap) {
    int written = 0;

    if (!db_path || !out_path || out_cap == 0u) {
        return 0;
    }

    written = snprintf(out_path, out_cap, "%s.ui.pack", db_path);
    if (written <= 0 || (size_t)written >= out_cap) {
        if (out_cap > 0u) {
            out_path[0] = '\0';
        }
        return 0;
    }
    return 1;
}

CoreResult mem_console_prefs_load(const char *prefs_path, MemConsoleState *state) {
    CorePackReader reader = {0};
    CorePackChunkInfo chunk = {0};
    CoreResult result;
    MemConsoleUiPrefsV1 prefs;
    FILE *probe = 0;
    int loaded_any = 0;

    if (!prefs_path || !state) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid prefs load request" };
    }

    probe = fopen(prefs_path, "rb");
    if (!probe) {
        return core_result_ok();
    }
    fclose(probe);

    result = core_pack_reader_open(prefs_path, &reader);
    if (result.code != CORE_OK) {
        return result;
    }

    result = core_pack_reader_find_chunk(&reader, "MCFG", 0, &chunk);
    if (result.code != CORE_OK) {
        (void)core_pack_reader_close(&reader);
        return core_result_ok();
    }

    if (chunk.size != (uint64_t)sizeof(prefs)) {
        (void)core_pack_reader_close(&reader);
        return (CoreResult){ CORE_ERR_FORMAT, "invalid mem_console prefs payload size" };
    }

    result = core_pack_reader_read_chunk_data(&reader, &chunk, &prefs, sizeof(prefs));
    if (result.code != CORE_OK) {
        (void)core_pack_reader_close(&reader);
        return result;
    }

    if (prefs.version != MEM_CONSOLE_UI_PREFS_VERSION) {
        (void)core_pack_reader_close(&reader);
        return core_result_ok();
    }

    if (state_set_theme_preset(state, (CoreThemePresetId)prefs.theme_preset_id)) {
        loaded_any = 1;
    }
    if (state_set_font_preset(state, (CoreFontPresetId)prefs.font_preset_id)) {
        loaded_any = 1;
    }

    result = core_pack_reader_close(&reader);
    if (result.code != CORE_OK) {
        return result;
    }

    if (loaded_any) {
        return (CoreResult){ CORE_OK, "prefs loaded" };
    }
    return core_result_ok();
}

CoreResult mem_console_prefs_save(const char *prefs_path, const MemConsoleState *state) {
    CorePackWriter writer = {0};
    CoreResult result;
    MemConsoleUiPrefsV1 prefs = {0};

    if (!prefs_path || !state) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid prefs save request" };
    }

    prefs.version = MEM_CONSOLE_UI_PREFS_VERSION;
    prefs.theme_preset_id = (int32_t)state->theme_preset_id;
    prefs.font_preset_id = (int32_t)state->font_preset_id;

    result = core_pack_writer_open(prefs_path, &writer);
    if (result.code != CORE_OK) {
        return result;
    }

    result = core_pack_writer_add_chunk(&writer, "MCFG", &prefs, (uint64_t)sizeof(prefs));
    if (result.code != CORE_OK) {
        (void)core_pack_writer_close(&writer);
        return result;
    }

    result = core_pack_writer_close(&writer);
    if (result.code != CORE_OK) {
        return result;
    }

    return core_result_ok();
}
