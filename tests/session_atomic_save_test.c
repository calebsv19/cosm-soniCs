#define _POSIX_C_SOURCE 200809L
#include "session.h"
#include "app_state.h"
#include "session/project_manager.h"
#include "daw/save_file.h"
#include "audio/media_registry.h"
#include "test_assert.h"

#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(v, m) daw_test_expect("session_atomic_save_test", (v), (m))

static int fail_create, fail_flush, fail_close, fail_file_sync, fail_directory_sync;
static int fail_rename_at, rename_count, crash_rename_at, fail_copy_write;

// Injects failure before any candidate file is created.
static int save_test_mkstemp(char* path) {
    if (fail_create) { errno = ENOSPC; return -1; }
    return mkstemp(path);
}

// Models a buffered disk-full error even when the serializer's earlier writes succeeded.
static int save_test_fflush(FILE* file) {
    if (fail_flush) { errno = ENOSPC; return EOF; }
    return fflush(file);
}

// Closes the real descriptor while reporting a delayed write error to the caller.
static int save_test_fclose(FILE* file) {
    int result = fclose(file);
    if (fail_close) { errno = EIO; return EOF; }
    return result;
}

// Separates failures before publication from parent-directory failures after rename.
static int save_test_fsync(int fd) {
    struct stat info;
    CHECK(fstat(fd, &info) == 0, "sync descriptor invalid");
    if ((S_ISDIR(info.st_mode) && fail_directory_sync) || (!S_ISDIR(info.st_mode) && fail_file_sync)) {
        errno = EIO;
        return -1;
    }
    return fsync(fd);
}

// Fails or terminates at an exact backup/primary publication boundary.
static int save_test_rename(const char* from, const char* to) {
    ++rename_count;
    if (crash_rename_at == rename_count) _exit(73);
    if (fail_rename_at == rename_count) { errno = EIO; return -1; }
    return rename(from, to);
}

// Models a partial backup or metadata write without touching the published destination.
static size_t save_test_fwrite(const void* ptr, size_t size, size_t count, FILE* file) {
    if (fail_copy_write && count) return fwrite(ptr, size, count / 2, file);
    return fwrite(ptr, size, count, file);
}

#define mkstemp save_test_mkstemp
#define fflush save_test_fflush
#define fclose save_test_fclose
#define fsync save_test_fsync
#define rename save_test_rename
#define fwrite save_test_fwrite
#include "../src/session/save_file.c"
#undef mkstemp
#undef fflush
#undef fclose
#undef fsync
#undef rename
#undef fwrite

// Clears only test fault controls between independently checked save attempts.
static void reset_faults(void) {
    fail_create = fail_flush = fail_close = fail_file_sync = fail_directory_sync = 0;
    fail_rename_at = rename_count = crash_rename_at = fail_copy_write = 0;
}

// Confirms the complete on-disk session remains readable and has the expected revision marker.
static void expect_tempo(const char* path, float tempo) {
    SessionDocument doc;
    session_document_init(&doc);
    CHECK(session_document_read_file(path, &doc), "saved session is unreadable");
    CHECK(doc.tempo.bpm == tempo, "wrong saved revision");
    session_document_free(&doc);
}

// Counts uncommitted candidates so ordinary failures cannot silently leak files.
static int temporary_count(const char* directory) {
    DIR* dir = opendir(directory);
    CHECK(dir != NULL, "open test directory");
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir))) if (strstr(entry->d_name, ".tmp.")) ++count;
    closedir(dir);
    return count;
}

// Removes only entries inside this test's unique temporary directory, including crash leftovers.
static void cleanup_directory(const char* directory) {
    DIR* dir = opendir(directory);
    CHECK(dir != NULL, "cleanup directory");
    struct dirent* entry;
    char path[1024];
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        struct stat info;
        CHECK(lstat(path, &info) == 0, "inspect owned test entry");
        if (S_ISDIR(info.st_mode)) cleanup_directory(path);
        else CHECK(unlink(path) == 0, "cleanup owned test file");
    }
    closedir(dir);
    CHECK(rmdir(directory) == 0, "cleanup owned test directory");
}

