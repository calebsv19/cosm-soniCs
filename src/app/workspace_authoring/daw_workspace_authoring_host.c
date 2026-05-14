#include "app/workspace_authoring/daw_workspace_authoring_host.h"

#include <string.h>

#include "core_font.h"
#include "core_theme.h"
#include "ui/shared_theme_font_adapter.h"

static CoreResult daw_workspace_authoring_invalid(const char *message) {
    CoreResult result = { CORE_ERR_INVALID_ARG, message };
    return result;
}

static uint32_t daw_workspace_authoring_mod_bits(SDL_Keymod mods) {
    uint32_t bits = 0u;
    if ((mods & KMOD_SHIFT) != 0) bits |= KIT_WORKSPACE_AUTHORING_MOD_SHIFT;
    if ((mods & KMOD_ALT) != 0) bits |= KIT_WORKSPACE_AUTHORING_MOD_ALT;
    if ((mods & KMOD_CTRL) != 0) bits |= KIT_WORKSPACE_AUTHORING_MOD_CTRL;
    if ((mods & KMOD_GUI) != 0) bits |= KIT_WORKSPACE_AUTHORING_MOD_GUI;
    return bits;
}

static KitWorkspaceAuthoringKey daw_workspace_authoring_key_from_sdl_keysym(const SDL_Keysym *keysym) {
    if (!keysym) return KIT_WORKSPACE_AUTHORING_KEY_UNKNOWN;
    switch (keysym->scancode) {
        case SDL_SCANCODE_C:
            return KIT_WORKSPACE_AUTHORING_KEY_C;
        case SDL_SCANCODE_V:
            return KIT_WORKSPACE_AUTHORING_KEY_V;
        default:
            break;
    }
    switch (keysym->sym) {
        case SDLK_TAB:
            return KIT_WORKSPACE_AUTHORING_KEY_TAB;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return KIT_WORKSPACE_AUTHORING_KEY_ENTER;
        case SDLK_ESCAPE:
            return KIT_WORKSPACE_AUTHORING_KEY_ESCAPE;
        case SDLK_c:
            return KIT_WORKSPACE_AUTHORING_KEY_C;
        case SDLK_v:
            return KIT_WORKSPACE_AUTHORING_KEY_V;
        default:
            return KIT_WORKSPACE_AUTHORING_KEY_UNKNOWN;
    }
}

static void daw_workspace_authoring_note_consumed(DawWorkspaceAuthoringHostState *host,
                                                  int runtime_event) {
    if (!host) return;
    host->last_event_consumed = 1u;
    host->consumed_event_count += 1u;
    if (runtime_event) {
        host->captured_runtime_event_count += 1u;
    }
}

static void daw_workspace_authoring_host_set_status(DawWorkspaceAuthoringHostState *host,
                                                    const char *status) {
    if (!host) return;
    if (!status) status = "";
    strncpy(host->font_theme_status, status, sizeof(host->font_theme_status) - 1u);
    host->font_theme_status[sizeof(host->font_theme_status) - 1u] = '\0';
}

static void daw_workspace_authoring_host_capture_baseline(DawWorkspaceAuthoringHostState *host) {
    if (!host) return;
    host->baseline_theme_preset[0] = '\0';
    host->baseline_font_preset[0] = '\0';
    (void)daw_shared_theme_current_preset(host->baseline_theme_preset,
                                         sizeof(host->baseline_theme_preset));
    (void)daw_shared_font_current_preset(host->baseline_font_preset,
                                        sizeof(host->baseline_font_preset));
    if (!host->baseline_theme_preset[0]) {
        strncpy(host->baseline_theme_preset, "daw_default", sizeof(host->baseline_theme_preset) - 1u);
        host->baseline_theme_preset[sizeof(host->baseline_theme_preset) - 1u] = '\0';
    }
    if (!host->baseline_font_preset[0]) {
        strncpy(host->baseline_font_preset, "daw_default", sizeof(host->baseline_font_preset) - 1u);
        host->baseline_font_preset[sizeof(host->baseline_font_preset) - 1u] = '\0';
    }
    host->baseline_font_zoom_step = daw_shared_font_zoom_step();
    host->font_theme_baseline_valid = 1u;
}

