#include "session/project_manager.h"

#include "app_state.h"
#include "daw/data_paths.h"
#include "session.h"
#include "ui/timeline_view.h"
#include "ui/effects_panel.h"
#include "input/input_manager.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char* kLegacyProjectsDir = "config/projects";
static const char* kLegacyLastPathFile = "config/projects/last_project.txt";
static const char* kSessionFileName = "last_session.json";

static void path_copy(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strnlen(src, dst_len - 1);
    memmove(dst, src, n);
    dst[n] = '\0';
}

static void resolve_projects_dir(const AppState* state, char* out_path, size_t out_len) {
    if (!out_path || out_len == 0) {
        return;
    }
    if (state && state->data_paths.output_root[0]) {
        if (snprintf(out_path, out_len, "%s/projects", state->data_paths.output_root) < (int)out_len) {
            return;
        }
    }
    path_copy(out_path, out_len, kLegacyProjectsDir);
}

static void resolve_last_path_file(const AppState* state, char* out_path, size_t out_len) {
    char projects_dir[SESSION_PATH_MAX];
    resolve_projects_dir(state, projects_dir, sizeof(projects_dir));
    if (snprintf(out_path, out_len, "%s/last_project.txt", projects_dir) >= (int)out_len) {
        path_copy(out_path, out_len, kLegacyLastPathFile);
    }
}

bool project_manager_last_session_path(const AppState* state, char* out_path, size_t out_len) {
    if (!out_path || out_len == 0) {
        return false;
    }
    if (state && state->data_paths.output_root[0]) {
        if (snprintf(out_path, out_len, "%s/%s", state->data_paths.output_root, kSessionFileName) < (int)out_len) {
            return true;
        }
    }
    path_copy(out_path, out_len, "config/last_session.json");
    return true;
}

static bool ensure_dir_exists(const char* path) {
    if (!path || !path[0]) {
        return false;
    }
#ifdef _WIN32
    int res = _mkdir(path);
#else
    int res = mkdir(path, 0755);
#endif
    if (res == 0) {
        return true;
    }
    if (errno == EEXIST) {
        return true;
    }
    SDL_Log("project_manager: mkdir %s failed: %s", path, strerror(errno));
    return false;
}

static void sanitize_name(const char* src, char* dst, size_t dst_len) {
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        strncpy(dst, "project", dst_len - 1);
        dst[dst_len - 1] = '\0';
        return;
    }
    size_t out = 0;
    for (const unsigned char* p = (const unsigned char*)src; *p && out + 1 < dst_len; ++p) {
        unsigned char ch = *p;
        if (isalnum(ch) || ch == '_' || ch == '-') {
            dst[out++] = (char)ch;
        } else if (ch == ' ') {
            dst[out++] = '_';
        } else {
            dst[out++] = '_';
        }
    }
    if (out == 0) {
        dst[out++] = 'p';
        dst[out++] = 'j';
    }
    dst[out] = '\0';
}

static void append_extension(char* path, size_t len) {
    if (!path || len == 0) return;
    const char* ext = ".json";
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (path_len + ext_len + 1 > len) {
        return;
    }
    strcat(path, ext);
}

bool project_manager_init(AppState* state) {
    char projects_dir[SESSION_PATH_MAX];
    resolve_projects_dir(state, projects_dir, sizeof(projects_dir));
    return ensure_dir_exists(projects_dir);
}

bool project_manager_remember_last(AppState* state, const char* path) {
    if (!path || !path[0]) {
        return false;
    }
    char projects_dir[SESSION_PATH_MAX];
    char last_path_file[SESSION_PATH_MAX];
    char legacy_projects_dir[SESSION_PATH_MAX];

    resolve_projects_dir(state, projects_dir, sizeof(projects_dir));
    resolve_last_path_file(state, last_path_file, sizeof(last_path_file));
    path_copy(legacy_projects_dir, sizeof(legacy_projects_dir), kLegacyProjectsDir);

    if (!ensure_dir_exists(projects_dir)) {
        return false;
    }
    FILE* f = fopen(last_path_file, "wb");
    if (!f) {
        SDL_Log("project_manager: failed to write %s: %s", last_path_file, strerror(errno));
        return false;
    }
    fputs(path, f);
    fclose(f);
    if (strcmp(projects_dir, legacy_projects_dir) != 0 && ensure_dir_exists(legacy_projects_dir)) {
        FILE* legacy = fopen(kLegacyLastPathFile, "wb");
        if (legacy) {
            fputs(path, legacy);
            fclose(legacy);
        }
    }
    return true;
}

static bool read_last_path_file(const char* path, char* out, size_t out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    size_t read = fread(out, 1, out_len - 1, f);
    fclose(f);
    if (read == 0) {
        out[0] = '\0';
        return false;
    }
    out[read] = '\0';
    for (size_t i = read; i > 0; --i) {
        if (out[i - 1] == '\n' || out[i - 1] == '\r' || out[i - 1] == ' ' || out[i - 1] == '\t') {
            out[i - 1] = '\0';
        } else {
            break;
        }
    }
    return out[0] != '\0';
}

