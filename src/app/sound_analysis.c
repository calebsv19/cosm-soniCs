#include "daw/sound_inspection.h"
#include "engine/analysis_math.h"
#include "app_state.h"
#include <string.h>
// Measures original stereo levels, a block RMS envelope and a selected onset-contiguous tonal spectrum.
bool daw_sound_measure(DawSoundSpec sound, DawSoundWindow window, const float* samples, DawSoundAnalysis* out) {
    if (!samples || !out || window<DAW_SOUND_ATTACK || window>DAW_SOUND_RELEASE) return false;
    memset(out,0,sizeof(*out)); out->sound=sound; out->window=window;
    out->window_start=window==DAW_SOUND_ATTACK?0:window==DAW_SOUND_SUSTAIN?48000:DAW_SOUND_GATE;
    double power=0,sum=0;
    for (int i=0;i<DAW_SOUND_FRAMES*2;++i) {
        double v=samples[i]; if (!isfinite(v)) return false;
        if (fabs(v)>out->peak) out->peak=fabs(v); if (fabs(v)>=1) ++out->clipped_samples;
        power+=v*v; sum+=v;
    }
    out->rms=sqrt(power/(DAW_SOUND_FRAMES*2));out->dc=sum/(DAW_SOUND_FRAMES*2);
    for (int point=0;point<DAW_SOUND_POINTS;++point) {
        uint64_t frame=out->window_start+(uint64_t)point*959/(DAW_SOUND_POINTS-1);
        out->waveform[point]=(samples[frame*2]+samples[frame*2+1])*.5f;
        int begin=point*DAW_SOUND_FRAMES/DAW_SOUND_POINTS,end=(point+1)*DAW_SOUND_FRAMES/DAW_SOUND_POINTS;
        double energy=0; for (int f=begin;f<end;++f) energy+=(double)samples[f*2]*samples[f*2]+(double)samples[f*2+1]*samples[f*2+1];
        out->envelope_rms[point]=(float)sqrt(energy/((end-begin)*2));
    }
    float mono[ENGINE_ANALYSIS_MAX_FRAMES];
    for (int i=0;i<ENGINE_ANALYSIS_MAX_FRAMES;++i) mono[i]=(samples[(out->window_start+i)*2]+samples[(out->window_start+i)*2+1])*.5f;
    EngineAnalysisPlan plan;
    if (!engine_analysis_prepare(&plan,ENGINE_ANALYSIS_MAX_FRAMES,DAW_SOUND_BINS,DAW_SOUND_RATE,20,20000)) return false;
    engine_analysis_compute(&plan,mono,out->spectrum_db);
    for (int b=0;b<DAW_SOUND_BINS;++b) out->frequency_hz[b]=engine_analysis_frequency(b,DAW_SOUND_BINS,DAW_SOUND_RATE,20,20000);
    return true;
}
// Reuses the same bounded float render as production WAV previews and releases it before returning.
bool daw_sound_analyze(DawSoundSpec sound, DawSoundWindow window, DawSoundAnalysis* out) {
    if (window<DAW_SOUND_ATTACK || window>DAW_SOUND_RELEASE) return false;
    DawSoundRender render; if (!daw_sound_render(sound,&render)) return false;
    bool ok=daw_sound_measure(sound,window,render.samples,out);daw_sound_render_free(&render);return ok;
}
// Refuses analysis during active recording/bounce without changing any authored or device state.
static bool busy(const AppState* state) {
    return state->bounce_active || state->undo.active_drag_valid || daw_audio_recording_is_active(&state->audio_recording);
}
// Captures a current generation-bound instrument and analyzes its complete effective settings privately.
DawActionResult daw_action_analyze_instrument(AppState* state,DawActionTarget target,bool chord,DawSoundWindow window,DawSoundAnalysis* out) {
    DawActionResult result=daw_action_get_instrument(state,target);if (!result.has_snapshot) return result;
    if (busy(state)) {result.status=DAW_ACTION_BUSY;result.message="Finish recording, bounce or the current edit before analyzing.";return result;}
    DawSoundSpec sound={result.snapshot.effective_preset,result.snapshot.effective_params,chord};
    if (!daw_sound_analyze(sound,window,out)) { result.status=DAW_ACTION_INVALID;result.message="Sound analysis failed or has invalid settings.";return result; }
    result.message="Sound measured at original level without effects.";return result;
}
// Reads an explicitly addressed library revision and performs bounded analysis without filesystem writes.
DawPresetStatus daw_action_analyze_preset(AppState* state,const char* id,const char* revision,bool chord,DawSoundWindow window,DawSoundAnalysis* out) {
    if (!state || !state->engine || !engine_is_control_thread(state->engine)) return DAW_PRESET_INVALID;
    if (busy(state)) return DAW_PRESET_BUSY;
    DawUserPreset preset;DawPresetStatus status=daw_preset_library_get(id,revision,&preset);if(status!=DAW_PRESET_OK)return status;
    return daw_sound_analyze((DawSoundSpec){preset.base,preset.params,chord},window,out)?DAW_PRESET_OK:DAW_PRESET_INVALID;
}