static void daw_workspace_authoring_host_restore_baseline(DawWorkspaceAuthoringHostState *host) {
    if (!host || !host->font_theme_baseline_valid) return;
    if (host->baseline_theme_preset[0]) {
        (void)daw_shared_theme_set_preset(host->baseline_theme_preset);
        host->font_theme_theme_dirty = 1u;
    }
    if (host->baseline_font_preset[0]) {
        (void)daw_shared_font_set_preset(host->baseline_font_preset);
        host->font_theme_font_dirty = 1u;
    }
    (void)daw_shared_font_set_zoom_step(host->baseline_font_zoom_step);
    host->font_theme_font_dirty = 1u;
    host->font_theme_pending_changes = 0u;
}

void daw_workspace_authoring_host_reset(DawWorkspaceAuthoringHostState *host) {
    if (!host) return;
    memset(host, 0, sizeof(*host));
    host->overlay_mode = DAW_WORKSPACE_AUTHORING_OVERLAY_PANES;
    host->entry_chord_armed_key = KIT_WORKSPACE_AUTHORING_KEY_UNKNOWN;
}

void daw_workspace_authoring_host_set_viewport(DawWorkspaceAuthoringHostState *host,
                                               uint32_t width,
                                               uint32_t height) {
    if (!host) return;
    host->viewport_width = width;
    host->viewport_height = height;
}

int daw_workspace_authoring_host_active(const DawWorkspaceAuthoringHostState *host) {
    return host && host->active ? 1 : 0;
}

int daw_workspace_authoring_host_pane_overlay_active(const DawWorkspaceAuthoringHostState *host) {
    return daw_workspace_authoring_host_active(host) &&
           host->overlay_mode == DAW_WORKSPACE_AUTHORING_OVERLAY_PANES;
}

int daw_workspace_authoring_host_font_theme_overlay_active(const DawWorkspaceAuthoringHostState *host) {
    return daw_workspace_authoring_host_active(host) &&
           host->overlay_mode == DAW_WORKSPACE_AUTHORING_OVERLAY_FONT_THEME;
}

CoreResult daw_workspace_authoring_host_enter(DawWorkspaceAuthoringHostState *host) {
    if (!host) return daw_workspace_authoring_invalid("null authoring host");
    if (!daw_workspace_authoring_host_active(host)) {
        daw_workspace_authoring_host_capture_baseline(host);
        host->active = 1u;
        host->overlay_mode = DAW_WORKSPACE_AUTHORING_OVERLAY_PANES;
        host->enter_count += 1u;
        host->font_theme_pending_changes = 0u;
        host->font_theme_font_dirty = 0u;
        host->font_theme_theme_dirty = 0u;
        host->font_theme_status[0] = '\0';
    }
    host->last_event_entered = 1u;
    return core_result_ok();
}

CoreResult daw_workspace_authoring_host_apply(DawWorkspaceAuthoringHostState *host) {
    if (!host) return daw_workspace_authoring_invalid("null authoring host");
    if (daw_workspace_authoring_host_active(host)) {
        host->active = 0u;
        host->apply_count += 1u;
        host->last_event_accepted = 1u;
        host->font_theme_pending_changes = 0u;
        host->font_theme_baseline_valid = 0u;
    }
    host->key_c_down = 0u;
    host->key_v_down = 0u;
    host->entry_chord_armed_key = KIT_WORKSPACE_AUTHORING_KEY_UNKNOWN;
    host->overlay_mode = DAW_WORKSPACE_AUTHORING_OVERLAY_PANES;
    host->last_event_exited = 1u;
    return core_result_ok();
}

