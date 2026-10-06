#define _POSIX_C_SOURCE 200809L
#include "daw/save_file.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Releases only resources owned by this candidate; published files are never removed.
void daw_save_file_abort(DawSaveFile* save) {
    if (!save) return;
    if (save->file) fclose(save->file);
    if (save->owns_temporary) unlink(save->temporary);
    if (save->directory_fd >= 0) close(save->directory_fd);
    free(save->destination);
    free(save->temporary);
    *save = (DawSaveFile){.directory_fd = -1};
}

// Opens the parent before staging and refuses nonregular destinations instead of replacing links.
bool daw_save_file_begin(DawSaveFile* save, const char* path) {
    if (!save) return false;
    *save = (DawSaveFile){.directory_fd = -1};
    if (!path || !path[0]) return false;
    struct stat existing;
    bool exists = lstat(path, &existing) == 0;
    if ((exists && !S_ISREG(existing.st_mode)) || (!exists && errno != ENOENT)) return false;
    size_t length = strlen(path);
    if (length > SIZE_MAX - 16) return false;
    save->destination = strdup(path);
    save->temporary = malloc(length + 16);
    char* parent = strdup(path);
    if (!save->destination || !save->temporary || !parent) {
        free(parent);
        // An allocated but uninitialized name must never be passed to unlink.
        free(save->temporary);
        save->temporary = NULL;
        daw_save_file_abort(save);
        return false;
    }
    snprintf(save->temporary, length + 16, "%s.tmp.XXXXXX", path);
    char* slash = strrchr(parent, '/');
    if (slash == parent) slash[1] = '\0';
    else if (slash) *slash = '\0';
    else strcpy(parent, ".");
    save->directory_fd = open(parent, O_RDONLY | O_DIRECTORY);
    free(parent);
    if (save->directory_fd < 0) {
        daw_save_file_abort(save);
        return false;
    }
    int fd = mkstemp(save->temporary);
    if (fd < 0) {
        // mkstemp did not create an owned file.
        free(save->temporary);
        save->temporary = NULL;
        daw_save_file_abort(save);
        return false;
    }
    save->owns_temporary = true;
    if (exists && fchmod(fd, existing.st_mode & 0777) != 0) {
        close(fd);
        daw_save_file_abort(save);
        return false;
    }
    save->file = fdopen(fd, "wb");
    if (!save->file) {
        close(fd);
        daw_save_file_abort(save);
        return false;
    }
    return true;
}

// Refuses publication if serialization, flush, data sync, or close reports an error.
bool daw_save_file_prepare(DawSaveFile* save) {
    if (!save || !save->file) return false;
    bool ok = ferror(save->file) == 0;
    if (fflush(save->file) != 0) ok = false;
    if (ok && fsync(fileno(save->file)) != 0) ok = false;
    if (fclose(save->file) != 0) ok = false;
    save->file = NULL;
    save->prepared = ok;
    return ok;
}

// Publishes once; a post-rename sync error is reported without pretending the replacement was undone.
DawSaveResult daw_save_file_commit(DawSaveFile* save) {
    if (!save || !save->prepared) return DAW_SAVE_FAILED;
    DawSaveResult result = DAW_SAVE_FAILED;
    if (rename(save->temporary, save->destination) == 0) {
        save->owns_temporary = false;
        result = fsync(save->directory_fd) == 0 ? DAW_SAVE_SYNCED : DAW_SAVE_PUBLISHED;
        if (result == DAW_SAVE_PUBLISHED)
            SDL_Log("save_file: published %s but directory sync failed: %s", save->destination, strerror(errno));
    }
    daw_save_file_abort(save);
    return result;
}

// Writes metadata through a private file and keeps the destination intact on prepublication failure.
DawSaveResult daw_save_file_write(const char* path, const void* data, size_t size) {
    if (!data && size) return DAW_SAVE_FAILED;
    DawSaveFile save;
    if (!daw_save_file_begin(&save, path)) return DAW_SAVE_FAILED;
    bool written = !size || fwrite(data, 1, size, save.file) == size;
    if (!written || !daw_save_file_prepare(&save)) {
        daw_save_file_abort(&save);
        return DAW_SAVE_FAILED;
    }
    return daw_save_file_commit(&save);
}

// Streams a prior valid file into a separately synced backup without modifying the source.
DawSaveResult daw_save_file_copy(const char* source, const char* destination) {
    FILE* input = fopen(source, "rb");
    if (!input) return DAW_SAVE_FAILED;
    DawSaveFile save;
    if (!daw_save_file_begin(&save, destination)) {
        fclose(input);
        return DAW_SAVE_FAILED;
    }
    unsigned char buffer[16384];
    size_t count;
    bool ok = true;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1, count, save.file) != count) { ok = false; break; }
    }
    if (ferror(input)) ok = false;
    if (fclose(input) != 0) ok = false;
    if (!ok || !daw_save_file_prepare(&save)) {
        daw_save_file_abort(&save);
        return DAW_SAVE_FAILED;
    }
    return daw_save_file_commit(&save);
}
