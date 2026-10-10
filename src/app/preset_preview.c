#define _POSIX_C_SOURCE 200809L
#include "daw/preset_actions.h"
#include "daw/sound_inspection.h"
#include "daw/save_file.h"
#include "app_state.h"
#include <math.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
// Writes an explicit little-endian integer independently of host byte order.
static void u32(FILE* file, uint32_t n) {
    unsigned char b[] = {n & 255, (n >> 8) & 255, (n >> 16) & 255, n >> 24}; fwrite(b, 1, 4, file);
}
// Produces fixed C4/chord PCM examples with original level, finite sample checks and no normalization.
DawPresetStatus daw_action_preview_preset(AppState* state, const char* id, const char* revision,
    bool chord, DawPresetPreview* out) {
    if (!state || !state->engine || !engine_is_control_thread(state->engine) || !out) return DAW_PRESET_INVALID;
    if (state->bounce_active || state->undo.active_drag_valid || daw_audio_recording_is_active(&state->audio_recording)) return DAW_PRESET_BUSY;
    memset(out, 0, sizeof(*out)); DawUserPreset preset;
    DawPresetStatus status = daw_preset_library_get(id, revision, &preset);
    if (status != DAW_PRESET_OK) return status;
    char root[DAW_PRESET_PATH_CAPACITY], directory[DAW_PRESET_PATH_CAPACITY];
    if (!daw_preset_library_root(root, sizeof(root), true) ||
        snprintf(directory, sizeof(directory), "%s/Previews", root) >= (int)sizeof(directory)) return DAW_PRESET_IO;
    struct stat st;
    if ((mkdir(directory, 0700) && errno != EEXIST) || lstat(directory, &st) || !S_ISDIR(st.st_mode) ||
        snprintf(out->path, sizeof(out->path), "%s/%s-%s-%s.wav", directory, id, preset.revision, chord ? "chord" : "note") >= (int)sizeof(out->path)) return DAW_PRESET_IO;
    DawSoundRender render;
    if (!daw_sound_render((DawSoundSpec){preset.base,preset.params,chord}, &render)) return DAW_PRESET_INVALID;
    if (render.peak > 1) { daw_sound_render_free(&render); return DAW_PRESET_INVALID; }
    DawSaveFile save;
    if (!daw_save_file_begin(&save,out->path)) {daw_sound_render_free(&render);return DAW_PRESET_IO;}
    fwrite("RIFF", 1, 4, save.file); u32(save.file, 36 + 144000*4); fwrite("WAVEfmt ", 1, 8, save.file);
    u32(save.file, 16); u32(save.file, 0x00020001); u32(save.file, 48000); u32(save.file, 48000*4);
    u32(save.file, 0x00100004); fwrite("data", 1, 4, save.file); u32(save.file, 144000*4);
    double sum = 0; bool valid = true;
    for (uint64_t frame = 0; frame < 144000 && valid; frame += 256) {
        int count = 144000 - frame < 256 ? (int)(144000 - frame) : 256;
        const float* data = render.samples + frame*2;
        unsigned char pcm[1024];
        for (int i = 0; i < count*2; ++i) {
            double value = data[i]; if (!isfinite(value) || fabs(value) > 1) { valid = false; break; }
            if (fabs(value) > out->peak) out->peak = fabs(value);
            sum += value*value;
            int16_t sample = (int16_t)lrint(value * 32767);
            pcm[i*2] = (uint16_t)sample & 255; pcm[i*2+1] = (uint16_t)sample >> 8;
        }
        if (valid && fwrite(pcm, 4, count, save.file) != (size_t)count) valid = false;
    }
    daw_sound_render_free(&render);
    out->rms = sqrt(sum / (144000*2));
    if (!valid || !daw_save_file_prepare(&save)) { daw_save_file_abort(&save); return DAW_PRESET_INVALID; }
    DawSaveResult result = daw_save_file_commit(&save); daw_save_file_abort(&save);
    out->durable = result == DAW_SAVE_SYNCED;
    return result == DAW_SAVE_FAILED ? DAW_PRESET_IO : DAW_PRESET_OK;
}
