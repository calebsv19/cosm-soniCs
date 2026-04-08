#include "app_state.h"
#include "daw/data_paths.h"
#include "session/project_manager.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_failures = 0;

static void failf(const char* message, const char* detail) {
    fprintf(stderr, "daw_data_path_contract_test: %s (%s)\n", message, detail ? detail : "no-detail");
    g_failures++;
}

static bool ensure_dir(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return true;
    }
    return false;
}

static bool is_dir(const char* path) {
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

static bool write_text_file(const char* path, const char* text) {
    FILE* file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    if (text && text[0] != '\0') {
        size_t n = strlen(text);
        if (fwrite(text, 1, n, file) != n) {
            fclose(file);
            return false;
        }
    }
    return fclose(file) == 0;
}

static bool read_file_contains(const char* path, const char* needle) {
    FILE* file = NULL;
    char* buffer = NULL;
    size_t size = 0;
    bool found = false;
    if (!path || !needle) {
        return false;
    }
    file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long n = ftell(file);
    if (n < 0) {
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    buffer = (char*)calloc((size_t)n + 1u, 1u);
    if (!buffer) {
        fclose(file);
        return false;
    }
    size = (size_t)n;
    if (size > 0 && fread(buffer, 1, size, file) != size) {
        free(buffer);
        fclose(file);
        return false;
    }
    found = strstr(buffer, needle) != NULL;
    free(buffer);
    fclose(file);
    return found;
}

static void test_roundtrip_and_recovery(void) {
    char template_path[] = "/tmp/daw_data_path_contract_XXXXXX";
    char* root = mkdtemp(template_path);
    char input_root[SESSION_PATH_MAX];
    char output_root[SESSION_PATH_MAX];
    char copy_root[SESSION_PATH_MAX];
    char cfg_path[SESSION_PATH_MAX];
    char missing_input[SESSION_PATH_MAX];
    DawDataPaths paths = {0};
    DawDataPaths loaded = {0};

    if (!root) {
        failf("mkdtemp failed", strerror(errno));
        return;
    }

    snprintf(input_root, sizeof(input_root), "%s/input_audio", root);
    snprintf(output_root, sizeof(output_root), "%s/output_data", root);
    snprintf(copy_root, sizeof(copy_root), "%s/library_copy", root);
    snprintf(cfg_path, sizeof(cfg_path), "%s/data_paths.cfg", root);

    if (!ensure_dir(input_root) || !ensure_dir(output_root) || !ensure_dir(copy_root)) {
        failf("failed creating roundtrip directories", root);
        return;
    }

    snprintf(paths.input_root, sizeof(paths.input_root), "%s", input_root);
    snprintf(paths.output_root, sizeof(paths.output_root), "%s", output_root);
    snprintf(paths.library_copy_root, sizeof(paths.library_copy_root), "%s", copy_root);

    if (!daw_data_paths_save_file(cfg_path, &paths)) {
        failf("daw_data_paths_save_file failed", cfg_path);
        return;
    }
    if (!daw_data_paths_load_file(cfg_path, &loaded)) {
        failf("daw_data_paths_load_file failed", cfg_path);
        return;
    }
    if (strcmp(loaded.input_root, input_root) != 0) {
        failf("input_root roundtrip mismatch", loaded.input_root);
    }
    if (strcmp(loaded.output_root, output_root) != 0) {
        failf("output_root roundtrip mismatch", loaded.output_root);
    }
    if (strcmp(loaded.library_copy_root, copy_root) != 0) {
        failf("library_copy_root roundtrip mismatch", loaded.library_copy_root);
    }

    snprintf(missing_input, sizeof(missing_input), "%s/missing_input_does_not_exist", root);
    snprintf(output_root, sizeof(output_root), "%s/recovered_output", root);
    snprintf(copy_root, sizeof(copy_root), "%s/recovered_copy", root);
    {
        char text[SESSION_PATH_MAX * 2];
        snprintf(text,
                 sizeof(text),
                 "input_root=%s\noutput_root=%s\nlibrary_copy_root=%s\n",
                 missing_input,
                 output_root,
                 copy_root);
        if (!write_text_file(cfg_path, text)) {
            failf("failed writing recovery config", cfg_path);
            return;
        }
    }

    if (!daw_data_paths_load_file(cfg_path, &loaded)) {
        failf("load_file failed in recovery case", cfg_path);
        return;
    }
    if (strcmp(loaded.input_root, DAW_DATA_PATH_DEFAULT_INPUT_ROOT) != 0) {
        failf("missing input root did not fall back to default", loaded.input_root);
    }
    if (!is_dir(loaded.output_root)) {
        failf("output_root recovery did not create directory", loaded.output_root);
    }
    if (!is_dir(loaded.library_copy_root)) {
        failf("library_copy_root recovery did not create directory", loaded.library_copy_root);
    }
}

static void test_output_root_session_resolution(void) {
    AppState state = {0};
    char path[SESSION_PATH_MAX];

    snprintf(state.data_paths.output_root, sizeof(state.data_paths.output_root), "/tmp/daw_contract_output");
    if (!project_manager_last_session_path(&state, path, sizeof(path))) {
        failf("project_manager_last_session_path failed", "output-root case");
        return;
    }
    if (strcmp(path, "/tmp/daw_contract_output/last_session.json") != 0) {
        failf("session path not rooted in output_root", path);
    }

    state.data_paths.output_root[0] = '\0';
    if (!project_manager_last_session_path(&state, path, sizeof(path))) {
        failf("project_manager_last_session_path failed", "legacy fallback case");
        return;
    }
    if (strcmp(path, "config/last_session.json") != 0) {
        failf("legacy session fallback mismatch", path);
    }
}

static void test_source_contract_guards(void) {
    const char* library_input_path = "src/input/library_input.c";
    const char* timeline_drop_path = "src/input/timeline/timeline_drop.c";

    if (!read_file_contains(library_input_path, "library_input_copy_file_binary(source_path, destination)")) {
        failf("library ingest copy guard missing", library_input_path);
    }
    if (!read_file_contains(library_input_path, "copy_root = state->data_paths.library_copy_root")) {
        failf("library copy_root contract guard missing", library_input_path);
    }
    if (!read_file_contains(timeline_drop_path, "snprintf(path, sizeof(path), \"%s/%s\", state->library.directory,")) {
        failf("timeline reference-path guard missing", timeline_drop_path);
    }
    if (!read_file_contains(timeline_drop_path, "engine_add_clip_to_track_with_id(state->engine,")) {
        failf("timeline clip add guard missing", timeline_drop_path);
    }
}

int main(void) {
    test_roundtrip_and_recovery();
    test_output_root_session_resolution();
    test_source_contract_guards();
    if (g_failures != 0) {
        fprintf(stderr, "daw_data_path_contract_test: failed (%d)\n", g_failures);
        return 1;
    }
    printf("daw_data_path_contract_test: success\n");
    return 0;
}
