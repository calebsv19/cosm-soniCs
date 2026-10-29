#include "audio/audio_device.h"
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
static bool callback;
static int allocations, fail_at = -1, locks;
// Proves that callback processing cannot allocate and allows exact control-path failure injection.
static void* allocate(size_t bytes) {
    assert(!callback);
    int n = allocations++;
    return n == fail_at ? NULL : malloc(bytes);
}
// Detects accidental callback retirement while keeping control-path ownership explicit.
static void deallocate(void* p) {
    assert(!callback);
    free(p);
}
// Requires callback exclusion on pointer publication without a real output endpoint.
static void lock(SDL_AudioDeviceID id) {
    assert(id == 42 && !callback && !locks);
    ++locks;
}
// Requires complete matching exclusion before retiring old copies.
static void unlock(SDL_AudioDeviceID id) {
    assert(id == 42 && !callback && locks == 1);
    --locks;
}
#define free deallocate
#define malloc allocate
#define SDL_LockAudioDevice lock
#define SDL_UnlockAudioDevice unlock
#include "../src/audio/device_comparison.c"
#include "../src/audio/device_preview.c"
#undef malloc
#undef free
// Renders only through the production callback entry with allocation/lock guards active.
static void render(AudioDevice* d, float* out, int frames) {
    callback = true;
    audio_device_preview_render(d, out, frames, 2);
    callback = false;
}
// Qualifies exact alignment, attenuation, retargeting, block parity and exclusive audition
// ownership.
int main(void) {
    float a[4096], b[4096], out[4096], partition[4096];
    for (int f = 0; f < 2048; ++f) {
        float v = .1f + f * .0001f;
        a[f * 2] = v;
        a[f * 2 + 1] = -v;
        b[f * 2] = 2 * v;
        b[f * 2 + 1] = -2 * v;
    }
    double gains[4] = {1, .5, 1, 1};
    AudioDevice d = {.device_id = 42, .is_open = true, .spec = {48000, 128, 2}}, other = d;
    assert(audio_device_comparison_play(&d, a, b, 2048, 2, 48000, gains, 7, 0));
    a[0] = .9;
    assert(d.preview_pcm[0] == .1f);
    a[0] = .1;
    assert(audio_device_comparison_play(&other, a, b, 2048, 2, 48000, gains, 7, 0));
    render(&d, out, 123);
    render(&other, partition, 123);
    assert(!memcmp(out, partition, 123 * 2 * sizeof(float)));
    assert(atomic_load(&d.preview_position) == 123);
    assert(audio_device_comparison_select(&d, 7, 1) &&
           audio_device_comparison_select(&other, 7, 1));
    render(&d, out, 300);
    render(&other, partition, 73);
    render(&other, partition + 73 * 2, 227);
    assert(!memcmp(out, partition, 300 * 2 * sizeof(float)));
    for (int f = 0; f < 300; ++f)
        assert(fabs(out[f * 2] - a[(f + 123) * 2]) < 1e-6);
    assert(d.preview_cursor == 423);
    assert(audio_device_comparison_select(&d, 7, 3));
    render(&d, out, 100);
    float old[4];
    memcpy(old, d.preview_weights, sizeof(old));
    assert(audio_device_comparison_select(&d, 7, 2));
    render(&d, out, 240);
    double expected = a[523 * 2] * (old[0] + old[1] + old[2] + 2 * old[3]);
    assert(fabs(out[0] - expected) < 1e-6 && fabs(out[239 * 2] - a[762 * 2]) < 1e-6);
    float* accepted = d.preview_pcm;
    uint64_t cursor = d.preview_cursor;
    fail_at = allocations + 1;
    assert(!audio_device_comparison_play(&d, a, b, 2048, 2, 48000, gains, 7, 0));
    fail_at = -1;
    assert(d.preview_pcm == accepted && d.preview_cursor == cursor);
    assert(!audio_device_comparison_select(&d, 8, 0) &&
           !audio_device_comparison_play(&d, a, b, 2048, 2, 48000, gains, 8, 0));
    double bad[4] = {1, 2, 1, 1};
    assert(!audio_device_comparison_play(&d, a, b, 2048, 2, 48000, bad, 7, 0));
    assert(!audio_device_comparison_play(&d, a, b, UINT64_MAX, 2, 48000, gains, 7, 0));
    b[0] = NAN;
    assert(!audio_device_comparison_play(&d, a, b, 2048, 2, 48000, gains, 7, 0));
    b[0] = .2;
    render(&d, out, 2048);
    assert(!audio_device_preview_active(&d) && atomic_load(&d.preview_position) == 2048);
    for (int f = (int)(2048 - cursor); f < 2048; ++f)
        assert(out[f * 2] == 0 && out[f * 2 + 1] == 0);
    assert(audio_device_preview_play(&d, a, 2048, 2, 48000));
    assert(!audio_device_comparison_stop(&d, 7) && audio_device_preview_active(&d));
    audio_device_preview_stop(&d);
    audio_device_preview_stop(&other);
    assert(!locks && !d.preview_pcm && !d.preview_pair_b);
    puts("comparison_player_test: passed (no callback allocations/locks, exact shared cursor, "
         "matched PCM, convex retargeting, partition parity, two-copy failure preservation, "
         "ownership and finite completion)");
    return 0;
}
