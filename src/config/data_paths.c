#include "daw/data_paths.h"

#include <SDL2/SDL.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static void daw_copy_path(char* dst, size_t dst_len, const char* src) {
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

static char* trim_leading(char* str) {
    while (*str && isspace((unsigned char)*str)) {
        ++str;
    }
    return str;
}

static void trim_trailing(char* str) {
    size_t len = strlen(str);
    while (len > 0) {
        unsigned char c = (unsigned char)str[len - 1];
        if (!isspace(c)) {
            break;
        }
        str[len - 1] = '\0';
        --len;
    }
}

static bool path_is_directory(const char* path) {
    struct stat st;
    if (!path || path[0] == '\0' || stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

static bool ensure_directory_recursive(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    char temp[SESSION_PATH_MAX];
    daw_copy_path(temp, sizeof(temp), path);
    for (char* p = temp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char hold = *p;
            *p = '\0';
#if defined(_WIN32)
            _mkdir(temp);
#else
            mkdir(temp, 0755);
#endif
            *p = hold;
        }
    }
#if defined(_WIN32)
    if (_mkdir(temp) == 0 || errno == EEXIST) {
        return true;
    }
#else
    if (mkdir(temp, 0755) == 0 || errno == EEXIST) {
        return true;
    }
#endif
    return path_is_directory(path);
}

static bool parse_key_value_line(DawDataPaths* paths, char* line) {
    if (!paths || !line) {
        return false;
    }
    char* cursor = trim_leading(line);
    if (*cursor == '\0' || *cursor == '#' || *cursor == ';') {
        return false;
    }
    char* comment = strpbrk(cursor, "#;");
    if (comment) {
        *comment = '\0';
    }
    trim_trailing(cursor);
    char* equals = strchr(cursor, '=');
    if (!equals) {
        return false;
    }
    *equals = '\0';
    char* key = trim_leading(cursor);
    trim_trailing(key);
    char* value = trim_leading(equals + 1);
    trim_trailing(value);
    if (*key == '\0' || *value == '\0') {
        return false;
    }
    if (strcmp(key, "input_root") == 0) {
        daw_copy_path(paths->input_root, sizeof(paths->input_root), value);
        return true;
    }
    if (strcmp(key, "output_root") == 0) {
        daw_copy_path(paths->output_root, sizeof(paths->output_root), value);
        return true;
    }
    if (strcmp(key, "library_copy_root") == 0) {
        daw_copy_path(paths->library_copy_root, sizeof(paths->library_copy_root), value);
        return true;
    }
    return false;
}

static bool resolve_absolute_path(const char* path, char* out_abs, size_t out_len) {
    if (!path || !out_abs || out_len == 0) {
        return false;
    }
    out_abs[0] = '\0';
    if (path[0] == '/') {
        daw_copy_path(out_abs, out_len, path);
        return true;
    }
    char cwd[SESSION_PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return false;
    }
    if (snprintf(out_abs, out_len, "%s/%s", cwd, path) >= (int)out_len) {
        return false;
    }
    return true;
}

static bool path_targets_app_bundle_contents(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    char absolute[SESSION_PATH_MAX * 2];
    if (!resolve_absolute_path(path, absolute, sizeof(absolute))) {
        return false;
    }
    return strstr(absolute, ".app/Contents/") != NULL;
}

void daw_data_paths_set_defaults(DawDataPaths* paths) {
    if (!paths) {
        return;
    }
    daw_copy_path(paths->input_root, sizeof(paths->input_root), DAW_DATA_PATH_DEFAULT_INPUT_ROOT);
    daw_copy_path(paths->output_root, sizeof(paths->output_root), DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
    daw_copy_path(paths->library_copy_root,
                  sizeof(paths->library_copy_root),
                  DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT);
}

const char* daw_data_paths_library_root(const DawDataPaths* paths) {
    if (!paths || paths->input_root[0] == '\0') {
        return DAW_DATA_PATH_DEFAULT_INPUT_ROOT;
    }
    return paths->input_root;
}

bool daw_data_paths_valid(const DawDataPaths* paths) {
    if (!paths) {
        return false;
    }
    return paths->input_root[0] != '\0' &&
           paths->output_root[0] != '\0' &&
           paths->library_copy_root[0] != '\0';
}

void daw_data_paths_apply_runtime_policy(DawDataPaths* paths) {
    if (!paths) {
        return;
    }
    if (paths->input_root[0] == '\0') {
        daw_copy_path(paths->input_root, sizeof(paths->input_root), DAW_DATA_PATH_DEFAULT_INPUT_ROOT);
        SDL_Log("data_paths: input_root empty -> using default %s", paths->input_root);
    }
    if (paths->output_root[0] == '\0') {
        daw_copy_path(paths->output_root, sizeof(paths->output_root), DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
        SDL_Log("data_paths: output_root empty -> using default %s", paths->output_root);
    }
    if (paths->library_copy_root[0] == '\0') {
        daw_copy_path(paths->library_copy_root,
                      sizeof(paths->library_copy_root),
                      DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT);
        SDL_Log("data_paths: library_copy_root empty -> using default %s", paths->library_copy_root);
    }

    if (!path_is_directory(paths->input_root)) {
        SDL_Log("data_paths: input_root missing/unreadable (%s), falling back to %s",
                paths->input_root,
                DAW_DATA_PATH_DEFAULT_INPUT_ROOT);
        daw_copy_path(paths->input_root, sizeof(paths->input_root), DAW_DATA_PATH_DEFAULT_INPUT_ROOT);
    }
    if (!path_is_directory(paths->output_root) && !ensure_directory_recursive(paths->output_root)) {
        SDL_Log("data_paths: output_root unavailable (%s), falling back to %s",
                paths->output_root,
                DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
        daw_copy_path(paths->output_root, sizeof(paths->output_root), DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
        ensure_directory_recursive(paths->output_root);
    }
    if (!path_is_directory(paths->library_copy_root) &&
        !ensure_directory_recursive(paths->library_copy_root)) {
        SDL_Log("data_paths: library_copy_root unavailable (%s), falling back to %s",
                paths->library_copy_root,
                DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT);
        daw_copy_path(paths->library_copy_root,
                      sizeof(paths->library_copy_root),
                      DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT);
        ensure_directory_recursive(paths->library_copy_root);
    }
}

bool daw_data_paths_load_file(const char* path, DawDataPaths* out_paths) {
    if (!out_paths || !path || path[0] == '\0') {
        return false;
    }
    DawDataPaths parsed = {0};
    daw_data_paths_set_defaults(&parsed);

    FILE* file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    char line[SESSION_PATH_MAX * 2];
    while (fgets(line, sizeof(line), file)) {
        parse_key_value_line(&parsed, line);
    }
    fclose(file);
    daw_data_paths_apply_runtime_policy(&parsed);
    *out_paths = parsed;
    return true;
}

bool daw_data_paths_save_file(const char* path, const DawDataPaths* paths) {
    if (!path || path[0] == '\0' || !paths) {
        return false;
    }
    if (path_targets_app_bundle_contents(path)) {
        SDL_Log("data_paths: refusing mutable write under app bundle contents: %s", path);
        return false;
    }
    char dirbuf[SESSION_PATH_MAX * 2];
    daw_copy_path(dirbuf, sizeof(dirbuf), path);
    char* slash = strrchr(dirbuf, '/');
    if (slash) {
        *slash = '\0';
        if (!ensure_directory_recursive(dirbuf)) {
            SDL_Log("data_paths: failed to prepare runtime config dir %s", dirbuf);
            return false;
        }
    }
    FILE* file = fopen(path, "wb");
    if (!file) {
        SDL_Log("data_paths: failed to open %s for write: %s", path, strerror(errno));
        return false;
    }
    fprintf(file, "input_root=%s\n", paths->input_root);
    fprintf(file, "output_root=%s\n", paths->output_root);
    fprintf(file, "library_copy_root=%s\n", paths->library_copy_root);
    if (fclose(file) != 0) {
        SDL_Log("data_paths: failed to close %s after write", path);
        return false;
    }
    return true;
}

bool daw_data_paths_load_runtime(DawDataPaths* out_paths) {
    if (!out_paths) {
        return false;
    }
    daw_data_paths_set_defaults(out_paths);
    if (!daw_data_paths_load_file(DAW_DATA_PATH_RUNTIME_CONFIG_PATH, out_paths)) {
        daw_data_paths_apply_runtime_policy(out_paths);
        return false;
    }
    return true;
}

bool daw_data_paths_save_runtime(const DawDataPaths* paths) {
    if (!paths) {
        return false;
    }
    return daw_data_paths_save_file(DAW_DATA_PATH_RUNTIME_CONFIG_PATH, paths);
}
