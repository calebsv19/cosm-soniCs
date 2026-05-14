#include "app/workspace_authoring/daw_workspace_authoring_host.h"
#include "ui/shared_theme_font_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static SDL_Event key_event(Uint32 type, SDL_Scancode scancode, SDL_Keycode key, SDL_Keymod mod) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key.type = type;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = key;
    event.key.keysym.mod = mod;
    return event;
}

static SDL_Event mouse_event(Uint32 type, int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button.type = type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    return event;
}

static void test_entry_chord_and_apply(void) {
    DawWorkspaceAuthoringHostState host;
    SDL_Event plain_c = key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c, KMOD_NONE);
    SDL_Event alt_c = key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c, KMOD_ALT);
    SDL_Event alt_v = key_event(SDL_KEYDOWN, SDL_SCANCODE_V, SDLK_v, KMOD_ALT);
    SDL_Event enter = key_event(SDL_KEYDOWN, SDL_SCANCODE_RETURN, SDLK_RETURN, KMOD_NONE);

    daw_workspace_authoring_host_reset(&host);
    assert(!daw_workspace_authoring_host_handle_sdl_event(&host, &plain_c, 0));
    assert(!daw_workspace_authoring_host_active(&host));

    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_c, 0));
    assert(!daw_workspace_authoring_host_active(&host));
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_v, 0));
    assert(daw_workspace_authoring_host_active(&host));
    assert(daw_workspace_authoring_host_pane_overlay_active(&host));
    assert(host.enter_count == 1u);

    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &enter, 0));
    assert(!daw_workspace_authoring_host_active(&host));
    assert(host.apply_count == 1u);
    assert(host.last_event_accepted == 1u);
}

static void test_text_entry_blocks_entry(void) {
    DawWorkspaceAuthoringHostState host;
    SDL_Event alt_c = key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c, KMOD_ALT);
    SDL_Event alt_v = key_event(SDL_KEYDOWN, SDL_SCANCODE_V, SDLK_v, KMOD_ALT);

    daw_workspace_authoring_host_reset(&host);
    assert(!daw_workspace_authoring_host_handle_sdl_event(&host, &alt_c, 1));
    assert(!daw_workspace_authoring_host_handle_sdl_event(&host, &alt_v, 1));
    assert(!daw_workspace_authoring_host_active(&host));
}

static void test_active_capture_and_cancel(void) {
    DawWorkspaceAuthoringHostState host;
    SDL_Event alt_c = key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c, KMOD_ALT);
    SDL_Event alt_v = key_event(SDL_KEYDOWN, SDL_SCANCODE_V, SDLK_v, KMOD_ALT);
    SDL_Event tab = key_event(SDL_KEYDOWN, SDL_SCANCODE_TAB, SDLK_TAB, KMOD_NONE);
    SDL_Event click = mouse_event(SDL_MOUSEBUTTONDOWN, 200, 80);
    SDL_Event esc = key_event(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, KMOD_NONE);

    daw_workspace_authoring_host_reset(&host);
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_c, 0));
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_v, 0));
    assert(daw_workspace_authoring_host_active(&host));

    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &tab, 0));
    assert(daw_workspace_authoring_host_font_theme_overlay_active(&host));
    assert(host.overlay_cycle_count == 1u);

    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &click, 0));
    assert(host.captured_runtime_event_count >= 2u);

    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &esc, 0));
    assert(!daw_workspace_authoring_host_active(&host));
    assert(host.cancel_count == 1u);
    assert(host.last_event_canceled == 1u);
}

static void test_alt_chord_toggles_off_as_cancel(void) {
    DawWorkspaceAuthoringHostState host;
    SDL_Event alt_c = key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c, KMOD_ALT);
    SDL_Event alt_v = key_event(SDL_KEYDOWN, SDL_SCANCODE_V, SDLK_v, KMOD_ALT);
    SDL_Event alt_c_up = key_event(SDL_KEYUP, SDL_SCANCODE_C, SDLK_c, KMOD_ALT);
    SDL_Event alt_v_up = key_event(SDL_KEYUP, SDL_SCANCODE_V, SDLK_v, KMOD_ALT);

    daw_workspace_authoring_host_reset(&host);
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_c, 0));
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_v, 0));
    assert(daw_workspace_authoring_host_active(&host));
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_c_up, 0));
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_v_up, 0));
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_c, 0));
    assert(daw_workspace_authoring_host_handle_sdl_event(&host, &alt_v, 0));
    assert(!daw_workspace_authoring_host_active(&host));
    assert(host.cancel_count == 1u);
}

static void test_font_theme_cancel_restores_entry_baseline(void) {
    DawWorkspaceAuthoringHostState host;
    char preset_name[64];

    (void)daw_shared_theme_set_preset("daw_default");
    (void)daw_shared_font_set_preset("daw_default");
    (void)daw_shared_font_reset_zoom_step();

    daw_workspace_authoring_host_reset(&host);
    assert(daw_workspace_authoring_host_enter(&host).code == CORE_OK);
    assert(daw_workspace_authoring_host_cycle_overlay(&host).code == CORE_OK);
    assert(daw_workspace_authoring_host_font_theme_overlay_active(&host));

    assert(daw_workspace_authoring_host_apply_font_theme_button(
        &host,
        KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_TEXT_SIZE_INC));
    assert(daw_shared_font_zoom_step() == 1);
    assert(daw_workspace_authoring_host_apply_font_theme_button(
        &host,
        KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_FONT_PRESET_IDE));
    assert(daw_shared_font_current_preset(preset_name, sizeof(preset_name)));
    assert(strcmp(preset_name, "ide") == 0);
    assert(daw_workspace_authoring_host_apply_font_theme_button(
        &host,
        KIT_WORKSPACE_AUTHORING_FONT_THEME_BUTTON_THEME_PRESET_MIDNIGHT_CONTRAST));
    assert(daw_shared_theme_current_preset(preset_name, sizeof(preset_name)));
    assert(strcmp(preset_name, "midnight_contrast") == 0);

    assert(daw_workspace_authoring_host_cancel(&host).code == CORE_OK);
    assert(!daw_workspace_authoring_host_active(&host));
    assert(daw_shared_font_zoom_step() == 0);
    assert(daw_shared_font_current_preset(preset_name, sizeof(preset_name)));
    assert(strcmp(preset_name, "daw_default") == 0);
    assert(daw_shared_theme_current_preset(preset_name, sizeof(preset_name)));
    assert(strcmp(preset_name, "studio_blue") == 0);
    assert(daw_workspace_authoring_host_take_font_dirty(&host));
    assert(daw_workspace_authoring_host_take_theme_dirty(&host));
}

int main(void) {
    test_entry_chord_and_apply();
    test_text_entry_blocks_entry();
    test_active_capture_and_cancel();
    test_alt_chord_toggles_off_as_cancel();
    test_font_theme_cancel_restores_entry_baseline();
    printf("daw_workspace_authoring_host_test: ok\n");
    return 0;
}
