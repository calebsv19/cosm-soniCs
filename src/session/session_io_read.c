#include "session.h"
#include "session_io_read_internal.h"

#include <SDL2/SDL.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool session_document_read_file(const char* path, SessionDocument* out_doc) {
    if (!out_doc) {
        return false;
    }
    session_document_reset(out_doc);
    if (!path || path[0] == '\0') {
        SDL_Log("session_document_read_file: path is empty");
        return false;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        SDL_Log("session_document_read_file: failed to open %s: %s", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        SDL_Log("session_document_read_file: failed to seek %s", path);
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0) {
        SDL_Log("session_document_read_file: failed to ftell %s", path);
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        SDL_Log("session_document_read_file: failed to rewind %s", path);
        fclose(file);
        return false;
    }
    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        SDL_Log("session_document_read_file: out of memory (%ld bytes)", size);
        fclose(file);
        return false;
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        SDL_Log("session_document_read_file: read mismatch for %s", path);
        free(buffer);
        return false;
    }
    buffer[size] = '\0';

    JsonReader reader;
    json_reader_init(&reader, buffer, (size_t)size);
    bool ok = parse_session_document(&reader, out_doc);
    free(buffer);
    if (!ok) {
        SDL_Log("session_document_read_file: failed to parse %s", path);
        session_document_reset(out_doc);
        return false;
    }

    char error[256] = {0};
    if (!session_document_validate(out_doc, error, sizeof(error))) {
        SDL_Log("session_document_read_file: validation failed for %s: %s", path, error[0] ? error : "unknown error");
        session_document_reset(out_doc);
        return false;
    }
    return true;
}
