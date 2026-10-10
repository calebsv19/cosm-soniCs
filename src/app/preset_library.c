#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "daw/preset_library.h"
#include "daw/save_file.h"
#include "cJSON.h"
#include <SDL2/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

// Keeps names bounded, printable and well-formed UTF-8 without imposing filename rules on labels.
bool daw_preset_name_valid(const char* name) {
    if (!name || !name[0] || strlen(name) >= 96) return false;
    const unsigned char* p = (const unsigned char*)name;
    bool nonspace = false;
    while (*p) {
        unsigned c = *p++;
        if (c < 32 || c == 127) return false;
        if (c != ' ') nonspace = true;
        if (c < 128) continue;
        unsigned value, minimum; int count;
        if (c >= 0xc2 && c <= 0xdf) { value = c & 31; minimum = 128; count = 1; }
        else if (c >= 0xe0 && c <= 0xef) { value = c & 15; minimum = 2048; count = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { value = c & 7; minimum = 65536; count = 3; }
        else return false;
        for (int i = 0; i < count; ++i) { if ((*p & 0xc0) != 0x80) return false; value = (value << 6) | (*p++ & 63); }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return nonspace;
}
// Requires exact lowercase hexadecimal IDs, never paths or display names.
bool daw_preset_id_valid(const char* id) {
    return id && strlen(id) == 32 && strspn(id, "0123456789abcdef") == 32;
}
// Creates only the configured directory chain and refuses a symlink as the library itself.
bool daw_preset_library_root(char* out, size_t capacity, bool create) {
    const char* override = getenv("DAW_PRESET_LIBRARY");
    const char* home = getenv("HOME");
    int n = override ? snprintf(out, capacity, "%s", override)
        : home ? snprintf(out, capacity, "%s/Music/soniCs/User Library/Presets", home) : -1;
    if (n < 1 || (size_t)n >= capacity || out[0] != '/') return false;
    if (create) {
        char copy[DAW_PRESET_PATH_CAPACITY];
        if (strlen(out) >= sizeof(copy)) return false;
        strcpy(copy, out);
        for (char* p = copy + 1;; ++p) {
            if (*p && *p != '/') continue;
            char saved = *p; *p = 0;
            struct stat st;
            if (mkdir(copy, 0700) && errno != EEXIST) return false;
            if (stat(copy, &st) || !S_ISDIR(st.st_mode)) return false;
            *p = saved; if (!saved) break;
        }
    }
    struct stat st;
    return lstat(out, &st) ? !create && errno == ENOENT : S_ISDIR(st.st_mode);
}
// Reports stable reasons suitable for both the panel and the guarded MCP adapter.
const char* daw_preset_status_message(DawPresetStatus status) {
    const char* text[] = {"Preset saved or loaded.", "Invalid preset name, settings or identifier.",
        "Preset no longer exists.", "Preset changed; refresh before updating or applying.",
        "Preset storage failed; existing sound is preserved.", "Preset library is busy; retry after refreshing.",
        "Preset format or synthesis version is unsupported."};
    return status >= 0 && status <= DAW_PRESET_UNSUPPORTED ? text[status] : "Preset operation failed.";
}
// Rejects unknown and duplicated fields before interpreting a portable sound document.
static bool fields(const cJSON* object, const char* const* names) {
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON* a = object->child; a; a = a->next) {
        bool found = false;
        for (int i = 0; names[i]; ++i) if (!strcmp(a->string, names[i])) found = true;
        if (!found) return false;
        for (const cJSON* b = a->next; b; b = b->next) if (!strcmp(a->string, b->string)) return false;
    }
    return true;
}
// Copies strict JSON strings without coercion.
static const char* text(const cJSON* json, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
// Requires every physical parameter to be finite and within its supported storage range.
static bool valid_sound(EngineInstrumentPresetId base, EngineInstrumentParams params) {
    if (base < 0 || base >= ENGINE_INSTRUMENT_PRESET_COUNT) return false;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec; engine_instrument_param_spec(i, &spec);
        float value = engine_instrument_params_get(params, i);
        if (!isfinite(value) || value < spec.min_value || value > spec.max_value) return false;
    }
    return true;
}
// Parses all sound values and refuses future schemas instead of silently falling back to another voice.
static DawPresetStatus decode(const char* bytes, DawUserPreset* out) {
    static const char* const names[] = {"format", "version", "synthesis_version", "id", "revision", "name", "base_voice", "category", "parameters", NULL};
    cJSON* j = cJSON_ParseWithOpts(bytes, NULL, true);
    DawPresetStatus status = DAW_PRESET_INVALID;
    if (!fields(j, names)) goto done;
    const char* format = text(j, "format");
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(j, "version");
    const cJSON* synthesis = cJSON_GetObjectItemCaseSensitive(j, "synthesis_version");
    if (!format || strcmp(format, "sonics-instrument-preset") || !cJSON_IsNumber(version) ||
        !cJSON_IsNumber(synthesis)) goto done;
    if ((version->valuedouble != 1 && version->valuedouble != 2) || synthesis->valuedouble != 1) { status = DAW_PRESET_UNSUPPORTED; goto done; }
    const char *id = text(j, "id"), *revision = text(j, "revision"), *name = text(j, "name");
    DawUserPreset candidate = {0};
    if (!daw_preset_id_valid(id) || !daw_preset_id_valid(revision) || !daw_preset_name_valid(name) ||
        !engine_instrument_preset_from_id_string(text(j, "base_voice"), &candidate.base)) goto done;
    if (version->valuedouble == 1 && candidate.base > ENGINE_INSTRUMENT_PRESET_FILTERED_PAD) goto done;
    const char* category = text(j, "category");
    if (!category || strcmp(category, engine_instrument_preset_category_display_name(engine_instrument_preset_category(candidate.base)))) goto done;
    const cJSON* p = cJSON_GetObjectItemCaseSensitive(j, "parameters");
    int count = version->valuedouble == 1 ? 14 : ENGINE_INSTRUMENT_PARAM_COUNT;
    if (!cJSON_IsObject(p) || cJSON_GetArraySize(p) != count) goto done;
    candidate.params = engine_instrument_default_params(candidate.base);
    for (int i = 0; i < count; ++i) {
        EngineInstrumentParamSpec spec; engine_instrument_param_spec(i, &spec);
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(p, spec.id);
        if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < spec.min_value || value->valuedouble > spec.max_value) goto done;
        for (const cJSON* a = p->child; a; a = a->next) for (const cJSON* b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) goto done;
        candidate.params = engine_instrument_params_set(candidate.base, candidate.params, i, (float)value->valuedouble);
    }
    strcpy(candidate.id, id); strcpy(candidate.revision, revision); strcpy(candidate.name, name);
    *out = candidate; status = DAW_PRESET_OK;
done:
    cJSON_Delete(j); return status;
}
// Reads at most 16 KiB from a regular file without following a preset symlink.
static DawPresetStatus read_file(const char* path, DawUserPreset* out) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? DAW_PRESET_NOT_FOUND : DAW_PRESET_IO;
    struct stat st; char data[16385];
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 16384) { close(fd); return DAW_PRESET_INVALID; }
    size_t used = 0;
    while (used < (size_t)st.st_size) {
        ssize_t n = read(fd, data + used, (size_t)st.st_size - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return DAW_PRESET_IO; }
        used += (size_t)n;
    }
    close(fd);
    if (memchr(data, 0, used)) return DAW_PRESET_INVALID;
    data[used] = 0; return decode(data, out);
}
// Builds only controlled filenames below the personal root.
static bool path_for(char* path, const char* root, const char* id) {
    return daw_preset_id_valid(id) && snprintf(path, DAW_PRESET_PATH_CAPACITY, "%s/%s.sonics-preset.json", root, id) < DAW_PRESET_PATH_CAPACITY;
}
// Copies an addressed sound and checks the optional library revision independently of project state.
DawPresetStatus daw_preset_library_get(const char* id, const char* revision, DawUserPreset* out) {
    char root[DAW_PRESET_PATH_CAPACITY], path[DAW_PRESET_PATH_CAPACITY];
    if (!out || !daw_preset_id_valid(id) || (revision && !daw_preset_id_valid(revision))) return DAW_PRESET_INVALID;
    if (!daw_preset_library_root(root, sizeof(root), false) || !path_for(path, root, id)) return DAW_PRESET_IO;
    DawPresetStatus status = read_file(path, out);
    if (status == DAW_PRESET_OK && strcmp(out->id, id)) return DAW_PRESET_INVALID;
    return status == DAW_PRESET_OK && revision && strcmp(out->revision, revision) ? DAW_PRESET_CONFLICT : status;
}
// Orders labels deterministically while keeping distinct sounds with duplicate names addressable by ID.
static int compare(const void* a, const void* b) {
    const DawUserPreset *x = a, *y = b; int n = strcmp(x->name, y->name);
    return n ? n : strcmp(x->id, y->id);
}
// Scans a bounded personal library and reports malformed documents without loading them as sounds.
DawPresetStatus daw_preset_library_list(DawPresetList* out) {
    if (!out) return DAW_PRESET_INVALID;
    memset(out, 0, sizeof(*out));
    if (!daw_preset_library_root(out->root, sizeof(out->root), false)) return DAW_PRESET_IO;
    DIR* dir = opendir(out->root);
    if (!dir) return errno == ENOENT ? DAW_PRESET_OK : DAW_PRESET_IO;
    struct dirent* entry; int scanned = 0;
    while ((entry = readdir(dir))) {
        if (++scanned > 4096) { out->truncated = true; break; }
        size_t n = strlen(entry->d_name);
        if (n != 51 || strcmp(entry->d_name + 32, ".sonics-preset.json")) continue;
        char id[33]; memcpy(id, entry->d_name, 32); id[32] = 0;
        DawUserPreset preset;
        if (daw_preset_library_get(id, NULL, &preset) != DAW_PRESET_OK) { ++out->invalid_count; continue; }
        if (out->count == DAW_PRESET_CAPACITY) { out->truncated = true; continue; }
        out->items[out->count++] = preset;
    }
    closedir(dir); qsort(out->items, out->count, sizeof(out->items[0]), compare); return DAW_PRESET_OK;
}
// Generates identity bytes independently of names, addresses or the clock.
static bool new_id(char out[33]) {
    unsigned char bytes[16]; int fd = open("/dev/urandom", O_RDONLY); if (fd < 0) return false;
    ssize_t n = read(fd, bytes, sizeof(bytes)); close(fd); if (n != sizeof(bytes)) return false;
    for (int i = 0; i < 16; ++i) snprintf(out + i*2, 3, "%02x", bytes[i]);
    return true;
}
// Serializes the exact scalar sound using versioned app-owned JSON and the existing atomic save facade.
static char* encode(const DawUserPreset* preset) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "format", "sonics-instrument-preset");
    cJSON_AddNumberToObject(j, "version", 2); cJSON_AddNumberToObject(j, "synthesis_version", 1);
    cJSON_AddStringToObject(j, "id", preset->id); cJSON_AddStringToObject(j, "revision", preset->revision);
    cJSON_AddStringToObject(j, "name", preset->name);
    cJSON_AddStringToObject(j, "base_voice", engine_instrument_preset_id_string(preset->base));
    cJSON_AddStringToObject(j, "category", engine_instrument_preset_category_display_name(engine_instrument_preset_category(preset->base)));
    cJSON* p = cJSON_AddObjectToObject(j, "parameters");
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamSpec spec; engine_instrument_param_spec(i, &spec);
        cJSON_AddNumberToObject(p, spec.id, engine_instrument_params_get(preset->params, i));
    }
    char* bytes = cJSON_PrintUnformatted(j); cJSON_Delete(j); return bytes;
}
// Locks cooperating writers, checks explicit revisions, preserves the prior file, and atomically publishes a sound.
DawPresetStatus daw_preset_library_save(const char* name, EngineInstrumentPresetId base,
    EngineInstrumentParams params, const char* id, const char* revision, DawUserPreset* out, bool* durable) {
    if (durable) *durable = false;
    if (!out || !daw_preset_name_valid(name) || !valid_sound(base, params) || (!!id != !!revision) ||
        (id && (!daw_preset_id_valid(id) || !daw_preset_id_valid(revision)))) return DAW_PRESET_INVALID;
    char root[DAW_PRESET_PATH_CAPACITY], path[DAW_PRESET_PATH_CAPACITY], lock_path[DAW_PRESET_PATH_CAPACITY];
    if (!daw_preset_library_root(root, sizeof(root), true) || snprintf(lock_path, sizeof(lock_path), "%s/.library.lock", root) >= (int)sizeof(lock_path)) return DAW_PRESET_IO;
    int lock = open(lock_path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (lock < 0) return DAW_PRESET_IO;
    struct stat lock_stat;
    if (fstat(lock, &lock_stat) || !S_ISREG(lock_stat.st_mode)) { close(lock); return DAW_PRESET_IO; }
    if (flock(lock, LOCK_EX | LOCK_NB)) { close(lock); return DAW_PRESET_BUSY; }
    DawPresetStatus status = DAW_PRESET_IO;
    DawUserPreset preset = {.base = base, .params = params}; strcpy(preset.name, name);
    if (id) {
        DawUserPreset previous;
        status = daw_preset_library_get(id, revision, &previous);
        if (status != DAW_PRESET_OK) goto done;
        strcpy(preset.id, id);
    } else if (!new_id(preset.id)) goto done;
    if (!new_id(preset.revision) || !path_for(path, root, preset.id)) { status = DAW_PRESET_IO; goto done; }
    if (!id) { struct stat st; if (!lstat(path, &st) || errno != ENOENT) { status = DAW_PRESET_CONFLICT; goto done; } }
    char* bytes = encode(&preset); DawUserPreset decoded;
    if (!bytes || decode(bytes, &decoded) != DAW_PRESET_OK || !daw_preset_sound_equal(&decoded, base, params)) { free(bytes); status = DAW_PRESET_IO; goto done; }
    if (id) {
        char backup[DAW_PRESET_PATH_CAPACITY];
        if (snprintf(backup, sizeof(backup), "%s.previous", path) >= (int)sizeof(backup) || daw_save_file_copy(path, backup) == DAW_SAVE_FAILED) { free(bytes); status = DAW_PRESET_IO; goto done; }
    }
    DawSaveResult result = daw_save_file_write(path, bytes, strlen(bytes)); free(bytes);
    status = result == DAW_SAVE_FAILED ? DAW_PRESET_IO : DAW_PRESET_OK;
    if (status == DAW_PRESET_OK) { *out = preset; if (durable) *durable = result == DAW_SAVE_SYNCED; }
done:
    flock(lock, LOCK_UN); close(lock); return status;
}
// Compares values individually, avoiding padding and JSON string formatting assumptions.
bool daw_preset_sound_equal(const DawUserPreset* preset, EngineInstrumentPresetId base, EngineInstrumentParams params) {
    if (!preset || preset->base != base) return false;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i)
        if (engine_instrument_params_get(preset->params, i) != engine_instrument_params_get(params, i)) return false;
    return true;
}
