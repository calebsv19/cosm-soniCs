#include "input/project_modal_input.h"

#include "app_state.h"
#include "ui/text_edit.h"
#include "ui/project_modal_controls.h"
#include "ui/transport_controls.h"
#include "session/project_manager.h"

#include <SDL2/SDL.h>
#include <string.h>

bool project_modal_input_active(const AppState* state) {
    return state && (state->project_prompt.active || state->project_load.active);
}

void project_modal_input_open_save_prompt(AppState* state) {
    if (!state) {
        return;
    }
    if (project_modal_input_active(state)) return;
    daw_text_cancel_composition(state);
    state->project_prompt.active = true;
    state->project_prompt.error[0] = '\0';
    project_manager_init(state);
    project_collections_refresh(state,NULL);
    state->project_prompt.folder_mode=false;
    state->project_prompt.resume_load=false;
    SDL_strlcpy(state->project_prompt.buffer,state->project.has_name ? state->project.name : "",sizeof(state->project_prompt.buffer));
    state->project_prompt.cursor = (int)strlen(state->project_prompt.buffer);
    daw_text_edit_begin(&state->project_prompt.text_edit, state->project_prompt.buffer,
        sizeof(state->project_prompt.buffer), &state->project_prompt.cursor, KIT_UI_TEXT_SINGLE_LINE);
    daw_transport_controls_sync(&state->transport_ui, state);
    SDL_StartTextInput();
}

void project_modal_input_close_save_prompt(AppState* state) {
    if (!state) {
        return;
    }
    state->project_prompt.active = false;
    kit_ui_text_cancel_composition(&state->project_prompt.text_edit);
    state->project_prompt.buffer[0] = '\0';
    state->project_prompt.cursor = 0;
    daw_transport_controls_sync(&state->transport_ui, state);
    daw_text_resume_input(state);
}

// Reloads only the visible list after browsing changes, retaining the live song and history.
static void refresh_list(AppState* s) {
    s->project_load.count=0;s->project_load.selected_index=-1;s->project_load.scroll_offset=0;
    s->project_load.last_click_index=-1;s->project_load.last_click_ticks=0;s->project_load.error[0]=0;
    if(!project_collections_list(s,s->project_load.entries,64,&s->project_load.count))
        SDL_strlcpy(s->project_load.error,"Folder unavailable. Choose another collection.",sizeof(s->project_load.error));
    if(s->project_load.count)s->project_load.selected_index=0;
}
// Opens an explicit collection-home path field without changing global data roots.
static void open_folder(AppState* s) {
    bool load=s->project_load.active;s->project_load.active=false;
    if(!s->project_prompt.active)project_modal_input_open_save_prompt(s);
    s->project_prompt.folder_mode=true;s->project_prompt.resume_load=load;s->project_prompt.error[0]=0;
    SDL_strlcpy(s->project_prompt.buffer,s->project_collections.root,sizeof(s->project_prompt.buffer));
    s->project_prompt.cursor=(int)strlen(s->project_prompt.buffer);
    daw_text_edit_begin(&s->project_prompt.text_edit,s->project_prompt.buffer,sizeof(s->project_prompt.buffer),
        &s->project_prompt.cursor,KIT_UI_TEXT_SINGLE_LINE);
    SDL_StartTextInput();
}
// Cycles independent project directories and invalidates the prior row selection.
static void choose_collection(AppState* s,int delta) {
    int n=s->project_collections.count;if(!n)return;
    project_collections_select(s,(s->project_collections.selected+delta+n)%n);
    if(s->project_load.active)refresh_list(s);
}
// Publishes the requested checkpoint or updates the exact current path after explicit activation.
static void submit_prompt(AppState* s,bool update) {
    ProjectSavePrompt* p=&s->project_prompt;
    if(p->folder_mode) {
        if(!project_collections_refresh(s,p->buffer)) {SDL_strlcpy(p->error,"Use an existing collections folder containing projects/ subfolders.",sizeof(p->error));return;}
        bool load=p->resume_load;project_modal_input_close_save_prompt(s);
        if(load)project_modal_input_open_load_modal(s);else project_modal_input_open_save_prompt(s);
        return;
    }
    bool ok=update ? project_manager_save(s,s->project.name,true) : project_collections_save_version(s,p->buffer);
    if(ok)project_modal_input_close_save_prompt(s);
    else SDL_strlcpy(p->error,"Save failed; current song retained. Retry or Cancel.",sizeof(p->error));
}
// Handles save/path text input and release-owned collection actions in one isolated modal.
static bool project_prompt_handle_event(AppState* s,const SDL_Event* e) {
    ProjectSavePrompt* p=&s->project_prompt;DawProjectModalLayout l;daw_project_modal_layout(s,&l);
    if(e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)
        kit_ui_text_cancel_composition(&p->text_edit);
    bool default_enter=e->type==SDL_KEYDOWN && (e->key.keysym.sym==SDLK_RETURN || e->key.keysym.sym==SDLK_KP_ENTER) &&
        !s->project_modal_controls.surface.interaction.focused_id;
    if(!(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE) && !default_enter) {
        int action;if(daw_project_modal_controls_event(s,e,&action)) {
            if(action==1)submit_prompt(s,false);
            if(action==2){bool load=p->folder_mode && p->resume_load;project_modal_input_close_save_prompt(s);if(load)project_modal_input_open_load_modal(s);}
            if(action==3)choose_collection(s,-1);if(action==4)choose_collection(s,1);
            if(action==5)open_folder(s);if(action==6)submit_prompt(s,true);return true;
        }
    }
    if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
        SDL_Point point={e->button.x,e->button.y};if(SDL_PointInRect(&point,&l.field)) {
            s->project_modal_controls.surface.interaction.focused_id=0;
            daw_text_edit_click(&p->text_edit,p->buffer,sizeof(p->buffer),&p->cursor,KIT_UI_TEXT_SINGLE_LINE,1,l.field.x+8,l.field.w-16,point.x);return true;
        }
    }
    KitUiTextEventResult result=daw_text_edit_event(&p->text_edit,p->buffer,sizeof(p->buffer),&p->cursor,KIT_UI_TEXT_SINGLE_LINE,e);
    if(result.cancel){bool load=p->folder_mode && p->resume_load;project_modal_input_close_save_prompt(s);if(load)project_modal_input_open_load_modal(s);}
    if(result.submit)submit_prompt(s,false);return result.consumed!=0;
}

