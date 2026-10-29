#define _XOPEN_SOURCE 700
#include "app_state.h"
#include "engine/engine_internal.h"
#include "engine/routing.h"
#include "input/effects_panel_input.h"
#include "input/effects_panel_input_helpers.h"
#include "ui/effects_panel.h"
#include "ui/layout.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
// Publishes actual prepared production dynamics instead of injecting invented meter values.
static void render(Engine* e) {
    engine_source_plan_apply(e);
    float out[256], scratch[256];
    for (int frame = 0; frame < 64000; frame += 128)
        engine_mix_tracks(e, frame, 128, out, scratch, 2);
}
// Checks identity-bound independent reads, freshness and exclusion of private offline work.
int main(void) {
    AppState* s = calloc(1, sizeof(*s));assert(s);
    config_set_defaults(&s->runtime_cfg);s->runtime_cfg.block_size = 128;
    s->runtime_cfg.default_fade_in_ms = s->runtime_cfg.default_fade_out_ms = 0;
    s->engine = engine_create(&s->runtime_cfg);assert(s->engine);undo_manager_init(&s->undo);
    input_manager_init(&s->input_manager);ui_init_panes(s);ui_layout_panes(s, 1600, 1000);
    char path[] = "/tmp/sonics-dynamics-feedback-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    float* pcm=malloc(64000*2*sizeof(float));assert(pcm);
    for (int n=0;n<64000;++n) {pcm[n*2]=1;pcm[n*2+1]=.01f;}
    EngineBounceBuffer input={pcm,64000,2,48000};assert(engine_bounce_write_wav(&input,path,ENGINE_BOUNCE_WAV_FLOAT32));
    assert(engine_add_clip_to_track(s->engine,0,path,0,NULL));free(pcm);
    uint64_t group,ret;assert(engine_append_group(s->engine,0,"Dynamics group",&group));assert(engine_append_return(s->engine,0,&ret));
    EngineRoutingSnapshot before,next;assert(engine_routing_capture(s->engine,&before));next=before;
    next.tracks[0].parent_id=group;next.tracks[1].sends[0]=(EngineSend){.return_id=ret,.gain=1};assert(engine_routing_set(s->engine,&before,&next));
    FxInstId ids[4];ids[0]=engine_fx_track_add(s->engine,0,20);ids[1]=engine_fx_track_add(s->engine,1,21);ids[2]=engine_fx_track_add(s->engine,2,20);ids[3]=engine_fx_master_add(s->engine,21);
    for(int n=0;n<4;++n)assert(ids[n]);
    assert(engine_fx_track_set_param(s->engine,0,ids[0],4,12));
    assert(engine_fx_track_set_param(s->engine,1,ids[1],0,-12));
    assert(engine_fx_track_set_param(s->engine,2,ids[2],0,-36));
    assert(engine_fx_master_set_param(s->engine,ids[3],0,-18));
    EngineGainReductionSnapshot v[4];render(s->engine);
    for(int n=0;n<4;++n) {
        assert(engine_get_gain_reduction(s->engine,n==3,n,ids[n],&v[n]));
        assert(v[n].available&&v[n].enabled&&v[n].fresh&&v[n].gain_db<-.1f&&v[n].sequence>0);
        EngineGainReductionSnapshot repeated;assert(engine_get_gain_reduction(s->engine,n==3,n,ids[n],&repeated));assert(repeated.sequence==v[n].sequence&&repeated.gain_db==v[n].gain_db);
        float history[64];engine_get_fx_scope_samples(s->engine,n==3,n,ids[n],ENGINE_SCOPE_STREAM_GAIN_REDUCTION,history,64);
        assert(engine_get_gain_reduction(s->engine,n==3,n,ids[n],&repeated)&&repeated.sequence==v[n].sequence);
    }
    assert(fabsf(v[0].gain_db+13.5f)<.01f); // Excludes +12 dB makeup and independent quiet channel.
    EngineBounceBuffer bounced={0};assert(engine_bounce_range_to_buffer(s->engine,0,64000,NULL,NULL,&bounced));engine_bounce_buffer_free(&bounced);
    EngineGainReductionSnapshot repeated;assert(engine_get_gain_reduction(s->engine,true,-1,ids[3],&repeated)&&repeated.sequence==v[3].sequence);
    SDL_Delay(1050);assert(engine_get_gain_reduction(s->engine,true,-1,ids[3],&repeated)&&!repeated.fresh&&repeated.available);
    assert(engine_fx_track_set_enabled(s->engine,0,ids[0],false));assert(engine_get_gain_reduction(s->engine,false,0,ids[0],&repeated)&&!repeated.enabled&&!repeated.available);
    assert(!engine_get_gain_reduction(s->engine,false,1,ids[0],&repeated));
    // Exercise the real native parameter drag and release path, then exact shared history.
    s->selected_track_index = -1;effects_panel_init(s);effects_panel_sync_from_engine(s);
    float original=s->effects_panel.chain[0].param_values[0];
    assert(begin_fx_param_drag(s,0,0));s->effects_panel.dragging_slider=true;
    apply_slider_value(s,0,0,-9);
    SDL_Event release={.type=SDL_MOUSEBUTTONUP};release.button.button=SDL_BUTTON_LEFT;
    effects_panel_input_handle_event(&s->input_manager,s,&release);
    FxMasterSnapshot chain;assert(engine_fx_master_snapshot(s->engine,&chain));assert(chain.items[0].params[0]==-9&&s->undo.undo_count==1);
    assert(undo_manager_undo(&s->undo,s));assert(engine_fx_master_snapshot(s->engine,&chain)&&chain.items[0].params[0]==original);
    assert(undo_manager_redo(&s->undo,s));assert(engine_fx_master_snapshot(s->engine,&chain)&&chain.items[0].params[0]==-9);
    undo_manager_free(&s->undo);engine_destroy(s->engine);free(s);assert(unlink(path)==0);
    puts("dynamics_feedback_test: passed (real child/group/return/master DSP, makeup exclusion, non-consuming reads, full ring, stale/bypass/identity guards, offline isolation and native parameter history)");
}
