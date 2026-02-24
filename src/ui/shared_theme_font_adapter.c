#include "ui/shared_theme_font_adapter.h"

#include "core_font.h"
#include "core_theme.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool parse_bool_env(const char* value, bool* out_value) {
    char lowered[16];
    size_t i = 0;
    if (!value || !value[0] || !out_value) {
        return false;
    }
    for (; value[i] && i < sizeof(lowered) - 1; ++i) {
        lowered[i] = (char)tolower((unsigned char)value[i]);
    }
    lowered[i] = '\0';

    if (strcmp(lowered, "1") == 0 || strcmp(lowered, "true") == 0 || strcmp(lowered, "yes") == 0 ||
        strcmp(lowered, "on") == 0) {
        *out_value = true;
        return true;
    }
    if (strcmp(lowered, "0") == 0 || strcmp(lowered, "false") == 0 || strcmp(lowered, "no") == 0 ||
        strcmp(lowered, "off") == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

static bool is_shared_toggle_enabled(const char* override_var_name) {
    const char* override = getenv(override_var_name);
    bool out_value = false;
    if (parse_bool_env(override, &out_value)) {
        return out_value;
    }
    if (parse_bool_env(getenv("DAW_USE_SHARED_THEME_FONT"), &out_value)) {
        return out_value;
    }
    return true;
}

static SDL_Color theme_color_or_default(const CoreThemePreset* preset,
                                        CoreThemeColorToken token,
                                        SDL_Color fallback) {
    CoreThemeColor raw = {0};
    CoreResult r = core_theme_get_color(preset, token, &raw);
    if (r.code != CORE_OK) {
        return fallback;
    }
    return (SDL_Color){raw.r, raw.g, raw.b, raw.a};
}

static int stat_path_exists(const char* path, void* user) {
    struct stat st;
    (void)user;
    return path && stat(path, &st) == 0;
}

bool daw_shared_theme_resolve_palette(DawThemePalette* out_palette) {
    const char* preset_name;
    CoreThemePreset preset = {0};
    CoreResult r;
    if (!out_palette || !is_shared_toggle_enabled("DAW_USE_SHARED_THEME")) {
        return false;
    }

    preset_name = getenv("DAW_THEME_PRESET");
    if (!preset_name || !preset_name[0]) {
        preset_name = "daw_default";
    }

    r = core_theme_get_preset_by_name(preset_name, &preset);
    if (r.code != CORE_OK) {
        r = core_theme_get_preset(CORE_THEME_PRESET_DAW_DEFAULT, &preset);
        if (r.code != CORE_OK) {
            return false;
        }
    }

    out_palette->menu_fill =
        theme_color_or_default(&preset, CORE_THEME_COLOR_SURFACE_1, (SDL_Color){26, 26, 34, 255});
    out_palette->timeline_fill =
        theme_color_or_default(&preset, CORE_THEME_COLOR_SURFACE_2, (SDL_Color){32, 32, 40, 255});
    out_palette->inspector_fill =
        theme_color_or_default(&preset, CORE_THEME_COLOR_SURFACE_1, (SDL_Color){28, 28, 36, 255});
    out_palette->library_fill =
        theme_color_or_default(&preset, CORE_THEME_COLOR_SURFACE_0, (SDL_Color){24, 24, 32, 255});
    out_palette->title_text =
        theme_color_or_default(&preset, CORE_THEME_COLOR_TEXT_PRIMARY, (SDL_Color){220, 220, 230, 255});
    return true;
}

SDL_Color daw_shared_theme_title_color(void) {
    DawThemePalette palette = {0};
    if (daw_shared_theme_resolve_palette(&palette)) {
        return palette.title_text;
    }
    return (SDL_Color){220, 220, 230, 255};
}

bool daw_shared_font_resolve_ui_regular(char* out_path, size_t out_path_size, int* out_point_size) {
    const char* preset_name;
    CoreFontPreset preset = {0};
    CoreFontRoleSpec role = {0};
    const char* selected_path = NULL;
    int tier_size = 0;
    CoreResult r;

    if (!out_path || out_path_size == 0 || !out_point_size ||
        !is_shared_toggle_enabled("DAW_USE_SHARED_FONT")) {
        return false;
    }

    preset_name = getenv("DAW_FONT_PRESET");
    if (!preset_name || !preset_name[0]) {
        preset_name = "daw_default";
    }

    r = core_font_get_preset_by_name(preset_name, &preset);
    if (r.code != CORE_OK) {
        r = core_font_get_preset(CORE_FONT_PRESET_DAW_DEFAULT, &preset);
        if (r.code != CORE_OK) {
            return false;
        }
    }

    r = core_font_resolve_role(&preset, CORE_FONT_ROLE_UI_REGULAR, &role);
    if (r.code != CORE_OK) {
        return false;
    }

    r = core_font_choose_path(&role, stat_path_exists, NULL, &selected_path);
    if (r.code != CORE_OK || !selected_path || !selected_path[0]) {
        return false;
    }

    strncpy(out_path, selected_path, out_path_size - 1);
    out_path[out_path_size - 1] = '\0';
    r = core_font_point_size_for_tier(&role, CORE_FONT_TEXT_SIZE_PARAGRAPH, &tier_size);
    if (r.code == CORE_OK && tier_size > 0) {
        *out_point_size = tier_size;
    } else {
        *out_point_size = role.point_size > 0 ? role.point_size : 9;
    }
    return true;
}
