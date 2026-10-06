#include "effects/effects_manager.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// Models a history-bearing effect whose output reveals state resets and incorrect ownership.
struct FxHandle { float history; float increment; };
static int live_handles, creations, destructions, fail_after = -1;

// Describes the deterministic stateful fixture effect.
static int describe(FxDesc* desc) {
    *desc = (FxDesc){.api_version = FX_API_VERSION, .flags = FX_FLAG_INPLACE_OK,
                     .num_inputs = 1, .num_outputs = 1, .num_params = 1,
                     .param_defaults = {1.0f}};
    return 1;
}
// Advances one persistent state per render call to expose history continuity.
static void process(FxHandle* handle, const float* input, float* output, int frames, int channels) {
    (void)input;
    handle->history += handle->increment;
    for (int i = 0; i < frames * channels; ++i) output[i] = handle->history;
}
// Changes the fixture increment without clearing its history.
static void set_param(FxHandle* handle, uint32_t index, float value) { assert(index == 0); handle->increment = value; }
// Resets the history of a newly prepared fixture.
static void reset(FxHandle* handle) { handle->history = 0.0f; }
// Counts releases so canceled and retired revisions cannot silently leak handles.
static void destroy(FxHandle* handle) { --live_handles; ++destructions; free(handle); }
// Allocates a fixture handle or injects a deterministic preparation failure.
static int create(const FxDesc* desc, FxHandle** handle, FxVTable* vt, uint32_t rate, uint32_t block, uint32_t channels) {
    (void)desc; (void)rate; (void)block; (void)channels;
    if (fail_after == 0) return 0;
    if (fail_after > 0) --fail_after;
    *handle = calloc(1, sizeof(**handle));
    if (!*handle) return 0;
    ++live_handles; ++creations;
    *vt = (FxVTable){.process = process, .set_param = set_param, .reset = reset, .destroy = destroy};
    return 1;
}
// Renders one frame from a track to observe its persistent fixture history.
static float render(EffectsManager* manager, int track) {
    float output[2] = {0};
    fxm_render_track(manager, track, output, 1, 2);
    assert(output[0] == output[1]);
    return output[0];
}

// Verifies independent control/render ownership, history transfer, rollback, and final reclamation.
int main(void) {
    FxConfig config = {.sample_rate = 48000, .max_block = 128, .max_channels = 2};
    EffectsManager* control = fxm_create(&config);
    FxRegistryEntry entry = {.id = 999, .name = "History fixture", .get_desc = describe, .create = create};
    assert(control && fxm_register_builtin(control, &entry, 1) && fxm_set_track_count(control, 2));
    FxInstId first = fxm_track_add(control, 0, 999);
    FxInstId second = fxm_track_add(control, 1, 999);
    assert(first && second && first != second);
    EffectsManager* active = fxm_clone_for_render(control);
    assert(active && live_handles == 4);
    fail_after = 1;
    assert(!fxm_clone_for_render(control) && live_handles == 4);
    fail_after = -1;
    assert(render(active, 0) == 1.0f && render(active, 0) == 2.0f);
    assert(render(active, 1) == 1.0f);
    EffectsManager* next = fxm_clone_for_render(control);
    int identity[] = {0, 1};
    int duplicate[] = {0, 0};
    int before_creations = creations, before_destructions = destructions;
    assert(!fxm_transfer_render_state(next, active, duplicate, 2));
    assert(fxm_transfer_render_state(next, active, identity, 2));
    assert(creations == before_creations && destructions == before_destructions);
    fxm_destroy(active);
    assert(render(next, 0) == 3.0f && render(next, 1) == 2.0f);
    assert(fxm_insert_track(control, 0));
    EffectsManager* shifted = fxm_clone_for_render(control);
    int insertion[] = {-1, 0, 1};
    assert(shifted && fxm_transfer_render_state(shifted, next, insertion, 3));
    fxm_destroy(next);
    assert(render(shifted, 0) == 0.0f && render(shifted, 1) == 4.0f && render(shifted, 2) == 3.0f);
    assert(fxm_remove_track(control, 0));
    next = fxm_clone_for_render(control);
    int removal[] = {1, 2};
    assert(next && fxm_transfer_render_state(next, shifted, removal, 2));
    fxm_destroy(shifted);
    assert(render(control, 0) == 1.0f); // Render history never leaked into the control handles.
    assert(fxm_track_remove(control, 1, second));
    assert(render(next, 1) == 4.0f); // A live revision survives deletion of the editable effect.
    int retained = live_handles;
    fail_after = 0;
    assert(!fxm_clone_for_render(control) && live_handles == retained);
    fail_after = -1;
    EffectsManager* replacement = fxm_clone_for_render(control);
    assert(replacement && fxm_transfer_render_state(replacement, next, identity, 2));
    fxm_destroy(next);
    assert(render(replacement, 0) == 5.0f && render(replacement, 1) == 0.0f);
    assert(fxm_track_set_param(control, 0, first, 0, 5.0f));
    EffectsManager* changed = fxm_clone_for_render(control);
    assert(changed && fxm_transfer_render_state(changed, replacement, identity, 2));
    assert(render(changed, 0) == 10.0f); // The new target applies without losing the previous five samples of history.
    fxm_destroy(changed);
    fxm_destroy(replacement);
    fxm_destroy(control);
    assert(live_handles == 0 && creations == destructions);
    puts("effects_revision_test: success");
    return 0;
}
