#include "daw/sound_inspection.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
// Validates exact physical settings before the production source can sanitize them.
bool daw_sound_spec_valid(DawSoundSpec sound) {
    if (sound.base < 0 || sound.base >= ENGINE_INSTRUMENT_PRESET_COUNT) return false;
    for (int i=0;i<ENGINE_INSTRUMENT_PARAM_COUNT;++i) {
        EngineInstrumentParamSpec spec; engine_instrument_param_spec(i,&spec);
        float value=engine_instrument_params_get(sound.params,i);
        if (!isfinite(value) || value<spec.min_value || value>spec.max_value) return false;
    }
    return true;
}
// Builds one bounded private production render shared by WAV publication and sound inspection.
bool daw_sound_render(DawSoundSpec sound, DawSoundRender* out) {
    if (!out || !daw_sound_spec_valid(sound)) return false;
    memset(out,0,sizeof(*out));
    EngineInstrumentSource* source=engine_instrument_source_create();
    if (!source) return false;
    out->samples=calloc(DAW_SOUND_FRAMES*2,sizeof(float));
    if (!out->samples) { engine_instrument_source_destroy(source); return false; }
    EngineMidiNote notes[]={{0,DAW_SOUND_GATE,48,.5f},{0,DAW_SOUND_GATE,55,.5f},{0,DAW_SOUND_GATE,60,.5f},{0,DAW_SOUND_GATE,64,.5f}};
    if (!sound.chord) notes[0].note=60;
    bool ok=engine_instrument_source_set_midi_clip(source,0,DAW_SOUND_FRAMES,sound.base,sound.params,notes,sound.chord?4:1,NULL,0,NULL,0);
    if (ok) {
        engine_instrument_source_reset(source,DAW_SOUND_RATE,2);
        for (int frame=0;frame<DAW_SOUND_FRAMES;frame+=256) {
            int count=DAW_SOUND_FRAMES-frame<256?DAW_SOUND_FRAMES-frame:256;
            engine_instrument_source_render(source,out->samples+frame*2,count,frame);
        }
        double power=0,sum=0;
        for (int i=0;i<DAW_SOUND_FRAMES*2;++i) {
            double sample=out->samples[i]; if (!isfinite(sample)) { ok=false; break; }
            if (fabs(sample)>out->peak) out->peak=fabs(sample);
            if (fabs(sample)>=1) ++out->clipped_samples;
            power+=sample*sample; sum+=sample;
        }
        out->rms=sqrt(power/(DAW_SOUND_FRAMES*2)); out->dc=sum/(DAW_SOUND_FRAMES*2);
    }
    engine_instrument_source_destroy(source);
    if (!ok) daw_sound_render_free(out);
    return ok;
}
// Releases an isolated render without retaining borrowed source/device storage.
void daw_sound_render_free(DawSoundRender* render) {
    if (!render) return; free(render->samples); memset(render,0,sizeof(*render));
}
