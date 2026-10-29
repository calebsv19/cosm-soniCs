#include "audio/audio_device.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define COMPARISON_MAX_BYTES (64u * 1024u * 1024u)
// Publishes complete immutable PCM only after both copies and all gain/headroom checks succeed.
bool audio_device_comparison_play(AudioDevice* d, const float* a, const float* b, uint64_t frames,
                                  int channels, int rate, const double gains[4], uint64_t id,
                                  int selection) {
    if (!d || !d->is_open || !a || !b || !gains || !id || selection < 0 || selection > 3 ||
        !frames || channels != d->spec.channels || rate != d->spec.sample_rate || channels < 1 ||
        frames > COMPARISON_MAX_BYTES / (2 * sizeof(float)) / (unsigned)channels)
        return false;
    if (audio_device_preview_active(d) && atomic_load(&d->preview_pair_id) != id)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!isfinite(gains[i]) || gains[i] < 0 || gains[i] > 1)
            return false;
    size_t count = (size_t)frames * channels, bytes = count * sizeof(float);
    for (size_t n = 0; n < count; ++n) {
        if (!isfinite(a[n]) || !isfinite(b[n]))
            return false;
        for (int k = 0; k < 4; ++k)
            if (fabs((k % 2 ? b[n] : a[n]) * gains[k]) > .951)
                return false;
    }
    float* first = malloc(bytes);
    if (!first)
        return false;
    float* second = malloc(bytes);
    if (!second) {
        free(first);
        return false;
    }
    memcpy(first, a, bytes);
    memcpy(second, b, bytes);
    SDL_LockAudioDevice(d->device_id);
    float* old_a = d->preview_pcm;
    float* old_b = d->preview_pair_b;
    d->preview_pcm = first;
    d->preview_pair_b = second;
    d->preview_frames = frames;
    d->preview_cursor = 0;
    atomic_store(&d->preview_position, 0);
    memcpy(d->preview_gains, gains, sizeof(d->preview_gains));
    memset(d->preview_weights, 0, sizeof(d->preview_weights));
    d->preview_weights[selection] = 1;
    d->preview_target = selection;
    d->preview_fade_length = (unsigned)fmax(2, round(rate * .005));
    d->preview_fade = d->preview_fade_length;
    atomic_store(&d->preview_selection, selection);
    atomic_store(&d->preview_pair_id, id);
    atomic_store_explicit(&d->preview_active, true, memory_order_release);
    SDL_UnlockAudioDevice(d->device_id);
    free(old_a);
    free(old_b);
    return true;
}
// Delivers a bounded selection intent to the callback while retaining buffer and cursor ownership.
bool audio_device_comparison_select(AudioDevice* d, uint64_t id, int selection) {
    if (!d || !id || selection < 0 || selection > 3 || atomic_load(&d->preview_pair_id) != id ||
        !audio_device_preview_active(d))
        return false;
    atomic_store_explicit(&d->preview_selection, selection, memory_order_release);
    return true;
}
// Retires only the caller's comparison through the existing callback-exclusion path.
bool audio_device_comparison_stop(AudioDevice* d, uint64_t id) {
    if (!d || !id || atomic_load(&d->preview_pair_id) != id)
        return false;
    audio_device_preview_stop(d);
    return true;
}
// Keeps all side/mode transitions convex, aligned and independent of callback partition sizes.
void audio_device_comparison_render(AudioDevice* d, float* out, int frames, int channels) {
    int selected = atomic_load_explicit(&d->preview_selection, memory_order_acquire);
    if (selected != d->preview_target) {
        memcpy(d->preview_from, d->preview_weights, sizeof(d->preview_from));
        d->preview_target = selected;
        d->preview_fade = 0;
    }
    uint64_t available = d->preview_frames - d->preview_cursor;
    uint64_t count = available < (uint64_t)frames ? available : (uint64_t)frames;
    memset(out, 0, (size_t)frames * channels * sizeof(float));
    for (uint64_t f = 0; f < count; ++f) {
        if (d->preview_fade < d->preview_fade_length) {
            float t = (float)d->preview_fade / (d->preview_fade_length - 1);
            for (int k = 0; k < 4; ++k)
                d->preview_weights[k] = d->preview_from[k] * (1 - t) + (k == selected ? t : 0);
            ++d->preview_fade;
        }
        for (int ch = 0; ch < channels; ++ch) {
            size_t n = (size_t)(d->preview_cursor + f) * channels + ch;
            double value = 0;
            for (int k = 0; k < 4; ++k)
                value += d->preview_weights[k] *
                         (k % 2 ? d->preview_pair_b[n] : d->preview_pcm[n]) * d->preview_gains[k];
            out[f * channels + ch] = (float)value;
        }
    }
    d->preview_cursor += count;
    atomic_store(&d->preview_position, d->preview_cursor);
    if (d->preview_cursor == d->preview_frames)
        atomic_store_explicit(&d->preview_active, false, memory_order_release);
}
