#include "ui/sound_panel.h"
#include "ui/midi_instrument_panel.h"
#include "ui/midi_editor.h"
#include "app_state.h"
#include <string.h>
#include "daw/midi_actions.h"
#include <stdio.h>
#include <stdlib.h>
// Resolves the currently selected MIDI region into the common owned target contract.
static bool target(AppState* state,DawActionTarget* out) {
    MidiEditorSelection selection;return midi_editor_get_selection(state,&selection) &&
        daw_instrument_target_capture(state,selection.track_index,selection.clip_index,out)==DAW_ACTION_UNCHANGED;
}
// Copies calibrated composition plots into the existing inspector plot representation.
static void composition_analysis(const DawCompositionRender* capture, DawSoundAnalysis* out) {
    *out = (DawSoundAnalysis){.peak=capture->analysis.peak,.rms=capture->analysis.rms,.dc=capture->analysis.dc,.clipped_samples=capture->analysis.clipped_samples};
    memcpy(out->waveform,capture->analysis.waveform,sizeof(out->waveform));
    memcpy(out->envelope_rms,capture->analysis.envelope_rms,sizeof(out->envelope_rms));
    memcpy(out->frequency_hz,capture->analysis.frequency_hz,sizeof(out->frequency_hz));
    memcpy(out->spectrum_db,capture->analysis.spectrum_db,sizeof(out->spectrum_db));
}
// Releases only inspector-owned samples; isolated instrument measurements remain value-owned.
void daw_sound_panel_release(AppState* state) {
    if(!state)return;
    bool owned=state->sound_ui.listening>=14;
    state->sound_ui.pressed=state->sound_ui.listening=-1;
    uint64_t position;bool comparison=state->ab && engine_comparison_status(state->engine,state->ab->revision,&position);
    if(owned && !comparison)engine_preview_stop(state->engine);
    daw_composition_render_free(&state->sound_ui.captured);
    daw_composition_render_free(&state->sound_ui.captured_reference);
    if(state->sound_ui.composition)state->sound_ui.has_current=state->sound_ui.has_reference=false;
}
// Captures actual region audio with bounded material/tail and the explicitly selected processing mode.
static bool refresh_composition(AppState* state,DawActionTarget t) {
    DawSoundUI* ui=&state->sound_ui;DawMidiSnapshot region={0};
    if(daw_action_get_midi(state,t,&region).status!=DAW_MIDI_UNCHANGED){SDL_strlcpy(ui->message,"Region unavailable; select again.",sizeof(ui->message));return false;}
    const EngineRuntimeConfig* config=engine_get_config(state->engine);uint64_t rate=config->sample_rate;
    uint64_t duration=region.duration_frames,maximum=rate*DAW_COMPOSITION_SECONDS;
    if(duration>maximum)duration=maximum;
    DawCompositionRequest request={.has_target=true,.target=t,.dry=ui->dry,.start=region.start_frame,.end=region.start_frame+duration,.tail=rate};
    request.window=ui->window==DAW_SOUND_ATTACK?0:ui->window==DAW_SOUND_SUSTAIN?duration/2:duration;
    daw_midi_snapshot_free(&region);DawCompositionRender candidate={0};
    DawActionStatus result=daw_action_render_composition(state,&request,&candidate);
    if(result!=DAW_ACTION_UNCHANGED){daw_composition_render_free(&candidate);SDL_strlcpy(ui->message,"Stop playback/recording and finish edits; then Refresh.",sizeof(ui->message));return false;}
    daw_composition_render_free(&ui->captured);ui->captured=candidate;composition_analysis(&candidate,&ui->current);ui->has_current=true;ui->current_target=t;
    SDL_strlcpy(ui->message,ui->dry?"Dry region captured; mixer levels retained.":"Processed region captured through track and master effects.",sizeof(ui->message));return true;
}
// Publishes the exact retained current/reference audio without implicitly refreshing it.
static void save_composition(DawSoundUI* ui,bool reference) {
    const DawCompositionRender* capture=reference?&ui->captured_reference:&ui->captured;
    if(!capture->audio.data){SDL_strlcpy(ui->message,"Capture audio first.",sizeof(ui->message));return;}
    static uint64_t sequence;char artifact[96];DawCompositionPreview result;
    snprintf(artifact,sizeof(artifact),"%016llx-%016llx-%016llx",(unsigned long long)capture->generation,(unsigned long long)SDL_GetPerformanceCounter(),(unsigned long long)++sequence);
    DawCompositionPublishStatus status=daw_composition_publish(capture,artifact,&result);
    if(status==DAW_COMPOSITION_PUBLISH_OK){SDL_strlcpy(ui->published_path,result.path,sizeof(ui->published_path));SDL_Log("Composition preview: %s",result.path);SDL_strlcpy(ui->message,result.durable?"WAV saved in User Library / CompositionPreviews.":"WAV published; directory sync uncertain.",sizeof(ui->message));}
    else SDL_strlcpy(ui->message,status==DAW_COMPOSITION_PUBLISH_CLIPPING?"Audio exceeds full scale; lower levels and recapture.":"WAV not saved; check private library storage.",sizeof(ui->message));
}
// Explicitly refreshes private analysis and keeps rejected results separate from the last valid capture.
static bool refresh(AppState* state) {
    DawSoundUI* ui=&state->sound_ui;DawActionTarget t;
    if (!target(state,&t)) {ui->has_current=false;SDL_strlcpy(ui->message,"Select a MIDI region to inspect.",sizeof(ui->message));return false;}
    if(ui->composition)return refresh_composition(state,t);
    DawSoundAnalysis candidate;DawActionResult result=daw_action_analyze_instrument(state,t,ui->chord,ui->window,&candidate);
    SDL_strlcpy(ui->message,result.message,sizeof(ui->message));
    if (result.status<=DAW_ACTION_UNCHANGED) {ui->current=candidate;ui->current_target=t;ui->has_current=true;return true;}
    return false;
}
// Runs every inspection control through explicit common render/analysis/audition actions.
bool daw_sound_panel_activate(AppState* state,int x,int y) {
    if (!state || !midi_instrument_panel_should_render(state)) return false;
    DawSoundPanelLayout layout;daw_sound_panel_layout(state,&layout);SDL_Point p={x,y};DawSoundUI* ui=&state->sound_ui;
    if (!ui->open) {if (!SDL_PointInRect(&p,&layout.buttons[12])) return false;ui->open=true;ui->pressed=ui->listening=-1;if(!ui->has_current)ui->window=DAW_SOUND_SUSTAIN;refresh(state);return true;}
    for (int i=0;i<17;++i) if (SDL_PointInRect(&p,&layout.buttons[i]) && SDL_HasIntersection(&layout.buttons[i],&layout.panel) &&
        layout.buttons[i].y>=layout.panel.y && layout.buttons[i].y+layout.buttons[i].h<=layout.panel.y+layout.panel.h) {
        if((i>=3 && i<=8) || i==11 || i==13){engine_preview_stop(state->engine);ui->listening=-1;}
        if(i==13){daw_sound_panel_release(state);ui->composition=!ui->composition;ui->has_current=ui->has_reference=false;refresh(state);}
        else if (i<3) ui->view=i;
        else if (i<6) {ui->window=(DawSoundWindow)(i-3);refresh(state);
            if (!ui->composition && ui->has_reference) {DawSoundAnalysis next;if(daw_sound_analyze(ui->reference.sound,ui->window,&next))ui->reference=next;}}
        else if(i==6) {if(ui->composition)ui->dry=!ui->dry;else ui->chord=!ui->chord;refresh(state);
            if(!ui->composition && ui->has_reference){DawSoundSpec sound=ui->reference.sound;sound.chord=ui->chord;DawSoundAnalysis next;if(daw_sound_analyze(sound,ui->window,&next))ui->reference=next;}}
        else if(i==14 || i==15) {
            const DawCompositionRender* capture=i==14?&ui->captured:&ui->captured_reference;
            if(!ui->composition || !capture->audio.data) SDL_strlcpy(ui->message,"Capture region audio first.",sizeof(ui->message));
            else if(state->bounce_active || state->audio_recording.status==DAW_AUDIO_RECORDING_ACTIVE || state->midi_editor_ui.qwerty_test_enabled || state->midi_editor_ui.qwerty_record_armed)
                SDL_strlcpy(ui->message,"Stop Test/recording/bounce before listening.",sizeof(ui->message));
            else {
                bool playing=engine_preview_play(state->engine,capture->audio.data,capture->audio.frame_count,capture->audio.channels,capture->audio.sample_rate);
                if(playing)ui->listening=i;
                SDL_strlcpy(ui->message,playing?"Playing exact retained capture at its original level.":"Stop transport; check audio output and full-scale levels.",sizeof(ui->message));
            }
        }
        else if(i==16) {ui->listening=-1;engine_preview_stop(state->engine);SDL_strlcpy(ui->message,"Capture playback stopped.",sizeof(ui->message));}
        else if(i==7) refresh(state);
        else if(i==8) {if(refresh(state)){
            if(ui->composition){
                DawCompositionRender copy={0};copy=ui->captured;size_t bytes=copy.audio.frame_count*(size_t)copy.audio.channels*sizeof(float);copy.audio.data=malloc(bytes);
                if(!copy.audio.data){SDL_strlcpy(ui->message,"Reference allocation failed; previous reference retained.",sizeof(ui->message));return true;}
                memcpy(copy.audio.data,ui->captured.audio.data,bytes);daw_composition_render_free(&ui->captured_reference);ui->captured_reference=copy;
            }
            ui->reference=ui->current;ui->has_reference=true;SDL_strlcpy(ui->message,"Reference captured. Adjust controls, then refresh to compare.",sizeof(ui->message));}}
        else if((i==9 || i==10) && ui->composition)save_composition(ui,i==10);
        else if(i==9 || i==10) {
            DawActionTarget t;if(!target(state,&t))return true;
            bool fresh = i!=9 || refresh(state);
            bool available=i==9?(fresh && ui->has_current):ui->has_reference;
            if(available){DawSoundSpec sound=i==9?ui->current.sound:ui->reference.sound;sound.chord=ui->chord;
                DawActionResult result=daw_action_audition_sound(state,t,sound);SDL_strlcpy(ui->message,result.message,sizeof(ui->message));}
            else SDL_strlcpy(ui->message,"Capture a reference first.",sizeof(ui->message));
        } else if(i==11) {daw_composition_render_free(&ui->captured_reference);ui->has_reference=false;SDL_strlcpy(ui->message,"Reference cleared.",sizeof(ui->message));}
        else {ui->open=false;daw_sound_panel_release(state);}
        return true;
    }
    return SDL_PointInRect(&p,&layout.panel);
}
// Keeps inspection overlays from activating hidden knobs while retaining global transport/window ownership.
bool daw_sound_panel_event(AppState* state,const SDL_Event* event) {
    if (!state || !event) return false;
    if(!midi_instrument_panel_should_render(state)){state->sound_ui.open=false;daw_sound_panel_release(state);return false;}
    if(state->preset_ui.open || state->preset_ui.save_open)return false;
    if(!state->sound_ui.open)return false;
    if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE){state->sound_ui.open=false;daw_sound_panel_release(state);return true;}
    if(event->type==SDL_KEYDOWN && !event->key.repeat && !state->midi_editor_ui.qwerty_test_enabled && !state->midi_editor_ui.qwerty_record_armed && !(event->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))){
        int index=-1;switch(event->key.keysym.sym){case SDLK_1:index=0;break;case SDLK_2:index=1;break;case SDLK_3:index=2;break;case SDLK_a:index=3;break;case SDLK_s:index=4;break;case SDLK_l:index=5;break;case SDLK_c:index=6;break;case SDLK_r:index=7;break;case SDLK_b:index=8;break;case SDLK_p:index=9;break;case SDLK_o:index=10;break;case SDLK_m:index=13;break;case SDLK_DELETE:index=11;break;default:break;}
        if(index>=0){DawSoundPanelLayout layout;daw_sound_panel_layout(state,&layout);return daw_sound_panel_activate(state,layout.buttons[index].x+1,layout.buttons[index].y+1);}
    }
    if(event->type==SDL_WINDOWEVENT){state->sound_ui.pressed=-1;if(event->window.event==SDL_WINDOWEVENT_FOCUS_LOST || event->window.event==SDL_WINDOWEVENT_HIDDEN){state->sound_ui.open=false;daw_sound_panel_release(state);}return false;}
    if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT){
        DawSoundPanelLayout layout;daw_sound_panel_layout(state,&layout);SDL_Point point={event->button.x,event->button.y};state->sound_ui.pressed=-1;
        for(int i=0;i<17;++i)if(SDL_PointInRect(&point,&layout.buttons[i]))state->sound_ui.pressed=i;
        return SDL_PointInRect(&point,&layout.panel);
    }
    if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT){
        int pressed=state->sound_ui.pressed;state->sound_ui.pressed=-1;
        DawSoundPanelLayout layout;daw_sound_panel_layout(state,&layout);SDL_Point point={event->button.x,event->button.y};
        if(pressed>=0 && SDL_PointInRect(&point,&layout.buttons[pressed]))return daw_sound_panel_activate(state,point.x,point.y);
        return SDL_PointInRect(&point,&layout.panel);
    }
    return false;
}
