#include "ui/sound_panel.h"
#include "ui/midi_instrument_panel.h"
#include "ui/midi_editor.h"
#include "ui/daw_ui_button.h"
#include "ui/render_utils.h"
#include "ui/font.h"
#include "app_state.h"
#include <stdio.h>
#include <string.h>
// Computes a compact entry button or an inspection overlay with one shared hit-testing geometry.
void daw_sound_panel_layout(const AppState* state,DawSoundPanelLayout* out) {
    memset(out,0,sizeof(*out));MidiInstrumentPanelLayout base;midi_instrument_panel_compute_layout(state,&base);
    out->panel=base.scope_rect;
    if(!state->sound_ui.open){out->buttons[12]=(SDL_Rect){base.scope_rect.x+base.scope_rect.w-78,base.scope_rect.y+2,74,20};return;}
    if(out->panel.h<185){int bottom=out->panel.y+out->panel.h;out->panel.y=base.param_grid_rect.y;out->panel.h=bottom-out->panel.y;}
    if(out->panel.h<130){int bottom=base.panel_rect.y+base.panel_rect.h-6;out->panel.y=base.header_rect.y+base.header_rect.h+4;out->panel.h=bottom-out->panel.y;}
    out->compact=out->panel.h<230;
    int columns=7,gap=4,w=(out->panel.w-8-(columns-1)*gap)/columns;if(w<0)w=0;
    for(int i=0;i<17;++i)out->buttons[i]=(SDL_Rect){out->panel.x+4+(i%columns)*(w+gap),out->panel.y+4+(i/columns)*23,w-2,20};
    int top=out->compact?121:158;
    out->plot=(SDL_Rect){out->panel.x+6,out->panel.y+top,out->panel.w-12,out->panel.h-top-6};
    if(out->plot.h<0)out->plot.h=0;
}
// Draws the same rounded product button tokens as existing instrument controls.
static void button(SDL_Renderer* r,SDL_Rect rect,const char* name,const DawThemePalette* theme,bool active) {
    if(rect.w<=0 || rect.h<=0)return;DawUiButtonSpec spec;daw_ui_button_spec_init(&spec,name);spec.state.selected=active;
    DawUiButtonStyle style;daw_ui_button_style_resolve(theme,&spec,&style);daw_ui_button_draw_frame(r,&rect,&style);
    ui_draw_text_clipped(r,rect.x+5,rect.y+3,name,theme->text_primary,.75f,rect.w-10);
}
// Plots fixed-scale source samples, envelope RMS or calibrated tonal dB without visual normalization.
static void plot(SDL_Renderer* r,SDL_Rect area,const DawSoundAnalysis* data,int view,SDL_Color color) {
    int count=view==2?DAW_SOUND_BINS:DAW_SOUND_POINTS;int px=0,py=0;
    SDL_SetRenderDrawColor(r,color.r,color.g,color.b,255);
    for(int i=0;i<count;++i){float v=view==0?data->waveform[i]:view==1?data->envelope_rms[i]:data->spectrum_db[i];
        float t=view==0?(v+1)*.5f:view==1?v:(v+100)/106;
        t=fmaxf(0,fminf(1,t));int x=area.x+i*(area.w-1)/(count-1),y=area.y+area.h-1-(int)(t*(area.h-1));
        if(i)SDL_RenderDrawLine(r,px,py,x,y);px=x;py=y;
    }
}
// Shows the exact cached sound and reference while flagging edits that need explicit refresh.
void daw_sound_panel_render(SDL_Renderer* r,AppState* state) {
    DawSoundPanelLayout l;daw_sound_panel_layout(state,&l);DawSoundUI* ui=&state->sound_ui;DawThemePalette theme;daw_shared_theme_resolve_palette(&theme);
    if(!ui->open){button(r,l.buttons[12],"Inspect",&theme,false);return;}
    SDL_Rect previous;SDL_RenderGetClipRect(r,&previous);bool clipped=SDL_RenderIsClipEnabled(r);ui_set_content_clip_rect(r,&l.panel);
    SDL_SetRenderDrawColor(r,theme.inspector_fill.r,theme.inspector_fill.g,theme.inspector_fill.b,255);SDL_RenderFillRect(r,&l.panel);
    if(ui->listening>=14 && !engine_preview_active(state->engine)){ui->listening=-1;SDL_strlcpy(ui->message,"Capture playback finished.",sizeof(ui->message));}
    const char* labels[]={"Waveform","RMS Envelope","Spectrum","Attack","Sustain","Release",ui->chord?"Chord":"C4","Refresh","Capture Ref","Play Current","Play Ref","Clear Ref","Close",ui->composition?"Region":"Instrument","Listen Capture","Listen Ref","Stop Capture"};
    if(ui->composition){labels[6]=ui->dry?"Dry":"Processed";labels[9]="Save WAV";labels[10]="Save Ref WAV";}
    for(int i=0;i<17;++i)button(r,l.buttons[i],labels[i],&theme,i==ui->listening || i==ui->view || (i>=3 && i<=5 && i-3==(int)ui->window));
    if(!l.compact)ui_draw_text_clipped(r,l.panel.x+5,l.panel.y+76,ui->message,theme.text_muted,.7f,l.panel.w-10);
    bool stale=false;
    MidiEditorSelection selected; DawActionTarget selected_target;
    bool selected_valid=midi_editor_get_selection(state,&selected) && daw_instrument_target_capture(state,selected.track_index,selected.clip_index,&selected_target)==DAW_ACTION_UNCHANGED;
    if(ui->has_current && !ui->composition){DawActionResult now=daw_action_get_instrument(state,ui->current_target);DawUserPreset old={.base=ui->current.sound.base,.params=ui->current.sound.params};
        stale=!selected_valid || selected_target.track_id!=ui->current_target.track_id || selected_target.clip_id!=ui->current_target.clip_id || selected_target.project_generation!=ui->current_target.project_generation || !now.has_snapshot || !daw_preset_sound_equal(&old,now.snapshot.effective_preset,now.snapshot.effective_params);
        snprintf(ui->metrics,sizeof(ui->metrics),"Current cyan: peak %.4f RMS %.4f | clipped %llu%s%s",ui->current.peak,ui->current.rms,
            (unsigned long long)ui->current.clipped_samples,ui->has_reference?" | reference purple":"",stale?" | settings changed: Refresh":"");
    }else SDL_strlcpy(ui->metrics,"Use Refresh to measure the selected sound.",sizeof(ui->metrics));
    if(l.compact && ui->has_current && !ui->composition)snprintf(ui->metrics,sizeof(ui->metrics),"Now: peak %.4f RMS %.4f, cutoff %.0f Hz attack %.0f ms drive %.2f | clipped %llu%s",ui->current.peak,ui->current.rms,ui->current.sound.params.cutoff_hz,ui->current.sound.params.attack_ms,ui->current.sound.params.drive,(unsigned long long)ui->current.clipped_samples,stale?" | Refresh":"");
    if(ui->composition && ui->has_current)snprintf(ui->metrics,sizeof(ui->metrics),"%s capture: peak %.4f RMS %.4f | full-scale samples %llu | Refresh after edits",ui->captured.request.dry?"Dry":"Processed",ui->current.peak,ui->current.rms,(unsigned long long)ui->current.clipped_samples);
    ui_draw_text_clipped(r,l.panel.x+5,l.panel.y+(l.compact?75:96),ui->metrics,theme.text_primary,.75f,l.panel.w-10);
    snprintf(ui->settings,sizeof(ui->settings),"Current: cutoff %.0f Hz, attack %.0f ms, drive %.2f",ui->current.sound.params.cutoff_hz,ui->current.sound.params.attack_ms,ui->current.sound.params.drive);
    if(ui->has_current && !l.compact && !ui->composition)ui_draw_text_clipped(r,l.panel.x+5,l.panel.y+111,ui->settings,theme.text_primary,.7f,l.panel.w-10);
    if(ui->has_reference){snprintf(ui->reference_metrics,sizeof(ui->reference_metrics),"Ref: peak %.4f RMS %.4f | cutoff %.0f Hz, attack %.0f ms, drive %.2f",ui->reference.peak,ui->reference.rms,ui->reference.sound.params.cutoff_hz,ui->reference.sound.params.attack_ms,ui->reference.sound.params.drive);
        if(ui->composition)snprintf(ui->reference_metrics,sizeof(ui->reference_metrics),"Ref %s: peak %.4f RMS %.4f",ui->captured_reference.request.dry?"dry":"processed",ui->reference.peak,ui->reference.rms);
        ui_draw_text_clipped(r,l.panel.x+5,l.panel.y+(l.compact?90:126),ui->reference_metrics,(SDL_Color){187,134,245,255},.7f,l.panel.w-10);}
    const char* axis=ui->view==0?"20 ms, mono waveform, fixed -1..+1":ui->view==1?"3 seconds, stereo block RMS, fixed 0..1":"20 Hz..20 kHz log grid, -100..+6 dB tonal amplitude; 2048-frame Hann window";
    char composition_axis[160];
    if(ui->composition && ui->has_current){
        snprintf(composition_axis,sizeof(composition_axis),ui->view==0?"20 ms mono, fixed -1..+1 | window %.3f s":ui->view==1?"%.3f seconds including tail, RMS fixed 0..1":"20 Hz..%.0f Hz, -100..+6 dB tonal amplitude, 2048-frame Hann",
            ui->view==0?(double)ui->captured.request.window/ui->captured.audio.sample_rate:ui->view==1?(double)ui->captured.audio.frame_count/ui->captured.audio.sample_rate:ui->captured.audio.sample_rate*.5<20000?ui->captured.audio.sample_rate*.5:20000.0);
        axis=composition_axis;
    }
    ui_draw_text_clipped(r,l.panel.x+5,l.panel.y+(l.compact?82:118),l.compact?ui->message:axis,theme.text_muted,.7f,l.panel.w-10);
    if(l.plot.h>0 && l.plot.w>0){SDL_SetRenderDrawColor(r,theme.pane_border.r,theme.pane_border.g,theme.pane_border.b,255);SDL_RenderDrawRect(r,&l.plot);
        if(ui->has_reference)plot(r,l.plot,&ui->reference,ui->view,(SDL_Color){187,134,245,255});
        if(ui->has_current)plot(r,l.plot,&ui->current,ui->view,(SDL_Color){90,210,230,255});}
    SDL_RenderSetClipRect(r,clipped?&previous:NULL);
}
