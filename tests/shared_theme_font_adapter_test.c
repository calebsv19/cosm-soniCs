#include "ui/shared_theme_font_adapter.h"

#include "core_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* msg) {
    fprintf(stderr, "shared_theme_font_adapter_test: %s\n", msg);
    exit(1);
}

static void expect(int cond, const char* msg) {
    if (!cond) {
        fail(msg);
    }
}

int main(void) {
    DawThemePalette palette = {0};
    char font_path[256] = {0};
    char preset_name[64] = {0};
    int point_size = 0;
    int base_point_size = 0;
    int zoomed_point_size = 0;
    int reset_point_size = 0;
    CoreFontPreset preset = {0};
    CoreFontRoleSpec role = {0};
    CoreResult r;
    size_t i;
    const char* theme_presets[] = {
        "studio_blue",
        "harbor_blue",
        "midnight_contrast",
        "soft_light",
        "standard_grey",
        "greyscale"
    };

    unsetenv("DAW_USE_SHARED_THEME_FONT");
    unsetenv("DAW_USE_SHARED_THEME");
    unsetenv("DAW_USE_SHARED_FONT");
    unsetenv("DAW_THEME_PRESET");
    unsetenv("DAW_FONT_PRESET");
    unsetenv("DAW_FONT_ZOOM_STEP");

    setenv("DAW_USE_SHARED_THEME_FONT", "0", 1);
    expect(!daw_shared_theme_resolve_palette(&palette), "theme should default to fallback path");
    setenv("DAW_USE_SHARED_THEME_FONT", "1", 1);

    setenv("DAW_USE_SHARED_FONT", "0", 1);
    expect(!daw_shared_font_resolve_ui_regular(font_path, sizeof(font_path), &point_size),
           "font should default to fallback path");
    setenv("DAW_USE_SHARED_FONT", "1", 1);

    setenv("DAW_THEME_PRESET", "studio_blue", 1);
    setenv("DAW_FONT_PRESET", "daw_default", 1);

    expect(daw_shared_theme_resolve_palette(&palette), "theme should resolve when shared mode is enabled");
    expect(palette.menu_fill.r == 26 && palette.menu_fill.g == 26 && palette.menu_fill.b == 34,
           "menu fill should map to studio_blue surface token");
    expect(daw_shared_theme_title_color().r == 220, "title color should map to primary text token");

    expect(daw_shared_font_resolve_ui_regular(font_path, sizeof(font_path), &point_size),
           "font should resolve when shared mode is enabled");
    expect(point_size > 0, "point size should be positive");
    base_point_size = point_size;
    expect(daw_shared_font_set_zoom_step(2), "zoom step should change from default");
    expect(daw_shared_font_zoom_step() == 2, "zoom step should report current value");
    expect(daw_shared_font_resolve_ui_regular(font_path, sizeof(font_path), &zoomed_point_size),
           "font should resolve after zoom step update");
    expect(zoomed_point_size > base_point_size,
           "positive zoom step should increase resolved point size");
    expect(daw_shared_font_reset_zoom_step(), "zoom step reset should report a change");
    expect(daw_shared_font_zoom_step() == 0, "zoom step should reset to zero");
    expect(daw_shared_font_resolve_ui_regular(font_path, sizeof(font_path), &reset_point_size),
           "font should resolve after zoom reset");
    expect(reset_point_size == base_point_size, "zoom reset should restore base point size");
    (void)daw_shared_font_set_zoom_step(99);
    expect(daw_shared_font_zoom_step() == 5, "zoom step should clamp to max");
    (void)daw_shared_font_set_zoom_step(-99);
    expect(daw_shared_font_zoom_step() == -4, "zoom step should clamp to min");
    (void)daw_shared_font_reset_zoom_step();

    expect(daw_shared_theme_set_preset("midnight_contrast"),
           "runtime preset set should accept known preset");
    expect(daw_shared_theme_current_preset(preset_name, sizeof(preset_name)),
           "runtime preset query should succeed");
    expect(strcmp(preset_name, "midnight_contrast") == 0,
           "runtime preset query should return the set preset");
    expect(daw_shared_theme_cycle_next(), "runtime next-cycle should succeed");
    expect(daw_shared_theme_current_preset(preset_name, sizeof(preset_name)),
           "runtime preset query should succeed after cycling next");
    expect(strcmp(preset_name, "soft_light") == 0,
           "runtime next-cycle should move to soft_light");
    expect(daw_shared_theme_cycle_prev(), "runtime prev-cycle should succeed");
    expect(daw_shared_theme_current_preset(preset_name, sizeof(preset_name)),
           "runtime preset query should succeed after cycling prev");
    expect(strcmp(preset_name, "midnight_contrast") == 0,
           "runtime prev-cycle should move back to midnight_contrast");
    expect(!daw_shared_theme_set_preset("missing_preset"),
           "runtime preset set should reject unknown preset");

    for (i = 0; i < sizeof(theme_presets) / sizeof(theme_presets[0]); ++i) {
        setenv("DAW_THEME_PRESET", theme_presets[i], 1);
        expect(daw_shared_theme_resolve_palette(&palette), "theme preset matrix should resolve");
    }

    r = core_font_get_preset(CORE_FONT_PRESET_DAW_DEFAULT, &preset);
    expect(r.code == CORE_OK, "failed to load daw_default font preset");
    r = core_font_resolve_role(&preset, CORE_FONT_ROLE_UI_REGULAR, &role);
    expect(r.code == CORE_OK, "failed to resolve daw ui_regular role");
    expect(strcmp(font_path, role.primary_path) == 0 || strcmp(font_path, role.fallback_path) == 0,
           "resolved font path should be primary or fallback");

    puts("shared_theme_font_adapter_test: success");
    return 0;
}