static bool read_last_path(const AppState* state, char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return false;
    }
    char preferred_path[SESSION_PATH_MAX];
    char legacy_path[SESSION_PATH_MAX];

    resolve_last_path_file(state, preferred_path, sizeof(preferred_path));
    path_copy(legacy_path, sizeof(legacy_path), kLegacyLastPathFile);

    if (read_last_path_file(preferred_path, out, out_len)) {
        return true;
    }
    if (strcmp(preferred_path, legacy_path) != 0) {
        return read_last_path_file(legacy_path, out, out_len);
    }
    return false;
}

bool project_manager_post_load(AppState* state) {
    if (!state || !state->engine) {
        return false;
    }
    if (!engine_start(state->engine)) {
        SDL_Log("project_manager: engine_start failed after load/new");
        return false;
    }
    session_apply_pending_master_fx(state);
    session_apply_pending_track_fx(state);
    effects_panel_sync_from_engine(state);
    engine_transport_stop(state->engine);
    engine_transport_seek(state->engine, 0);
    input_manager_reset_meter_history_on_seek(state);
    return true;
}

bool project_manager_save(AppState* state, const char* name_override, bool overwrite_current) {
    if (!state) {
        return false;
    }
    if (!project_manager_init(state)) {
        return false;
    }

    char name_buf[SESSION_NAME_MAX];
    if (name_override && name_override[0]) {
        sanitize_name(name_override, name_buf, sizeof(name_buf));
    } else if (state->project.has_name) {
        sanitize_name(state->project.name, name_buf, sizeof(name_buf));
    } else {
        sanitize_name("project", name_buf, sizeof(name_buf));
    }

    char path[SESSION_PATH_MAX];
    char projects_dir[SESSION_PATH_MAX];
    resolve_projects_dir(state, projects_dir, sizeof(projects_dir));
    snprintf(path, sizeof(path), "%s/%s", projects_dir, name_buf);
    append_extension(path, sizeof(path));

    if (!overwrite_current && state->project.has_name && strcmp(state->project.path, path) != 0) {
        // If we are not overwriting current but the name differs, use current path.
        strncpy(path, state->project.path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    if (!session_save_to_file(state, path)) {
        SDL_Log("project_manager: save failed: %s", path);
        return false;
    }

    strncpy(state->project.name, name_buf, sizeof(state->project.name) - 1);
    state->project.name[sizeof(state->project.name) - 1] = '\0';
    strncpy(state->project.path, path, sizeof(state->project.path) - 1);
    state->project.path[sizeof(state->project.path) - 1] = '\0';
    state->project.has_name = true;

    project_manager_remember_last(state, path);
    SDL_Log("Project saved: %s", path);
    return true;
}

bool project_manager_load(AppState* state, const char* path_optional) {
    if (!state) {
        return false;
    }
    char path[SESSION_PATH_MAX];
    if (path_optional && path_optional[0]) {
        strncpy(path, path_optional, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        if (!read_last_path(state, path, sizeof(path))) {
            SDL_Log("project_manager: no last project path");
            return false;
        }
    }
    if (!session_load_from_file(state, path)) {
        SDL_Log("project_manager: load failed: %s", path);
        return false;
    }
    // Derive name from filename.
    const char* slash = strrchr(path, '/');
    const char* base = slash ? slash + 1 : path;
    char name_buf[SESSION_NAME_MAX];
    strncpy(name_buf, base, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    char* dot = strrchr(name_buf, '.');
    if (dot) *dot = '\0';

    strncpy(state->project.name, name_buf, sizeof(state->project.name) - 1);
    state->project.name[sizeof(state->project.name) - 1] = '\0';
    strncpy(state->project.path, path, sizeof(state->project.path) - 1);
    state->project.path[sizeof(state->project.path) - 1] = '\0';
    state->project.has_name = true;
    project_manager_remember_last(state, path);
    SDL_Log("Project loaded: %s", path);
    return true;
}

bool project_manager_load_last(AppState* state) {
    return project_manager_load(state, NULL);
}

bool project_manager_new(AppState* state) {
    if (!state) {
        return false;
    }
    SessionDocument doc;
    session_document_init(&doc);
    doc.engine = state->runtime_cfg;
    doc.tempo.bpm = (float)state->tempo.bpm;
    doc.tempo.ts_num = state->tempo.ts_num;
    doc.tempo.ts_den = state->tempo.ts_den;
    doc.timeline.visible_seconds = TIMELINE_DEFAULT_VISIBLE_SECONDS;
    doc.timeline.vertical_scale = 1.0f;
    doc.timeline.show_all_grid_lines = false;
    doc.timeline.view_in_beats = state->timeline_view_in_beats;
    doc.timeline.follow_mode = state->timeline_follow_mode;
    doc.loop.enabled = false;
    doc.loop.start_frame = 0;
    doc.loop.end_frame = state->runtime_cfg.sample_rate > 0 ? (uint64_t)state->runtime_cfg.sample_rate : 48000;
    if (!session_apply_document(state, &doc)) {
        SDL_Log("project_manager: failed to create new project");
        return false;
    }
    state->project.has_name = false;
    state->project.name[0] = '\0';
    state->project.path[0] = '\0';
    SDL_Log("project_manager: new project created");
    return true;
}

static long long file_mtime_ms(const char* path) {
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        return 0;
    }
#if defined(__APPLE__) && defined(_DARWIN_FEATURE_64_BIT_INODE)
    return (long long)st.st_mtimespec.tv_sec * 1000LL + (long long)st.st_mtimespec.tv_nsec / 1000000LL;
#else
    return (long long)st.st_mtime * 1000LL;
#endif
}

bool project_manager_get_info(const char* path, ProjectInfo* out_info) {
    if (!path || !out_info) {
        return false;
    }
    memset(out_info, 0, sizeof(*out_info));
    strncpy(out_info->path, path, sizeof(out_info->path) - 1);
    out_info->path[sizeof(out_info->path) - 1] = '\0';

    const char* slash = strrchr(path, '/');
    const char* base = slash ? slash + 1 : path;
    strncpy(out_info->name, base, sizeof(out_info->name) - 1);
    out_info->name[sizeof(out_info->name) - 1] = '\0';
    char* dot = strrchr(out_info->name, '.');
    if (dot) *dot = '\0';

    struct stat st;
    if (stat(path, &st) == 0) {
        out_info->file_size = (long long)st.st_size;
        out_info->modified_ms = file_mtime_ms(path);
    }

    SessionDocument doc;
    session_document_init(&doc);
    if (session_document_read_file(path, &doc)) {
        out_info->track_count = doc.track_count;
        int clips = 0;
        for (int t = 0; t < doc.track_count; ++t) {
            clips += doc.tracks[t].clip_count;
        }
        out_info->clip_count = clips;
        // Approximate duration from playhead frame if set, otherwise from clip data.
        float max_sec = 0.0f;
        const EngineRuntimeConfig* cfg = &doc.engine;
        int sr = cfg ? cfg->sample_rate : 0;
        if (sr > 0) {
            uint64_t max_frame = doc.timeline.playhead_frame;
            if (doc.loop.end_frame > max_frame) {
                max_frame = doc.loop.end_frame;
            }
            for (int t = 0; t < doc.track_count; ++t) {
                const SessionTrack* track = &doc.tracks[t];
                for (int c = 0; c < track->clip_count; ++c) {
                    const SessionClip* clip = &track->clips[c];
                    uint64_t end = clip->start_frame + clip->duration_frames;
                    if (end > max_frame) {
                        max_frame = end;
                    }
                }
            }
            max_sec = (float)max_frame / (float)sr;
        }
        out_info->duration_seconds = max_sec;
        session_document_free(&doc);
    }
    return true;
}

static bool path_already_listed(const ProjectInfo* out_items, int count, const char* path) {
    for (int i = 0; i < count; ++i) {
        if (strcmp(out_items[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static void project_manager_collect_from_dir(const char* dir_path,
                                             ProjectInfo* out_items,
                                             int max_items,
                                             int* io_count) {
    DIR* dir;
    struct dirent* entry;
    char path[SESSION_PATH_MAX];
    int count;
    if (!dir_path || !out_items || !io_count || max_items <= 0) {
        return;
    }
    count = *io_count;
    dir = opendir(dir_path);
    if (!dir) {
        return;
    }
    while ((entry = readdir(dir)) != NULL && count < max_items) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        const char* name = entry->d_name;
        const char* dot = strrchr(name, '.');
        if (!dot || strcmp(dot, ".json") != 0) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", dir_path, name) >= (int)sizeof(path)) {
            continue;
        }
        if (path_already_listed(out_items, count, path)) {
            continue;
        }
        if (project_manager_get_info(path, &out_items[count])) {
            count++;
        }
    }
    closedir(dir);
    *io_count = count;
}

bool project_manager_list(AppState* state, ProjectInfo* out_items, int max_items, int* out_count) {
    if (!out_items || max_items <= 0) {
        return false;
    }
    if (!project_manager_init(state)) {
        return false;
    }
    char preferred_dir[SESSION_PATH_MAX];
    char legacy_dir[SESSION_PATH_MAX];
    int count = 0;
    resolve_projects_dir(state, preferred_dir, sizeof(preferred_dir));
    path_copy(legacy_dir, sizeof(legacy_dir), kLegacyProjectsDir);

    project_manager_collect_from_dir(preferred_dir, out_items, max_items, &count);
    if (strcmp(preferred_dir, legacy_dir) != 0) {
        project_manager_collect_from_dir(legacy_dir, out_items, max_items, &count);
    }
    if (out_count) {
        *out_count = count;
    }
    return count > 0;
}