// Confirms project identity and the startup marker advance only after session publication.
static void test_project_ordering(void) {
    AppState* state = calloc(1, sizeof(*state));
    CHECK(state != NULL, "project state allocation");
    config_set_defaults(&state->runtime_cfg);
    state->tempo = tempo_state_default(state->runtime_cfg.sample_rate);
    state->timeline_visible_seconds = 8;
    state->selected_track_index = state->selected_clip_index = -1;
    CHECK(project_manager_save(state, "first", true), "first project save");
    char first_path[SESSION_PATH_MAX];
    SDL_strlcpy(first_path, state->project.path, sizeof(first_path));
    reset_faults();
    fail_rename_at = 1;
    CHECK(!project_manager_save(state, "second", true), "rejected project save accepted");
    CHECK(!strcmp(state->project.path, first_path), "failed save changed project identity");
    reset_faults();
    fail_rename_at = 2;
    CHECK(project_manager_save(state, "second", true), "published session misreported as unsaved after marker failure");
    CHECK(strcmp(state->project.path, first_path) != 0, "published project identity not updated");
    reset_faults();
    expect_tempo(state->project.path, 120);
    char content[SESSION_PATH_MAX] = {0};
    FILE* file = fopen("config/projects/last_project.txt", "rb");
    CHECK(file && fread(content, 1, sizeof(content) - 1, file) > 0 && fclose(file) == 0,
          "read retained project marker");
    CHECK(!strcmp(content, first_path), "failed marker did not retain earlier project");
    free(state);
}