CoreResult daw_workspace_authoring_host_cancel(DawWorkspaceAuthoringHostState *host) {
    if (!host) return daw_workspace_authoring_invalid("null authoring host");
    if (daw_workspace_authoring_host_active(host)) {
        daw_workspace_authoring_host_restore_baseline(host);
        host->active = 0u;
        host->cancel_count += 1u;
        host->last_event_canceled = 1u;
    }
    host->key_c_down = 0u;
    host->key_v_down = 0u;
    host->entry_chord_armed_key = KIT_WORKSPACE_AUTHORING_KEY_UNKNOWN;
    host->overlay_mode = DAW_WORKSPACE_AUTHORING_OVERLAY_PANES;
    host->last_event_exited = 1u;
    return core_result_ok();
}

CoreResult daw_workspace_authoring_host_cycle_overlay(DawWorkspaceAuthoringHostState *host) {
    if (!host) return daw_workspace_authoring_invalid("null authoring host");
    if (!daw_workspace_authoring_host_active(host)) {
        return core_result_ok();
    }
    host->overlay_mode = host->overlay_mode == DAW_WORKSPACE_AUTHORING_OVERLAY_PANES
        ? DAW_WORKSPACE_AUTHORING_OVERLAY_FONT_THEME
        : DAW_WORKSPACE_AUTHORING_OVERLAY_PANES;
    host->overlay_cycle_count += 1u;
    return core_result_ok();
}

int daw_workspace_authoring_host_apply_overlay_button(DawWorkspaceAuthoringHostState *host,
                                                      KitWorkspaceAuthoringOverlayButtonId button_id) {
    if (!host || !daw_workspace_authoring_host_active(host)) return 0;
    host->last_overlay_button_id = (uint32_t)button_id;
    switch (button_id) {
        case KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_MODE:
            (void)daw_workspace_authoring_host_cycle_overlay(host);
            host->overlay_button_click_count += 1u;
            return 1;
        case KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_APPLY:
            (void)daw_workspace_authoring_host_apply(host);
            host->overlay_button_click_count += 1u;
            return 1;
        case KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_CANCEL:
            (void)daw_workspace_authoring_host_cancel(host);
            host->overlay_button_click_count += 1u;
            return 1;
        case KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_ADD:
            host->add_stub_count += 1u;
            host->overlay_button_click_count += 1u;
            daw_workspace_authoring_host_set_status(host, "Add module requested. DAW module insertion is not wired yet.");
            return 1;
        case KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_NONE:
        default:
            break;
    }
    return 0;
}

int daw_workspace_authoring_host_apply_font_theme_button(DawWorkspaceAuthoringHostState *host,
                                                         KitWorkspaceAuthoringFontThemeButtonId button_id) {
    KitWorkspaceAuthoringFontThemeAction action;
    const char *preset_name = NULL;
    if (!host || !daw_workspace_authoring_host_font_theme_overlay_active(host)) return 0;
    if (!kit_workspace_authoring_ui_font_theme_button_enabled(button_id)) return 0;

    action = kit_workspace_authoring_ui_font_theme_action_for_button(button_id);
    host->last_font_theme_button_id = (uint32_t)button_id;

    switch (action.type) {
        case KIT_WORKSPACE_AUTHORING_FONT_THEME_ACTION_TEXT_SIZE_DEC:
            (void)daw_shared_font_step_by(-1);
            host->font_theme_font_dirty = 1u;
            daw_workspace_authoring_host_set_status(host, "Text size decreased.");
            break;
        case KIT_WORKSPACE_AUTHORING_FONT_THEME_ACTION_TEXT_SIZE_INC:
            (void)daw_shared_font_step_by(1);
            host->font_theme_font_dirty = 1u;
            daw_workspace_authoring_host_set_status(host, "Text size increased.");
            break;
        case KIT_WORKSPACE_AUTHORING_FONT_THEME_ACTION_TEXT_SIZE_RESET:
            (void)daw_shared_font_reset_zoom_step();
            host->font_theme_font_dirty = 1u;
            daw_workspace_authoring_host_set_status(host, "Text size reset.");
            break;
        case KIT_WORKSPACE_AUTHORING_FONT_THEME_ACTION_SET_FONT_PRESET:
            preset_name = core_font_preset_name(action.font_preset_id);
            if (!daw_shared_font_set_preset(preset_name)) return 0;
            host->font_theme_font_dirty = 1u;
            daw_workspace_authoring_host_set_status(host, "Font preset previewed.");
            break;
        case KIT_WORKSPACE_AUTHORING_FONT_THEME_ACTION_SET_THEME_PRESET:
            preset_name = core_theme_preset_name(action.theme_preset_id);
            if (!daw_shared_theme_set_preset(preset_name)) return 0;
            host->font_theme_theme_dirty = 1u;
            daw_workspace_authoring_host_set_status(host, "Theme preset previewed.");
            break;
        case KIT_WORKSPACE_AUTHORING_FONT_THEME_ACTION_CUSTOM_THEME_STATUS:
            daw_workspace_authoring_host_set_status(host, action.custom_status_text);
            break;
        case KIT_WORKSPACE_AUTHORING_FONT_THEME_ACTION_NONE:
        default:
            return 0;
    }

    host->font_theme_button_click_count += 1u;
    host->font_theme_pending_changes += 1u;
    return 1;
}

