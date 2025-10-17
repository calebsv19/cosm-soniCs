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

static bool equals_ignore_case(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (tolower(ca) != tolower(cb)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool parse_bool_value(const char* value, bool* out) {
    if (!value || !out) {
        return false;
    }
    if (equals_ignore_case(value, "true") || equals_ignore_case(value, "on") || strcmp(value, "1") == 0) {
        *out = true;
        return true;
    }
    if (equals_ignore_case(value, "false") || equals_ignore_case(value, "off") || strcmp(value, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

void config_set_defaults(EngineRuntimeConfig* cfg) {
    if (!cfg) {
        return;
    }
    cfg->sample_rate = 48000;
    cfg->block_size = 128;
    cfg->default_fade_in_ms = 0.0f;
    cfg->default_fade_out_ms = 0.0f;
    cfg->fade_preset_count = CONFIG_FADE_PRESET_MAX;
    const float defaults[CONFIG_FADE_PRESET_MAX] = {0.0f, 10.0f, 50.0f, 100.0f};
    for (int i = 0; i < CONFIG_FADE_PRESET_MAX; ++i) {
        cfg->fade_preset_ms[i] = defaults[i];
    }
    cfg->enable_engine_logs = false;
    cfg->enable_cache_logs = false;
    cfg->enable_timing_logs = false;
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
    } else if (strcmp(key, "fade_default_in_ms") == 0) {
        char* end = NULL;
        float val = strtof(value, &end);
        if (end != value) {
            if (val < 0.0f) {
                val = 0.0f;
            }
            cfg->default_fade_in_ms = val;
        }
    } else if (strcmp(key, "fade_default_out_ms") == 0) {
        char* end = NULL;
        float val = strtof(value, &end);
        if (end != value) {
            if (val < 0.0f) {
                val = 0.0f;
            }
            cfg->default_fade_out_ms = val;
        }
    } else if (strcmp(key, "fade_presets_ms") == 0) {
        float parsed[CONFIG_FADE_PRESET_MAX] = {0.0f};
        int count = 0;
        const char* cursor = value;
        while (*cursor && count < CONFIG_FADE_PRESET_MAX) {
            while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) {
                ++cursor;
            }
            if (*cursor == '\0') {
                break;
            }
            char* end = NULL;
            float val = strtof(cursor, &end);
            if (cursor == end) {
                break;
            }
            if (val < 0.0f) {
                val = 0.0f;
            }
            parsed[count++] = val;
            cursor = end;
        }
        if (count > 0) {
            cfg->fade_preset_count = count;
            for (int i = 0; i < count; ++i) {
                cfg->fade_preset_ms[i] = parsed[i];
            }
            for (int i = count; i < CONFIG_FADE_PRESET_MAX; ++i) {
                cfg->fade_preset_ms[i] = 0.0f;
            }
        }
    } else if (strcmp(key, "enable_engine_logs") == 0) {
        bool flag;
        if (parse_bool_value(value, &flag)) {
            cfg->enable_engine_logs = flag;
        }
    } else if (strcmp(key, "enable_cache_logs") == 0) {
        bool flag;
        if (parse_bool_value(value, &flag)) {
            cfg->enable_cache_logs = flag;
        }
    } else if (strcmp(key, "enable_timing_logs") == 0) {
        bool flag;
        if (parse_bool_value(value, &flag)) {
            cfg->enable_timing_logs = flag;
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