// Exercises actual session serialization and recovery through syscall failures and child interruption.
int main(void) {
    char directory[] = "/tmp/daw-atomic-save-XXXXXX";
    CHECK(mkdtemp(directory) != NULL, "create test directory");
    char previous_directory[4096];
    CHECK(getcwd(previous_directory, sizeof(previous_directory)) && chdir(directory) == 0, "isolate project paths");
    char path[1024], backup[1030], marker[1024], manifest[1024];
    snprintf(path, sizeof(path), "%s/project.json", directory);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    snprintf(marker, sizeof(marker), "%s/last_project.txt", directory);
    snprintf(manifest, sizeof(manifest), "%s/media.json", directory);
    SessionDocument doc;
    session_document_init(&doc);
    doc.tempo.bpm = 100;
    CHECK(session_document_write_file(&doc, path), "initial save");
    CHECK(access(backup, F_OK) != 0, "first save invented backup");
    CHECK(chmod(path, 0640) == 0, "fixture permissions");
    doc.tempo.bpm = 110;
    CHECK(session_document_write_file(&doc, path), "replacement save");
    expect_tempo(path, 110);
    expect_tempo(backup, 100);
    struct stat info;
    CHECK(stat(path, &info) == 0 && (info.st_mode & 0777) == 0640, "replacement changed permissions");
    doc.tempo.bpm = 120;

    for (int failure = 0; failure < 7; ++failure) {
        reset_faults();
        switch (failure) {
            case 0: fail_create = 1; break;
            case 1: fail_flush = 1; break;
            case 2: fail_close = 1; break;
            case 3: fail_file_sync = 1; break;
            case 4: fail_copy_write = 1; break;
            case 5: fail_rename_at = 1; break;
            case 6: fail_rename_at = 2; break;
        }
        CHECK(!session_document_write_file(&doc, path), "injected save failure accepted");
        reset_faults();
        expect_tempo(path, 110);
        expect_tempo(backup, failure == 6 ? 110 : 100);
        CHECK(temporary_count(directory) == 0, "failed save leaked candidate");
    }
    // A non-finite document must not publish syntactically invalid JSON over a good save.
    doc.tempo.bpm = NAN;
    CHECK(!session_document_write_file(&doc, path), "non-finite serialized candidate accepted");
    expect_tempo(path, 110);
    doc.tempo.bpm = 120;
    CHECK(session_document_write_file(&doc, path), "successful save retry");
    expect_tempo(path, 120);
    expect_tempo(backup, 110);
    SessionDocument preferred;
    session_document_init(&preferred);
    bool used_backup = true;
    CHECK(session_document_read_recoverable(path, &preferred, &used_backup) && !used_backup && preferred.tempo.bpm == 120,
          "valid primary did not take precedence over backup");
    session_document_free(&preferred);

    for (int boundary = 1; boundary <= 2; ++boundary) {
        pid_t child = fork();
        CHECK(child >= 0, "fork interruption fixture");
        if (!child) {
            reset_faults();
            crash_rename_at = boundary;
            doc.tempo.bpm = 130;
            (void)session_document_write_file(&doc, path);
            _exit(74);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 73,
              "child did not interrupt at publication boundary");
        expect_tempo(path, 120);
        expect_tempo(backup, boundary == 1 ? 110 : 120);
    }
    // A missing/corrupt primary recovers the prior valid revision without rewriting either file.
    FILE* corrupt = fopen(path, "wb");
    CHECK(corrupt && fputs("{broken", corrupt) >= 0 && fclose(corrupt) == 0, "corrupt primary fixture");
    SessionDocument loaded;
    session_document_init(&loaded);
    bool recovered = false;
    CHECK(session_document_read_recoverable(path, &loaded, &recovered) && recovered && loaded.tempo.bpm == 120,
          "corrupt-primary recovery failed");
    session_document_free(&loaded);
    CHECK(session_document_write_file(&doc, path), "save after recovery");
    expect_tempo(backup, 120);
    CHECK(unlink(path) == 0, "missing-primary fixture");
    session_document_init(&loaded);
    CHECK(session_document_read_recoverable(path, &loaded, &recovered) && recovered, "missing-primary recovery failed");
    session_document_free(&loaded);
    CHECK(session_document_write_file(&doc, path), "restore missing primary");
    FILE* trailing = fopen(path, "ab");
    CHECK(trailing && fputs("garbage", trailing) >= 0 && fclose(trailing) == 0, "trailing garbage fixture");
    session_document_init(&loaded);
    CHECK(!session_document_read_file(path, &loaded), "trailing corruption accepted");
    session_document_free(&loaded);

    FILE* broken_backup = fopen(backup, "wb");
    CHECK(broken_backup && fputs("broken", broken_backup) >= 0 && fclose(broken_backup) == 0, "invalid backup fixture");
    session_document_init(&loaded);
    CHECK(!session_document_read_recoverable(path, &loaded, &recovered) && !recovered,
          "both invalid sessions reported successful recovery");
    session_document_free(&loaded);
    char link_path[1024];
    snprintf(link_path, sizeof(link_path), "%s/linked-session", directory);
    CHECK(symlink(path, link_path) == 0, "symlink fixture");
    CHECK(daw_save_file_write(link_path, "replacement", 11) == DAW_SAVE_FAILED,
          "save replaced a symlink destination");
    CHECK(lstat(link_path, &info) == 0 && S_ISLNK(info.st_mode), "symlink was modified");

    CHECK(daw_save_file_write(marker, "old", 3) == DAW_SAVE_SYNCED, "initial marker");
    fail_rename_at = 1; rename_count = 0;
    CHECK(daw_save_file_write(marker, "new", 3) == DAW_SAVE_FAILED, "failed marker accepted");
    reset_faults();
    char bytes[4] = {0};
    FILE* file = fopen(marker, "rb");
    CHECK(file && fread(bytes, 1, 3, file) == 3 && fclose(file) == 0 && !strcmp(bytes, "old"), "marker truncated on rejection");
    fail_directory_sync = 1;
    CHECK(daw_save_file_write(marker, "new", 3) == DAW_SAVE_PUBLISHED, "post-rename sync status incorrect");
    reset_faults();
    file = fopen(marker, "rb");
    CHECK(file && fread(bytes, 1, 3, file) == 3 && fclose(file) == 0 && !strcmp(bytes, "new"), "published marker missing");

    MediaRegistry registry;
    media_registry_init(&registry, manifest);
    registry.dirty = true;
    fail_flush = 1;
    CHECK(!media_registry_save(&registry) && registry.dirty, "failed registry save lost dirty state");
    reset_faults();
    CHECK(media_registry_save(&registry) && !registry.dirty, "registry retry failed");
    registry.dirty = true; fail_rename_at = 1; rename_count = 0;
    CHECK(!media_registry_save(&registry) && registry.dirty, "registry replacement failure accepted");
    reset_faults();
    CHECK(media_registry_load(&registry), "prior registry unreadable after failed replacement");
    media_registry_shutdown(&registry);

    test_project_ordering();
    session_document_free(&doc);
    CHECK(chdir(previous_directory) == 0, "restore working directory");
    cleanup_directory(directory);
    puts("session_atomic_save_test: success (7 failure stages, 2 interrupted publications, backup recovery, metadata retry)");
    return 0;
}
