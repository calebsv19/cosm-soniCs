#ifndef DAW_TEST_ASSERT_H
#define DAW_TEST_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

static inline void daw_test_fail(const char* test_name, const char* message) {
    fprintf(stderr, "%s: %s\n", test_name ? test_name : "daw_test", message ? message : "assertion failed");
    exit(1);
}

static inline void daw_test_expect(const char* test_name, int condition, const char* message) {
    if (!condition) {
        daw_test_fail(test_name, message);
    }
}

#endif
