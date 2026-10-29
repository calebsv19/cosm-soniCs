#ifndef DAW_TEST_TEMP_PATH_H
#define DAW_TEST_TEMP_PATH_H

#include <stdio.h>
#include <stdlib.h>

// Returns the caller's temporary directory, retaining /tmp for ordinary local tests.
static inline const char* daw_test_temp_root(void) {
    const char* root = getenv("TMPDIR");
    return root && root[0] ? root : "/tmp";
}

// Formats one fixture name beneath the caller's temporary directory or fails closed.
static inline void daw_test_temp_path(char* destination, size_t capacity, const char* name) {
    int length = snprintf(destination, capacity, "%s/%s", daw_test_temp_root(), name);
    if (length < 0 || (size_t)length >= capacity) {
        fputs("test temporary path exceeds its buffer\n", stderr);
        abort();
    }
}

// Declares a writable fixture path while leaving mkstemp/mkdtemp ownership unchanged.
#define DAW_TEST_TEMP_PATH(variable, name) \
    char variable[4096]; \
    daw_test_temp_path(variable, sizeof(variable), name)

#endif
