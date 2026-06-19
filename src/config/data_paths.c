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

void daw_data_path_copy(char* dst, size_t dst_len, const char* src) {
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

bool daw_data_path_exists(const char* path) {
    struct stat st;
    return path && path[0] != '\0' && stat(path, &st) == 0;
}

bool daw_data_path_is_directory(const char* path) {
    struct stat st;
    if (!path || path[0] == '\0' || stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool daw_data_path_is_regular_file(const char* path) {
    struct stat st;
    if (!path || path[0] == '\0' || stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

static bool data_path_is_directory_with_errno(const char* path, int* out_errno) {
    struct stat st;
    if (out_errno) {
        *out_errno = 0;
    }
    if (!path || path[0] == '\0') {
        if (out_errno) {
            *out_errno = EINVAL;
        }
        return false;
    }
    if (stat(path, &st) != 0) {
        if (out_errno) {
            *out_errno = errno;
        }
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (out_errno) {
            *out_errno = ENOTDIR;
        }
        return false;
    }
    return true;
}

static const char* data_path_reason(int err) {
    return err != 0 ? strerror(err) : "unavailable";
}

bool daw_data_path_ensure_directory_recursive(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    char temp[SESSION_PATH_MAX];
    daw_data_path_copy(temp, sizeof(temp), path);
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
    return daw_data_path_is_directory(path);
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
        daw_data_path_copy(paths->input_root, sizeof(paths->input_root), value);
        return true;
    }
    if (strcmp(key, "output_root") == 0) {
        daw_data_path_copy(paths->output_root, sizeof(paths->output_root), value);
        return true;
    }
    if (strcmp(key, "library_copy_root") == 0) {
        daw_data_path_copy(paths->library_copy_root, sizeof(paths->library_copy_root), value);
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
        daw_data_path_copy(out_abs, out_len, path);
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

bool daw_data_path_targets_app_bundle_contents(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    char absolute[SESSION_PATH_MAX * 2];
    if (!resolve_absolute_path(path, absolute, sizeof(absolute))) {
        return false;
    }
    const char* marker = strstr(absolute, ".app/Contents");
    if (!marker) {
        return false;
    }
    char next = marker[strlen(".app/Contents")];
    return next == '\0' || next == '/' || next == '\\';
}

bool daw_data_path_is_safe_write_root(const char* path) {
    return path && path[0] != '\0' && !daw_data_path_targets_app_bundle_contents(path);
}

static bool apply_write_root_guard(const char* role, char* path, size_t path_len, const char* fallback) {
    if (!path || path_len == 0) {
        return false;
    }
    if (daw_data_path_is_safe_write_root(path)) {
        return true;
    }
    SDL_Log("data_paths: role=%s path=%s write_root refused reason=inside_app_bundle_contents fallback=%s",
            role ? role : "(none)",
            path[0] != '\0' ? path : "(empty)",
            fallback ? fallback : "(none)");
    daw_data_path_copy(path, path_len, fallback);
    return false;
}

void daw_data_paths_set_defaults(DawDataPaths* paths) {
    if (!paths) {
        return;
    }
    daw_data_path_copy(paths->input_root, sizeof(paths->input_root), DAW_DATA_PATH_DEFAULT_INPUT_ROOT);
    daw_data_path_copy(paths->output_root, sizeof(paths->output_root), DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
    daw_data_path_copy(paths->library_copy_root,
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
        daw_data_path_copy(paths->input_root, sizeof(paths->input_root), DAW_DATA_PATH_DEFAULT_INPUT_ROOT);
        SDL_Log("data_paths: input_root empty -> using default %s", paths->input_root);
    }
    if (paths->output_root[0] == '\0') {
        daw_data_path_copy(paths->output_root, sizeof(paths->output_root), DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
        SDL_Log("data_paths: output_root empty -> using default %s", paths->output_root);
    }
    if (paths->library_copy_root[0] == '\0') {
        daw_data_path_copy(paths->library_copy_root,
                           sizeof(paths->library_copy_root),
                           DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT);
        SDL_Log("data_paths: library_copy_root empty -> using default %s", paths->library_copy_root);
    }

    apply_write_root_guard("output_root",
                           paths->output_root,
                           sizeof(paths->output_root),
                           DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
    apply_write_root_guard("library_copy_root",
                           paths->library_copy_root,
                           sizeof(paths->library_copy_root),
                           DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT);

    int input_errno = 0;
    if (!data_path_is_directory_with_errno(paths->input_root, &input_errno)) {
        SDL_Log("data_paths: role=input_root path=%s unavailable reason=%s fallback=%s",
                paths->input_root,
                data_path_reason(input_errno),
                DAW_DATA_PATH_DEFAULT_INPUT_ROOT);
        daw_data_path_copy(paths->input_root, sizeof(paths->input_root), DAW_DATA_PATH_DEFAULT_INPUT_ROOT);
    }

    int output_errno = 0;
    if (!data_path_is_directory_with_errno(paths->output_root, &output_errno)) {
        errno = 0;
        bool output_prepared = daw_data_path_ensure_directory_recursive(paths->output_root);
        int prepare_errno = errno;
        if (!output_prepared) {
            SDL_Log("data_paths: role=output_root path=%s unavailable reason=%s prepare_reason=%s fallback=%s",
                paths->output_root,
                data_path_reason(output_errno),
                data_path_reason(prepare_errno),
                DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
            daw_data_path_copy(paths->output_root, sizeof(paths->output_root), DAW_DATA_PATH_DEFAULT_OUTPUT_ROOT);
            daw_data_path_ensure_directory_recursive(paths->output_root);
        }
    }

    int library_errno = 0;
    if (!data_path_is_directory_with_errno(paths->library_copy_root, &library_errno)) {
        errno = 0;
        bool library_prepared = daw_data_path_ensure_directory_recursive(paths->library_copy_root);
        int prepare_errno = errno;
        if (!library_prepared) {
            SDL_Log("data_paths: role=library_copy_root path=%s unavailable reason=%s prepare_reason=%s fallback=%s",
                paths->library_copy_root,
                data_path_reason(library_errno),
                data_path_reason(prepare_errno),
                DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT);
            daw_data_path_copy(paths->library_copy_root,
                               sizeof(paths->library_copy_root),
                               DAW_DATA_PATH_DEFAULT_LIBRARY_COPY_ROOT);
            daw_data_path_ensure_directory_recursive(paths->library_copy_root);
        }
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
        SDL_Log("data_paths: role=runtime_config path=%s open=read failed reason=%s",
                path,
                strerror(errno));
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
    if (daw_data_path_targets_app_bundle_contents(path)) {
        SDL_Log("data_paths: role=runtime_config path=%s open=write refused reason=inside_app_bundle_contents", path);
        return false;
    }
    char dirbuf[SESSION_PATH_MAX * 2];
    daw_data_path_copy(dirbuf, sizeof(dirbuf), path);
    char* slash = strrchr(dirbuf, '/');
    if (slash) {
        *slash = '\0';
        if (!daw_data_path_ensure_directory_recursive(dirbuf)) {
            SDL_Log("data_paths: role=runtime_config_dir path=%s prepare=write failed reason=%s",
                    dirbuf,
                    data_path_reason(errno));
            return false;
        }
    }
    FILE* file = fopen(path, "wb");
    if (!file) {
        SDL_Log("data_paths: role=runtime_config path=%s open=write failed reason=%s", path, strerror(errno));
        return false;
    }
    fprintf(file, "input_root=%s\n", paths->input_root);
    fprintf(file, "output_root=%s\n", paths->output_root);
    fprintf(file, "library_copy_root=%s\n", paths->library_copy_root);
    if (fclose(file) != 0) {
        SDL_Log("data_paths: role=runtime_config path=%s close=write failed reason=%s", path, strerror(errno));
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
