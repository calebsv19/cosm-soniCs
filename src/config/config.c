#include "config.h"

#include <SDL2/SDL.h>

#include <ctype.h>
#include <errno.h>
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

static void log_config_diag(const char* path, int line_number, const char* key, const char* message, const char* value) {
    SDL_Log("config_load_file: %s:%d key=%s value=%s %s",
            path ? path : "(null)",
            line_number,
            key ? key : "(none)",
            value ? value : "(none)",
            message ? message : "diagnostic");
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

static void apply_entry(EngineRuntimeConfig* cfg, const char* key, const char* value, const char* path, int line_number) {
    if (!cfg || !key || !value) {
        return;
    }
    if (strcmp(key, "sample_rate") == 0) {
        int rate = atoi(value);
        if (rate > 0) {
            cfg->sample_rate = rate;
        } else {
            log_config_diag(path, line_number, key, "ignored invalid positive integer; keeping default/current value", value);
        }
    } else if (strcmp(key, "block_size") == 0) {
        int block = atoi(value);
        if (block > 0) {
            cfg->block_size = block;
        } else {
            log_config_diag(path, line_number, key, "ignored invalid positive integer; keeping default/current value", value);
        }
    } else if (strcmp(key, "fade_default_in_ms") == 0) {
        char* end = NULL;
        float val = strtof(value, &end);
        if (end != value) {
            if (end && *end != '\0') {
                log_config_diag(path, line_number, key, "accepted numeric prefix and ignored trailing characters", value);
            }
            if (val < 0.0f) {
                log_config_diag(path, line_number, key, "clamped negative value to 0.0", value);
                val = 0.0f;
            }
            cfg->default_fade_in_ms = val;
        } else {
            log_config_diag(path, line_number, key, "ignored invalid float; keeping default/current value", value);
        }
    } else if (strcmp(key, "fade_default_out_ms") == 0) {
        char* end = NULL;
        float val = strtof(value, &end);
        if (end != value) {
            if (end && *end != '\0') {
                log_config_diag(path, line_number, key, "accepted numeric prefix and ignored trailing characters", value);
            }
            if (val < 0.0f) {
                log_config_diag(path, line_number, key, "clamped negative value to 0.0", value);
                val = 0.0f;
            }
            cfg->default_fade_out_ms = val;
        } else {
            log_config_diag(path, line_number, key, "ignored invalid float; keeping default/current value", value);
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
                log_config_diag(path, line_number, key, "stopped parsing at invalid fade preset token", cursor);
                break;
            }
            if (val < 0.0f) {
                log_config_diag(path, line_number, key, "clamped negative fade preset to 0.0", cursor);
                val = 0.0f;
            }
            parsed[count++] = val;
            cursor = end;
        }
        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) {
            ++cursor;
        }
        if (*cursor != '\0' && count >= CONFIG_FADE_PRESET_MAX) {
            log_config_diag(path, line_number, key, "ignored extra fade presets beyond configured maximum", cursor);
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
        } else {
            log_config_diag(path, line_number, key, "ignored invalid boolean; keeping default/current value", value);
        }
    } else if (strcmp(key, "enable_cache_logs") == 0) {
        bool flag;
        if (parse_bool_value(value, &flag)) {
            cfg->enable_cache_logs = flag;
        } else {
            log_config_diag(path, line_number, key, "ignored invalid boolean; keeping default/current value", value);
        }
    } else if (strcmp(key, "enable_timing_logs") == 0) {
        bool flag;
        if (parse_bool_value(value, &flag)) {
            cfg->enable_timing_logs = flag;
        } else {
            log_config_diag(path, line_number, key, "ignored invalid boolean; keeping default/current value", value);
        }
    } else {
        log_config_diag(path, line_number, key, "ignored unknown key", value);
    }
}

bool config_load_file(const char* path, EngineRuntimeConfig* cfg) {
    if (!cfg) {
        return false;
    }
    config_set_defaults(cfg);
    if (!path) {
        SDL_Log("config_load_file: path is null; using defaults");
        return false;
    }

    FILE* file = fopen(path, "r");
    if (!file) {
        SDL_Log("config_load_file: failed to open %s: %s; using defaults", path, strerror(errno));
        return false;
    }

    char line[256];
    int line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        ++line_number;
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
            log_config_diag(path, line_number, NULL, "ignored malformed line without '='", cursor);
            continue;
        }
        *equals = '\0';
        char* key = trim_leading(cursor);
        trim_trailing(key);
        char* value = trim_leading(equals + 1);
        trim_trailing(value);
        if (*key == '\0' || *value == '\0') {
            log_config_diag(path, line_number, *key ? key : NULL, "ignored empty key or value", value);
            continue;
        }
        apply_entry(cfg, key, value, path, line_number);
    }

    fclose(file);
    return true;
}
