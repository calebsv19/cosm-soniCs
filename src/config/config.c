#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void config_set_defaults(EngineRuntimeConfig* cfg) {
    if (!cfg) {
        return;
    }
    cfg->sample_rate = 48000;
    cfg->block_size = 128;
}

static void apply_entry(EngineRuntimeConfig* cfg, const char* key, const char* value) {
    if (!cfg || !key || !value) {
        return;
    }
    if (strcmp(key, "sample_rate") == 0) {
        int rate = atoi(value);
        if (rate > 0) {
            cfg->sample_rate = rate;
        }
    } else if (strcmp(key, "block_size") == 0) {
        int block = atoi(value);
        if (block > 0) {
            cfg->block_size = block;
        }
    }
}

bool config_load_file(const char* path, EngineRuntimeConfig* cfg) {
    if (!cfg) {
        return false;
    }
    config_set_defaults(cfg);
    if (!path) {
        return false;
    }

    FILE* file = fopen(path, "r");
    if (!file) {
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char* cursor = trim_leading(line);
        if (*cursor == '#' || *cursor == ';' || *cursor == '\0') {
            continue;
        }
        char* comment = strpbrk(cursor, "#;");
        if (comment) {
            *comment = '\0';
        }
        trim_trailing(cursor);
        char* equals = strchr(cursor, '=');
        if (!equals) {
            continue;
        }
        *equals = '\0';
        char* key = trim_leading(cursor);
        trim_trailing(key);
        char* value = trim_leading(equals + 1);
        trim_trailing(value);
        if (*key == '\0' || *value == '\0') {
            continue;
        }
        apply_entry(cfg, key, value);
    }

    fclose(file);
    return true;
}