int daw_workspace_authoring_host_take_font_dirty(DawWorkspaceAuthoringHostState *host) {
    if (!host || !host->font_theme_font_dirty) return 0;
    host->font_theme_font_dirty = 0u;
    return 1;
}

int daw_workspace_authoring_host_take_theme_dirty(DawWorkspaceAuthoringHostState *host) {
    if (!host || !host->font_theme_theme_dirty) return 0;
    host->font_theme_theme_dirty = 0u;
    return 1;
}

static int daw_workspace_authoring_host_handle_overlay_click(DawWorkspaceAuthoringHostState *host,
                                                            int x,
                                                            int y) {
    KitWorkspaceAuthoringOverlayButton buttons[4];
    KitWorkspaceAuthoringOverlayButtonId hit = KIT_WORKSPACE_AUTHORING_OVERLAY_BUTTON_NONE;
    uint32_t count = 0u;
    if (!host || !daw_workspace_authoring_host_active(host)) return 0;
    if (host->viewport_width == 0u) return 0;

    count = kit_workspace_authoring_ui_build_overlay_buttons(
        (int)host->viewport_width,
        1,
        daw_workspace_authoring_host_pane_overlay_active(host),
        buttons,
        (uint32_t)(sizeof(buttons) / sizeof(buttons[0])));
    hit = kit_workspace_authoring_ui_overlay_hit_test(buttons, count, (float)x, (float)y);
    return daw_workspace_authoring_host_apply_overlay_button(host, hit);
}

static int daw_workspace_authoring_host_handle_font_theme_click(DawWorkspaceAuthoringHostState *host,
                                                               int x,
                                                               int y) {
    KitWorkspaceAuthoringFontThemeLayout layout;
    KitWorkspaceAuthoringFontThemeButtonId hit;
    if (!host || !daw_workspace_authoring_host_font_theme_overlay_active(host)) return 0;
    if (host->viewport_width == 0u || host->viewport_height == 0u) return 0;
    if (!kit_workspace_authoring_ui_font_theme_build_layout(NULL,
                                                            (int)host->viewport_width,
                                                            (int)host->viewport_height,
                                                            &layout)) {
        return 0;
    }
    hit = kit_workspace_authoring_ui_font_theme_hit_button(&layout, (float)x, (float)y);
    return daw_workspace_authoring_host_apply_font_theme_button(host, hit);
}

