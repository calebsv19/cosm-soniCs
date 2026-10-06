#ifndef DAW_SAVE_FILE_H
#define DAW_SAVE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// Owns one same-directory temporary save until validation and atomic publication finish.
typedef struct DawSaveFile {
    FILE* file;
    char* destination;
    char* temporary;
    int directory_fd;
    bool prepared;
    bool owns_temporary;
} DawSaveFile;

// Distinguishes failed publication from a visible replacement whose directory sync failed.
typedef enum DawSaveResult {
    DAW_SAVE_FAILED = 0,
    DAW_SAVE_PUBLISHED,
    DAW_SAVE_SYNCED
} DawSaveResult;

// Creates a private temporary file without opening the destination for writing.
bool daw_save_file_begin(DawSaveFile* save, const char* path);
// Flushes, syncs, and closes the candidate so callers may validate its complete bytes.
bool daw_save_file_prepare(DawSaveFile* save);
// Renames a prepared candidate and reports whether directory synchronization also succeeded.
DawSaveResult daw_save_file_commit(DawSaveFile* save);
// Releases candidate storage without changing a previously published destination.
void daw_save_file_abort(DawSaveFile* save);
// Publishes a short metadata value using the same checked transaction.
DawSaveResult daw_save_file_write(const char* path, const void* data, size_t size);
// Copies an existing file into an independently prepared replacement.
DawSaveResult daw_save_file_copy(const char* source, const char* destination);

#endif