void project_modal_input_close_load_modal(AppState* state) {
    if (!state) {
        return;
    }
    state->project_load.active = false;
    state->project_load.count = 0;
    state->project_load.selected_index = -1;
    daw_project_modal_controls_sync(state);
    daw_transport_controls_sync(&state->transport_ui, state);
    daw_text_resume_input(state);
}

static void project_load_clamp_scroll(ProjectLoadModal* modal, int item_height, int view_height) {
    if (!modal) {
        return;
    }
    float max_scroll = (float)(modal->count * item_height - view_height);
    if (max_scroll < 0.0f) {
        max_scroll = 0.0f;
    }
    if (modal->scroll_offset < 0.0f) {
        modal->scroll_offset = 0.0f;
    }
    if (modal->scroll_offset > max_scroll) {
        modal->scroll_offset = max_scroll;
    }
}

bool project_modal_input_open_load_modal(AppState* state) {
    if (!state) {
        return false;
    }
    if (project_modal_input_active(state)) return false;
    daw_text_cancel_composition(state);
    SDL_StopTextInput();
    state->project_load.active = true;
    state->project_load.error[0] = '\0';
    state->project_load.scroll_offset = 0.0f;
    state->project_load.selected_index = -1;
    state->project_load.last_click_index = -1;
    state->project_load.last_click_ticks = 0;
    daw_project_modal_controls_sync(state);
    daw_transport_controls_sync(&state->transport_ui, state);

    project_manager_init(state);
    project_collections_refresh(state,NULL);
    refresh_list(state);
    int count=state->project_load.count;
    int match = -1;
    for (int i = 0; i < count; ++i) {
        if (state->project.path[0] && strcmp(state->project.path, state->project_load.entries[i].path) == 0) {
            match = i;
            break;
        }
    }
    state->project_load.selected_index = match >= 0 ? match : (count ? 0 : -1);
    return true;
}

static void project_load_selected(AppState* state, int selected_index) {
    if (!state || selected_index < 0 || selected_index >= state->project_load.count) {
        return;
    }
    if (project_manager_load(state, state->project_load.entries[selected_index].path)) {
        project_manager_post_load(state);
        project_modal_input_close_load_modal(state);
    } else {
        SDL_strlcpy(state->project_load.error, "Load failed. Choose another project or retry.", sizeof(state->project_load.error));
    }
}

static bool project_load_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !state->project_load.active || !event) {
        return false;
    }
    ProjectLoadModal* modal = &state->project_load;

    DawProjectModalLayout layout;daw_project_modal_layout(state,&layout);
    SDL_Rect list_rect=layout.list;

    int item_h = 28;
    project_load_clamp_scroll(modal, item_h, list_rect.h);

    if (!(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE)) {
        int action;
        if (daw_project_modal_controls_event(state, event, &action)) {
            if (action == 1) project_load_selected(state, modal->selected_index);
            if (action == 2) project_modal_input_close_load_modal(state);
            if (action == 3) choose_collection(state,-1);
            if (action == 4) choose_collection(state,1);
            if (action == 5) open_folder(state);
            return true;
        }
    }

    switch (event->type) {
    case SDL_MOUSEWHEEL:
        modal->scroll_offset -= (float)event->wheel.y * (float)item_h * 2.0f;
        project_load_clamp_scroll(modal, item_h, list_rect.h);
        return true;
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_Point p = {event->button.x, event->button.y};
            Uint32 now = SDL_GetTicks();
            if (SDL_PointInRect(&p, &list_rect)) {
                int local_y = p.y - list_rect.y;
                int idx = (int)((local_y + (int)modal->scroll_offset) / item_h);
                if (idx >= 0 && idx < modal->count) {
                    if (modal->last_click_index == idx && (now - modal->last_click_ticks) <= 350) {
                        project_load_selected(state, idx);
                        return true;
                    }
                    modal->selected_index = idx;
                    modal->last_click_index = idx;
                    modal->last_click_ticks = now;
                }
                return true;
            }

        }
        break;
    case SDL_KEYDOWN:
        if (event->key.repeat) return true;
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            project_modal_input_close_load_modal(state);
            return true;
        }
        if (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER) {
            project_load_selected(state, modal->selected_index);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

// Keeps every modal event isolated from timeline shortcuts, including unhandled letter keydowns.
bool project_modal_input_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !event) {
        return false;
    }
    if (state->project_load.active) {
        (void)project_load_handle_event(state, event);
        return true;
    }
    if (state->project_prompt.active) {
        (void)project_prompt_handle_event(state, event);
        return true;
    }
    return false;
}
