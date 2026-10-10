#define _POSIX_C_SOURCE 200809L
#include "daw/composition.h"
#include "daw/preset_library.h"
#include "audio/wav_writer.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Publishes caller-owned captured samples below the private library without normalizing or overwriting files.
DawCompositionPublishStatus daw_composition_publish(const DawCompositionRender* render, const char* artifact, DawCompositionPreview* out) {
    if (!render || !artifact || !out || !render->audio.data || !render->audio.frame_count || render->audio.channels <= 0 ||
        render->audio.sample_rate <= 0 || !*artifact || strlen(artifact) > 95 || render->audio.frame_count > UINT64_C(8388608) || render->audio.channels > 8) return DAW_COMPOSITION_PUBLISH_INVALID;
    for (const char* p = artifact; *p; ++p) if (!((*p >= 'a' && *p <= 'f') || (*p >= '0' && *p <= '9') || *p == '-')) return DAW_COMPOSITION_PUBLISH_INVALID;
    *out = (DawCompositionPreview){0};
    uint64_t samples = render->audio.frame_count * (uint64_t)render->audio.channels;
    for (uint64_t i = 0; i < samples; ++i) {
        if (!isfinite(render->audio.data[i])) return DAW_COMPOSITION_PUBLISH_INVALID;
        if (fabsf(render->audio.data[i]) > 1) return DAW_COMPOSITION_PUBLISH_CLIPPING;
    }
    char root[DAW_PRESET_PATH_CAPACITY], directory[DAW_PRESET_PATH_CAPACITY];
    if (!daw_preset_library_root(root, sizeof(root), true) || snprintf(directory, sizeof(directory), "%s/CompositionPreviews", root) >= (int)sizeof(directory)) return DAW_COMPOSITION_PUBLISH_IO;
    struct stat st;
    if ((mkdir(directory, 0700) && errno != EEXIST) || lstat(directory, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) return DAW_COMPOSITION_PUBLISH_IO;
    if (snprintf(out->path, sizeof(out->path), "%s/%s.wav", directory, artifact) >= (int)sizeof(out->path)) return DAW_COMPOSITION_PUBLISH_IO;
    if (!lstat(out->path, &st) || errno != ENOENT) return DAW_COMPOSITION_PUBLISH_OCCUPIED;
    const EngineBounceBuffer* audio = &render->audio;
    DawSaveResult saved = wav_write_pcm16_dithered_result(out->path, audio->data, audio->frame_count, audio->channels, audio->sample_rate, UINT32_C(0x534f4e49));
    if (saved == DAW_SAVE_FAILED) return DAW_COMPOSITION_PUBLISH_IO;
    out->durable = saved == DAW_SAVE_SYNCED;
    return DAW_COMPOSITION_PUBLISH_OK;
}
