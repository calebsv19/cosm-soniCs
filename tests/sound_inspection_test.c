#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "daw/sound_inspection.h"
#include "ui/sound_panel.h"
#include "daw/composition.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/midi_instrument_panel.h"
#include "engine/engine_internal.h"
#include "engine/analysis_math.h"
#include "test_midi_editor_harness.h"
#include "test_assert.h"
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "audio/wav_writer.h"
#define CHECK(v,m) daw_test_expect("sound_inspection_test",(v),(m))
// Checks original-level calibration against independently synthesized tones, silence and clipping.
static void calibration(void) {
    float* samples=calloc(DAW_SOUND_FRAMES*2,sizeof(float));CHECK(samples!=NULL,"signal allocation");
    DawSoundSpec sound={ENGINE_INSTRUMENT_PRESET_PURE_SINE,engine_instrument_default_params(0),false};DawSoundAnalysis a;
    CHECK(daw_sound_measure(sound,DAW_SOUND_SUSTAIN,samples,&a) && a.peak==0 && a.rms==0 && a.dc==0,"silence metrics");
    for(int i=0;i<DAW_SOUND_BINS;++i)CHECK(a.spectrum_db[i]==-160,"silence finite spectrum floor");
    const int bin=55;double hz=engine_analysis_frequency(bin,DAW_SOUND_BINS,DAW_SOUND_RATE,20,20000);
    double sum=0,power=0,peak=0;uint64_t clipped=0;
    for(int i=0;i<DAW_SOUND_FRAMES;++i){float value=(float)(.5*sin(6.283185307179586*hz*i/DAW_SOUND_RATE));samples[i*2]=samples[i*2+1]=value;
        sum+=2*value;power+=2*(double)value*value;if(fabs(value)>peak)peak=fabs(value);}
    CHECK(daw_sound_measure(sound,DAW_SOUND_SUSTAIN,samples,&a),"tone measured");
    CHECK(a.peak==peak && fabs(a.rms-sqrt(power/(DAW_SOUND_FRAMES*2)))<1e-12 && fabs(a.dc-sum/(DAW_SOUND_FRAMES*2))<1e-12,"independent stereo metrics");
    CHECK(fabs(a.spectrum_db[bin]-20*log10(.5))<.01,"Hann coherent-gain tone calibration");
    int strongest=0;for(int i=1;i<DAW_SOUND_BINS;++i)if(a.spectrum_db[i]>a.spectrum_db[strongest])strongest=i;
    CHECK(strongest==bin,"known frequency grid location");
    for(int i=0;i<DAW_SOUND_POINTS;++i)CHECK(a.waveform[i]==samples[(48000+(uint64_t)i*959/(DAW_SOUND_POINTS-1))*2],"window sample identity");
    for(int i=0;i<DAW_SOUND_FRAMES*2;++i){samples[i]*=3;if(fabs(samples[i])>=1)++clipped;}
    CHECK(daw_sound_measure(sound,DAW_SOUND_ATTACK,samples,&a) && a.peak>1 && a.clipped_samples==clipped,"clipping reported without normalization");
    samples[17]=NAN;CHECK(!daw_sound_measure(sound,DAW_SOUND_ATTACK,samples,&a),"nonfinite rejected");free(samples);
}
// Requires exact parity with a separately configured production source and fixed gate/release behavior.
static void production_parity(void) {
    DawSoundSpec sound={ENGINE_INSTRUMENT_PRESET_FILTERED_PAD,engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_FILTERED_PAD),false};
    DawSoundRender render;CHECK(daw_sound_render(sound,&render),"shared render");
    EngineInstrumentSource* source=engine_instrument_source_create();EngineMidiNote note={0,DAW_SOUND_GATE,60,.5f};
    CHECK(engine_instrument_source_set_midi_clip(source,0,DAW_SOUND_FRAMES,sound.base,sound.params,&note,1,NULL,0,NULL,0),"independent source");
    engine_instrument_source_reset(source,DAW_SOUND_RATE,2);
    for(int frame=0;frame<DAW_SOUND_FRAMES;frame+=256){int count=DAW_SOUND_FRAMES-frame<256?DAW_SOUND_FRAMES-frame:256;float data[512]={0};
        engine_instrument_source_render(source,data,count,frame);CHECK(!memcmp(data,render.samples+frame*2,count*2*sizeof(float)),"all source samples exact");}
    engine_instrument_source_destroy(source);DawSoundAnalysis attack,sustain,release;
    CHECK(daw_sound_analyze(sound,DAW_SOUND_ATTACK,&attack) && daw_sound_analyze(sound,DAW_SOUND_SUSTAIN,&sustain) && daw_sound_analyze(sound,DAW_SOUND_RELEASE,&release),"three windows");
    CHECK(attack.window_start==0 && sustain.window_start==48000 && release.window_start==DAW_SOUND_GATE,"explicit time windows");
    CHECK(attack.peak==render.peak && sustain.rms==render.rms && release.dc==render.dc,"original metrics independent of window");
    CHECK(memcmp(attack.waveform,sustain.waveform,sizeof(attack.waveform)),"attack and sustain distinction");
    CHECK(release.envelope_rms[220]==0,"finite release tail ends");
    CHECK(!daw_sound_analyze(sound,(DawSoundWindow)3,&attack),"invalid window");daw_sound_render_free(&render);
}
// Exercises explicit UI refresh/reference actions and verifies audition cannot modify sound or history.
static void comparison(void) {
    AppState* state=calloc(1,sizeof(*state));EngineRuntimeConfig config;daw_midi_editor_test_state_init("sound_inspection_test",state,&config);
    CHECK(engine_add_midi_clip_to_track(state->engine,0,0,DAW_SOUND_FRAMES,NULL),"region");
    state->selected_track_index=state->active_track_index=state->selected_clip_index=0;state->midi_editor_ui.panel_mode=MIDI_REGION_PANEL_INSTRUMENT;
    DawActionTarget target;CHECK(daw_instrument_target_capture(state,0,0,&target)==DAW_ACTION_UNCHANGED,"owned target");
    DawInstrumentRequest request={.target=target,.set_preset=true,.preset=ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS};CHECK(daw_action_set_instrument(state,&request).status==DAW_ACTION_APPLIED,"initial sound");
    DawSoundPanelLayout layout;daw_sound_panel_layout(state,&layout);SDL_Rect rect=layout.buttons[12];CHECK(daw_sound_panel_activate(state,rect.x+1,rect.y+1) && state->sound_ui.has_current,"open inspection");
    daw_sound_panel_layout(state,&layout);rect=layout.buttons[8];CHECK(daw_sound_panel_activate(state,rect.x+1,rect.y+1) && state->sound_ui.has_reference,"capture reference");
    DawSoundAnalysis reference=state->sound_ui.reference;
    request.set_preset=false;request.parameter_mask=1u<<ENGINE_INSTRUMENT_PARAM_CUTOFF_HZ;request.params.cutoff_hz=6500;
    CHECK(daw_action_set_instrument(state,&request).status==DAW_ACTION_APPLIED,"edited sound");
    rect=layout.buttons[7];CHECK(daw_sound_panel_activate(state,rect.x+1,rect.y+1),"refresh comparison");
    CHECK(!memcmp(&reference,&state->sound_ui.reference,sizeof(reference)) && memcmp(reference.spectrum_db,state->sound_ui.current.spectrum_db,sizeof(reference.spectrum_db)),"immutable reference differs from current");
    DawSoundSpec invalid=reference.sound;invalid.params.cutoff_hz=NAN;
    CHECK(daw_action_audition_sound(state,target,invalid).status==DAW_ACTION_INVALID,"invalid captured audition rejected");
    SDL_Event view={.type=SDL_KEYDOWN};view.key.keysym.sym=SDLK_3;
    CHECK(daw_sound_panel_event(state,&view) && state->sound_ui.view==2,"inspection keyboard view");
    state->midi_editor_ui.qwerty_record_armed=true;view.key.keysym.sym=SDLK_1;
    CHECK(!daw_sound_panel_event(state,&view) && state->sound_ui.view==2,"armed MIDI recording retains key ownership");state->midi_editor_ui.qwerty_record_armed=false;
    for(int size=0;size<2;++size){ui_layout_panes(state,size?1200:1440,size?800:900);daw_sound_panel_layout(state,&layout);
        CHECK(layout.plot.h>0 && layout.plot.w>0,"compact inspection retains plot");
        for(int i=0;i<13;++i)CHECK(layout.buttons[i].x>=layout.panel.x && layout.buttons[i].y>=layout.panel.y && layout.buttons[i].x+layout.buttons[i].w<=layout.panel.x+layout.panel.w && layout.buttons[i].y+layout.buttons[i].h<=layout.panel.y+layout.panel.h,"inspection button containment");}
    int history=state->undo.undo_count;DawInstrumentSnapshot current=daw_action_get_instrument(state,target).snapshot;
    rect=layout.buttons[10];CHECK(daw_sound_panel_activate(state,rect.x+1,rect.y+1),"play reference");
    CHECK(state->engine->midi_audition_params.cutoff_hz==reference.sound.params.cutoff_hz && state->undo.undo_count==history,"reference audition original settings without history");
    CHECK(daw_action_get_instrument(state,target).snapshot.effective_params.cutoff_hz==current.effective_params.cutoff_hz,"reference audition preserves project sound");
    rect=layout.buttons[9];CHECK(daw_sound_panel_activate(state,rect.x+1,rect.y+1) && state->engine->midi_audition_params.cutoff_hz==6500,"play current");
    CHECK(state->engine->midi_audition_notes.notes[0].duration_frames==DAW_SOUND_GATE,"comparison identical finite gate");
    state->bounce_active=true;rect=layout.buttons[8];CHECK(daw_sound_panel_activate(state,rect.x+1,rect.y+1) && !memcmp(&reference,&state->sound_ui.reference,sizeof(reference)),"busy capture preserves reference");
    state->bounce_active=false;SDL_Event escape={.type=SDL_KEYDOWN};escape.key.keysym.sym=SDLK_ESCAPE;CHECK(daw_sound_panel_event(state,&escape) && !state->sound_ui.open,"close inspection");
    daw_midi_editor_test_state_destroy(state);free(state);
}
// Checks actual processed-region inspector captures, immutable reference audio and close-time ownership.
static void composition_ui(void) {
    AppState* state=calloc(1,sizeof(*state));EngineRuntimeConfig config;daw_midi_editor_test_state_init("sound_inspection_test",state,&config);
    CHECK(engine_add_midi_clip_to_track(state->engine,0,0,48000,NULL),"composition region");
    CHECK(engine_clip_midi_add_note(state->engine,0,0,(EngineMidiNote){0,24000,60,.5f},NULL),"composition note");
    state->selected_track_index=state->active_track_index=state->selected_clip_index=0;state->midi_editor_ui.panel_mode=MIDI_REGION_PANEL_INSTRUMENT;
    DawSoundPanelLayout layout;daw_sound_panel_layout(state,&layout);SDL_Rect r=layout.buttons[12];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1),"open");
    daw_sound_panel_layout(state,&layout);r=layout.buttons[13];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1) && state->sound_ui.composition && state->sound_ui.captured.audio.data,"actual region mode");
    CHECK(state->sound_ui.current.rms>0 && state->sound_ui.captured.audio.frame_count==96000,"actual notes and explicit tail");
    r=layout.buttons[8];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1) && state->sound_ui.captured_reference.audio.data,"capture immutable processed reference");
    double original=state->sound_ui.reference.rms;float* previous=state->sound_ui.captured_reference.audio.data;
    DawActionTarget target;CHECK(daw_instrument_target_capture(state,0,0,&target)==DAW_ACTION_UNCHANGED,"target");DawFxSnapshot before,after;
    CHECK(daw_action_get_fx(state,target.project_generation,target.track_id,&before)==DAW_ACTION_UNCHANGED,"chain");
    EngineFxChainRequest effect={.op=ENGINE_FX_CHAIN_ADD,.type=1};CHECK(daw_action_edit_fx(state,&before,&effect,&after)==DAW_ACTION_APPLIED,"gain");before=after;
    effect=(EngineFxChainRequest){.op=ENGINE_FX_CHAIN_PARAM,.id=before.chain.items[0].id,.parameter=0,.value=-6};CHECK(daw_action_edit_fx(state,&before,&effect,&after)==DAW_ACTION_APPLIED,"gain setting");
    int history=state->undo.undo_count;r=layout.buttons[7];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1),"processed refresh");
    CHECK(state->sound_ui.current.rms/original>.49 && state->sound_ui.current.rms/original<.52,"processed inspector actual gain");
    CHECK(state->sound_ui.captured_reference.audio.data==previous && state->sound_ui.reference.rms==original,"reference survives authored changes");
    r=layout.buttons[6];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1) && state->sound_ui.dry && fabs(state->sound_ui.current.rms-original)<1e-8,"dry comparison retains mixer");
    CHECK(state->undo.undo_count==history,"inspection read-only history");
    state->bounce_active=true;r=layout.buttons[8];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1) && state->sound_ui.captured_reference.audio.data==previous,"busy capture preserves reference");state->bounce_active=false;
    char library[]="/tmp/daw-composition-ui-XXXXXX";CHECK(mkdtemp(library)!=NULL && !setenv("DAW_PRESET_LIBRARY",library,1),"private WAV library");
    r=layout.buttons[9];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1) && state->sound_ui.published_path[0],"save captured current WAV");
    char expected[1024];snprintf(expected,sizeof(expected),"%s/expected.wav",library);const EngineBounceBuffer* audio=&state->sound_ui.captured.audio;
    CHECK(wav_write_pcm16_dithered_result(expected,audio->data,audio->frame_count,audio->channels,audio->sample_rate,UINT32_C(0x534f4e49))==DAW_SAVE_SYNCED,"expected original-level PCM");
    FILE* actual=fopen(state->sound_ui.published_path,"rb"),*comparison=fopen(expected,"rb");CHECK(actual && comparison,"saved files");int a,b;do{a=fgetc(actual);b=fgetc(comparison);CHECK(a==b,"native save exact captured PCM");}while(a!=EOF);fclose(actual);fclose(comparison);
    CHECK(!unlink(state->sound_ui.published_path) && !unlink(expected),"retire owned WAVs");char previews[1024];snprintf(previews,sizeof(previews),"%s/CompositionPreviews",library);CHECK(!rmdir(previews) && !rmdir(library),"retire private dirs");unsetenv("DAW_PRESET_LIBRARY");
    CHECK(!setenv("SDL_AUDIODRIVER","dummy",1),"private dummy output");
    AudioDeviceSpec spec={config.sample_rate,config.block_size,state->sound_ui.captured.audio.channels};
    CHECK(audio_device_open(&state->engine->device,&spec,engine_audio_callback,state->engine),"paused dummy endpoint");
    r=layout.buttons[14]; SDL_Event listen={.type=SDL_MOUSEBUTTONDOWN};listen.button.button=SDL_BUTTON_LEFT;listen.button.x=r.x+1;listen.button.y=r.y+1;
    CHECK(daw_sound_panel_event(state,&listen) && !engine_preview_active(state->engine),"listen waits for matching release");
    listen.type=SDL_MOUSEBUTTONUP;CHECK(daw_sound_panel_event(state,&listen) && engine_preview_active(state->engine),"listen captured audio");
    CHECK(state->engine->device.preview_pcm!=state->sound_ui.captured.audio.data && !memcmp(state->engine->device.preview_pcm,state->sound_ui.captured.audio.data,16*sizeof(float)),"playback owns exact independent samples");
    r=layout.buttons[15];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1) && !memcmp(state->engine->device.preview_pcm,state->sound_ui.captured_reference.audio.data,16*sizeof(float)),"reference listening exact samples");
    CHECK(engine_transport_play(state->engine) && !engine_preview_active(state->engine),"transport cancels listening");CHECK(engine_transport_stop(state->engine),"stop intent");
    r=layout.buttons[14];CHECK(daw_sound_panel_activate(state,r.x+1,r.y+1) && engine_preview_active(state->engine),"replay capture");
    SDL_Event close={.type=SDL_KEYDOWN};close.key.keysym.sym=SDLK_ESCAPE;CHECK(daw_sound_panel_event(state,&close) && !state->sound_ui.captured.audio.data && !state->sound_ui.captured_reference.audio.data && !state->engine->device.preview_pcm,"close releases capture and playback ownership");
    daw_midi_editor_test_state_destroy(state);free(state);unsetenv("SDL_AUDIODRIVER");
}
// Runs calibrated private-signal and common UI/engine owner tests without personal data or audio devices.
int main(void){CHECK(!SDL_Init(SDL_INIT_EVENTS) && !TTF_Init(),"test runtime");calibration();production_parity();comparison();composition_ui();ui_font_shutdown();TTF_Quit();SDL_Quit();puts("sound_inspection_test: success (calibration, source parity, windows, clipping, immutable reference and finite auditions)");return 0;}