int daw_workspace_authoring_host_handle_sdl_event(DawWorkspaceAuthoringHostState *host,
                                                  const SDL_Event *event,
                                                  int text_entry_active) {
    KitWorkspaceAuthoringKey key = KIT_WORKSPACE_AUTHORING_KEY_UNKNOWN;
    uint32_t mod_bits = 0u;

    if (!host || !event) {
        return 0;
    }
    host->last_event_consumed = 0u;
    host->last_event_entered = 0u;
    host->last_event_exited = 0u;
    host->last_event_accepted = 0u;
    host->last_event_canceled = 0u;

    if (event->type == SDL_KEYUP) {
        key = daw_workspace_authoring_key_from_sdl_keysym(&event->key.keysym);
        if (key == KIT_WORKSPACE_AUTHORING_KEY_C) {
            host->key_c_down = 0u;
        } else if (key == KIT_WORKSPACE_AUTHORING_KEY_V) {
            host->key_v_down = 0u;
        }
        if (host->entry_chord_armed_key == (uint8_t)key) {
            host->entry_chord_armed_key = KIT_WORKSPACE_AUTHORING_KEY_UNKNOWN;
        }
        return daw_workspace_authoring_host_active(host);
    }

    if (event->type == SDL_MOUSEBUTTONDOWN &&
        event->button.button == SDL_BUTTON_LEFT &&
        daw_workspace_authoring_host_active(host)) {
        int overlay_hit = daw_workspace_authoring_host_handle_overlay_click(host,
                                                                           event->button.x,
                                                                           event->button.y);
        int font_theme_hit = 0;
        if (!overlay_hit && daw_workspace_authoring_host_font_theme_overlay_active(host)) {
            font_theme_hit = daw_workspace_authoring_host_handle_font_theme_click(host,
                                                                                 event->button.x,
                                                                                 event->button.y);
        }
        daw_workspace_authoring_note_consumed(host, (overlay_hit || font_theme_hit) ? 0 : 1);
        return 1;
    }

    if (event->type != SDL_KEYDOWN) {
        if (daw_workspace_authoring_host_active(host)) {
            daw_workspace_authoring_note_consumed(host, 1);
            return 1;
        }
        return 0;
    }

    key = daw_workspace_authoring_key_from_sdl_keysym(&event->key.keysym);
    mod_bits = daw_workspace_authoring_mod_bits((SDL_Keymod)event->key.keysym.mod);

    if (key == KIT_WORKSPACE_AUTHORING_KEY_C) {
        host->key_c_down = 1u;
    } else if (key == KIT_WORKSPACE_AUTHORING_KEY_V) {
        host->key_v_down = 1u;
    }

    if (!text_entry_active &&
        (mod_bits & KIT_WORKSPACE_AUTHORING_MOD_ALT) != 0u &&
        (mod_bits & (KIT_WORKSPACE_AUTHORING_MOD_SHIFT |
                     KIT_WORKSPACE_AUTHORING_MOD_CTRL |
                     KIT_WORKSPACE_AUTHORING_MOD_GUI)) == 0u &&
        (key == KIT_WORKSPACE_AUTHORING_KEY_C ||
         key == KIT_WORKSPACE_AUTHORING_KEY_V)) {
        if (!kit_workspace_authoring_entry_chord_pressed(key,
                                                         mod_bits,
                                                         host->key_c_down,
                                                         host->key_v_down)) {
            daw_workspace_authoring_note_consumed(host, 0);
            return 1;
        }
        if (host->entry_chord_armed_key == (uint8_t)key) {
            daw_workspace_authoring_note_consumed(host, 0);
            return 1;
        }
        host->entry_chord_armed_key = (uint8_t)key;
        if (daw_workspace_authoring_host_active(host)) {
            (void)daw_workspace_authoring_host_cancel(host);
        } else {
            (void)daw_workspace_authoring_host_enter(host);
        }
        daw_workspace_authoring_note_consumed(host, 0);
        return 1;
    }

    if (!daw_workspace_authoring_host_active(host)) {
        return 0;
    }

    switch (key) {
        case KIT_WORKSPACE_AUTHORING_KEY_TAB:
            (void)daw_workspace_authoring_host_cycle_overlay(host);
            daw_workspace_authoring_note_consumed(host, 1);
            return 1;
        case KIT_WORKSPACE_AUTHORING_KEY_ENTER:
            (void)daw_workspace_authoring_host_apply(host);
            daw_workspace_authoring_note_consumed(host, 1);
            return 1;
        case KIT_WORKSPACE_AUTHORING_KEY_ESCAPE:
            (void)daw_workspace_authoring_host_cancel(host);
            daw_workspace_authoring_note_consumed(host, 1);
            return 1;
        default:
            daw_workspace_authoring_note_consumed(host, 1);
            return 1;
    }
}
