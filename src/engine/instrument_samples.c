#include "engine/instrument_samples.h"
#include "engine/piano_expression.h"
#include "audio/media_clip.h"
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pins one original recording and its prepared onset, loop and loudness metadata for process lifetime.
typedef struct InstrumentSample {
    const char* file;
    int note, layer;
    uint64_t trim, loop_start, loop_end, crossfade;
    float seam_correlation, gain;
    AudioMediaClip media;
} InstrumentSample;
// Stores slow soft/strong energy and covariance measured from actual prepared loop playback.
typedef struct InstrumentSampleBlendPoint {
    float soft, strong, covariance;
} InstrumentSampleBlendPoint;
// Describes one immutable onset and cyclic sustain energy curve for a recorded root.
typedef struct InstrumentSampleBlendEnvelope {
    const InstrumentSampleBlendPoint* initial;
    size_t initial_count;
    const InstrumentSampleBlendPoint* cycle;
    size_t cycle_count;
    uint64_t loop_end, period;
} InstrumentSampleBlendEnvelope;
static InstrumentSample violin_samples[] = {
#include "../../assets/instruments/vsco2_solo_violin_v1/sample_map_stabilized.inc"
};
static InstrumentSample trumpet_samples[] = {
#include "../../assets/instruments/vsco2_trumpet_v1/sample_map_stabilized.inc"
};
static InstrumentSample clarinet_samples[] = {
#include "../../assets/instruments/vsco2_clarinet_v1/sample_map_stabilized.inc"
};
static InstrumentSample saxophone_samples[] = {
#include "../../assets/instruments/vcsl_tenor_saxophone_v1/sample_map_stabilized.inc"
};
static const float saxophone_blend_correlations[] = {
#include "../../assets/instruments/vcsl_tenor_saxophone_v1/blend_correlation.inc"
};
_Static_assert(sizeof(saxophone_blend_correlations)/sizeof(float)==sizeof(saxophone_samples)/sizeof(InstrumentSample), "Saxophone layer metadata must match sample rows");
static InstrumentSample flute_samples[] = {
#include "../../assets/instruments/vsco2_flute_v1/sample_map_refined.inc"
};
static const float flute_blend_correlations[] = {
#include "../../assets/instruments/vsco2_flute_v1/blend_correlation.inc"
};
static const float flute_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_flute_v1/pitch_ratios.inc"
};
_Static_assert(sizeof(flute_pitch_ratios)/sizeof(float)==sizeof(flute_samples)/sizeof(InstrumentSample), "flute pitch metadata must match sample rows");
_Static_assert(sizeof(flute_blend_correlations)/sizeof(float)==sizeof(flute_samples)/sizeof(InstrumentSample), "flute blend metadata must match sample rows");
static InstrumentSample cello_samples[] = {
#include "../../assets/instruments/vsco2_cello_section_v1/sample_map_refined.inc"
};
static const float cello_blend_correlations[] = {
#include "../../assets/instruments/vsco2_cello_section_v1/blend_correlation.inc"
};
static const float cello_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_cello_section_v1/pitch_ratios.inc"
};
_Static_assert(sizeof(cello_pitch_ratios)/sizeof(float)==sizeof(cello_samples)/sizeof(InstrumentSample), "cello pitch metadata must match sample rows");
_Static_assert(sizeof(cello_blend_correlations)/sizeof(float)==sizeof(cello_samples)/sizeof(InstrumentSample), "cello blend metadata must match sample rows");
#include "../../assets/instruments/vsco2_flute_v1/blend_envelopes.inc"
#include "../../assets/instruments/vsco2_cello_section_v1/blend_envelopes.inc"
_Static_assert(sizeof(flute_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(flute_samples)/sizeof(InstrumentSample), "Flute energy curves must match sample rows");
_Static_assert(sizeof(cello_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(cello_samples)/sizeof(InstrumentSample), "Cello energy curves must match sample rows");
static InstrumentSample oboe_samples[] = {
#include "../../assets/instruments/vsco2_oboe_v1/sample_map_refined.inc"
};
static const float oboe_blend_correlations[] = {
#include "../../assets/instruments/vsco2_oboe_v1/blend_correlation.inc"
};
static const float oboe_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_oboe_v1/pitch_ratios.inc"
};
#include "../../assets/instruments/vsco2_oboe_v1/blend_envelopes.inc"
_Static_assert(sizeof(oboe_pitch_ratios)/sizeof(float)==sizeof(oboe_samples)/sizeof(InstrumentSample), "oboe pitch rows");
_Static_assert(sizeof(oboe_blend_correlations)/sizeof(float)==sizeof(oboe_samples)/sizeof(InstrumentSample), "oboe blend rows");
_Static_assert(sizeof(oboe_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(oboe_samples)/sizeof(InstrumentSample), "oboe curve rows");
static InstrumentSample horn_samples[] = {
#include "../../assets/instruments/vsco2_french_horn_v1/sample_map_refined.inc"
};
static const float horn_blend_correlations[] = {
#include "../../assets/instruments/vsco2_french_horn_v1/blend_correlation.inc"
};
static const float horn_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_french_horn_v1/pitch_ratios.inc"
};
#include "../../assets/instruments/vsco2_french_horn_v1/blend_envelopes.inc"
_Static_assert(sizeof(horn_pitch_ratios)/sizeof(float)==sizeof(horn_samples)/sizeof(InstrumentSample), "horn pitch rows");
_Static_assert(sizeof(horn_blend_correlations)/sizeof(float)==sizeof(horn_samples)/sizeof(InstrumentSample), "horn blend rows");
_Static_assert(sizeof(horn_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(horn_samples)/sizeof(InstrumentSample), "horn curve rows");
static InstrumentSample bassoon_samples[] = {
#include "../../assets/instruments/vsco2_bassoon_v1/sample_map_refined.inc"
};
static const float bassoon_blend_correlations[] = {
#include "../../assets/instruments/vsco2_bassoon_v1/blend_correlation.inc"
};
static const float bassoon_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_bassoon_v1/pitch_ratios.inc"
};
#include "../../assets/instruments/vsco2_bassoon_v1/blend_envelopes.inc"
_Static_assert(sizeof(bassoon_pitch_ratios)/sizeof(float)==sizeof(bassoon_samples)/sizeof(InstrumentSample), "bassoon pitch rows");
_Static_assert(sizeof(bassoon_blend_correlations)/sizeof(float)==sizeof(bassoon_samples)/sizeof(InstrumentSample), "bassoon blend rows");
_Static_assert(sizeof(bassoon_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(bassoon_samples)/sizeof(InstrumentSample), "bassoon curve rows");
static InstrumentSample trombone_samples[] = {
#include "../../assets/instruments/vsco2_tenor_trombone_v1/sample_map_refined.inc"
};
static const float trombone_blend_correlations[] = {
#include "../../assets/instruments/vsco2_tenor_trombone_v1/blend_correlation.inc"
};
static const float trombone_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_tenor_trombone_v1/pitch_ratios.inc"
};
#include "../../assets/instruments/vsco2_tenor_trombone_v1/blend_envelopes.inc"
_Static_assert(sizeof(trombone_pitch_ratios)/sizeof(float)==sizeof(trombone_samples)/sizeof(InstrumentSample), "trombone pitch rows");
_Static_assert(sizeof(trombone_blend_correlations)/sizeof(float)==sizeof(trombone_samples)/sizeof(InstrumentSample), "trombone blend rows");
_Static_assert(sizeof(trombone_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(trombone_samples)/sizeof(InstrumentSample), "trombone curve rows");
static InstrumentSample viola_samples[] = {
#include "../../assets/instruments/vsco2_viola_section_v1/sample_map_refined.inc"
};
static const float viola_blend_correlations[] = {
#include "../../assets/instruments/vsco2_viola_section_v1/blend_correlation.inc"
};
static const float viola_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_viola_section_v1/pitch_ratios.inc"
};
#include "../../assets/instruments/vsco2_viola_section_v1/blend_envelopes.inc"
_Static_assert(sizeof(viola_pitch_ratios)/sizeof(float)==sizeof(viola_samples)/sizeof(InstrumentSample), "viola pitch rows");
_Static_assert(sizeof(viola_blend_correlations)/sizeof(float)==sizeof(viola_samples)/sizeof(InstrumentSample), "viola blend rows");
_Static_assert(sizeof(viola_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(viola_samples)/sizeof(InstrumentSample), "viola curve rows");
static InstrumentSample vlnsection_samples[] = {
#include "../../assets/instruments/vsco2_violin_section_v1/sample_map_refined.inc"
};
static const float vlnsection_blend_correlations[] = {
#include "../../assets/instruments/vsco2_violin_section_v1/blend_correlation.inc"
};
static const float vlnsection_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_violin_section_v1/pitch_ratios.inc"
};
#include "../../assets/instruments/vsco2_violin_section_v1/blend_envelopes.inc"
_Static_assert(sizeof(vlnsection_pitch_ratios)/sizeof(float)==sizeof(vlnsection_samples)/sizeof(InstrumentSample), "vlnsection pitch rows");
_Static_assert(sizeof(vlnsection_blend_correlations)/sizeof(float)==sizeof(vlnsection_samples)/sizeof(InstrumentSample), "vlnsection blend rows");
_Static_assert(sizeof(vlnsection_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(vlnsection_samples)/sizeof(InstrumentSample), "vlnsection curve rows");
static InstrumentSample doublebass_samples[] = {
#include "../../assets/instruments/vsco2_double_bass_v1/sample_map_refined.inc"
};
static const float doublebass_blend_correlations[] = {
#include "../../assets/instruments/vsco2_double_bass_v1/blend_correlation.inc"
};
static const float doublebass_pitch_ratios[] = {
#include "../../assets/instruments/vsco2_double_bass_v1/pitch_ratios.inc"
};
#include "../../assets/instruments/vsco2_double_bass_v1/blend_envelopes.inc"
_Static_assert(sizeof(doublebass_pitch_ratios)/sizeof(float)==sizeof(doublebass_samples)/sizeof(InstrumentSample), "doublebass pitch rows");
_Static_assert(sizeof(doublebass_blend_correlations)/sizeof(float)==sizeof(doublebass_samples)/sizeof(InstrumentSample), "doublebass blend rows");
_Static_assert(sizeof(doublebass_blend_envelopes)/sizeof(InstrumentSampleBlendEnvelope)==sizeof(doublebass_samples)/sizeof(InstrumentSample), "doublebass curve rows");
static InstrumentSample piano_samples[] = {
#include "../../assets/instruments/salamander_piano_v1/sample_map.inc"
};
// Owns one immutable bank and its bounded precomputed MIDI/root/layer lookup.
typedef struct InstrumentSampleBank {
    EngineInstrumentPresetId preset;
    EngineSampleBankInfo info;
    InstrumentSample* samples;
    size_t count;
    int sample_rate;
    const float* blend_correlations;
    const float* pitch_ratios;
    const InstrumentSampleBlendEnvelope* blend_envelopes;
    bool decaying;
    bool ready;
    int roots[128][3];
    double ratios[128];
} InstrumentSampleBank;
static InstrumentSampleBank banks[] = {
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN,.info={"vsco2_solo_violin_v1",55,96,15,2,"bowed vibrato sustain"},.samples=violin_samples,.count=30,.sample_rate=44100},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_TRUMPET,.info={"vsco2_trumpet_v1",53,84,10,2,"open trumpet sustain"},.samples=trumpet_samples,.count=20,.sample_rate=44100},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_CLARINET,.info={"vsco2_clarinet_v1",50,89,11,3,"clarinet long sustain"},.samples=clarinet_samples,.count=33,.sample_rate=44100},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_SAXOPHONE,.info={"vcsl_tenor_saxophone_v1",44,88,23,2,"tenor saxophone non-vibrato sustain"},.samples=saxophone_samples,.count=46,.sample_rate=48000,.blend_correlations=saxophone_blend_correlations},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_FLUTE,.info={"vsco2_flute_v1",60,93,9,2,"flute non-vibrato sustain"},.samples=flute_samples,.count=18,.sample_rate=44100,.blend_correlations=flute_blend_correlations,.pitch_ratios=flute_pitch_ratios,.blend_envelopes=flute_blend_envelopes},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_CELLO_SECTION,.info={"vsco2_cello_section_v1",36,77,13,2,"cello section bowed vibrato sustain"},.samples=cello_samples,.count=26,.sample_rate=44100,.blend_correlations=cello_blend_correlations,.pitch_ratios=cello_pitch_ratios,.blend_envelopes=cello_blend_envelopes},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_OBOE,.info={"vsco2_oboe_v1",58,89,9,2,"oboe non-vibrato sustain"},.samples=oboe_samples,.count=18,.sample_rate=44100,.blend_correlations=oboe_blend_correlations,.pitch_ratios=oboe_pitch_ratios,.blend_envelopes=oboe_blend_envelopes},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_FRENCH_HORN,.info={"vsco2_french_horn_v1",36,60,8,2,"French horn open sustain"},.samples=horn_samples,.count=16,.sample_rate=44100,.blend_correlations=horn_blend_correlations,.pitch_ratios=horn_pitch_ratios,.blend_envelopes=horn_blend_envelopes},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_BASSOON,.info={"vsco2_bassoon_v1",34,72,12,2,"bassoon sustained tone"},.samples=bassoon_samples,.count=24,.sample_rate=44100,.blend_correlations=bassoon_blend_correlations,.pitch_ratios=bassoon_pitch_ratios,.blend_envelopes=bassoon_blend_envelopes},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_TENOR_TROMBONE,.info={"vsco2_tenor_trombone_v1",34,65,11,2,"open tenor trombone sustain"},.samples=trombone_samples,.count=22,.sample_rate=44100,.blend_correlations=trombone_blend_correlations,.pitch_ratios=trombone_pitch_ratios,.blend_envelopes=trombone_blend_envelopes},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_ACOUSTIC_PIANO,.info={"salamander_piano_v1",21,108,30,3,"acoustic piano velocity-selected strike with natural decay"},.samples=piano_samples,.count=90,.sample_rate=48000,.decaying=true},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLA_SECTION,.info={"vsco2_viola_section_v1",48,86,13,2,"viola section bowed vibrato sustain"},.samples=viola_samples,.count=26,.sample_rate=44100,.blend_correlations=viola_blend_correlations,.pitch_ratios=viola_pitch_ratios,.blend_envelopes=viola_blend_envelopes},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_DOUBLE_BASS,.info={"vsco2_double_bass_v1",28,59,14,2,"solo double bass bowed non-vibrato sustain"},.samples=doublebass_samples,.count=28,.sample_rate=44100,.blend_correlations=doublebass_blend_correlations,.pitch_ratios=doublebass_pitch_ratios,.blend_envelopes=doublebass_blend_envelopes},
    {.preset=ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN_SECTION,.info={"vsco2_violin_section_v1",55,86,11,2,"violin section bowed vibrato sustain"},.samples=vlnsection_samples,.count=22,.sample_rate=44100,.blend_correlations=vlnsection_blend_correlations,.pitch_ratios=vlnsection_pitch_ratios,.blend_envelopes=vlnsection_blend_envelopes}
};
static pthread_once_t bank_once = PTHREAD_ONCE_INIT;
// Finds a fixed bank identity without allocating.
static InstrumentSampleBank* find_bank(EngineInstrumentPresetId preset) {
    for (size_t i=0;i<sizeof(banks)/sizeof(banks[0]);++i) if (banks[i].preset==preset) return &banks[i];
    return NULL;
}
// Copies one fixed bank description for UI, MCP and validation.
bool engine_instrument_sample_bank_info(EngineInstrumentPresetId preset, EngineSampleBankInfo* out) {
    InstrumentSampleBank* bank=find_bank(preset); if (!bank || !out) return false; *out=bank->info; return true;
}
// Reports immutable preload results for the selected bank.
bool engine_instrument_sample_bank_ready(EngineInstrumentPresetId preset) {
    InstrumentSampleBank* bank=find_bank(preset); return bank && bank->ready;
}
// Loads the fixed bundled bank once before any source is published to rendering.
static void load_one_bank(InstrumentSampleBank* bank) {
    char path[4096], directory[4096];
    const char* root=getenv("DAW_INSTRUMENT_BANKS_ROOT");
    if (!root || !*root) root="assets/instruments";
    int length=snprintf(directory,sizeof(directory),"%s/%s",root,bank->info.id);
    const char* violin_override=getenv("DAW_INSTRUMENT_ASSET_ROOT");
    if (bank->preset==ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN && violin_override && *violin_override)
        length=snprintf(directory,sizeof(directory),"%s",violin_override);
    if (length<0 || length>=(int)sizeof(directory)) return;
    InstrumentSample* samples=bank->samples;
    for (size_t i=0;i<bank->count;++i) {
        InstrumentSample* s=&samples[i];
        int n=snprintf(path,sizeof(path),"%s/%s",directory,s->file);
        if (n<0 || n>=(int)sizeof(path) || !audio_media_clip_load_wav(path,bank->sample_rate,&s->media) ||
            s->media.sample_rate!=bank->sample_rate || s->media.channels<1 || s->media.channels>2 ||
            (!bank->decaying && (s->trim+s->loop_end>=s->media.frame_count || s->loop_end<=s->loop_start+s->crossfade)) ||
            (bank->decaying && s->trim+2>=s->media.frame_count)) goto fail;
        if (bank->pitch_ratios && (!isfinite(bank->pitch_ratios[i]) || bank->pitch_ratios[i]<=0 ||
            s->trim+s->loop_end*(double)bank->pitch_ratios[i]+1>=s->media.frame_count)) goto fail;
        if (bank->blend_envelopes) {
            const InstrumentSampleBlendEnvelope* curve=&bank->blend_envelopes[i];
            if (!curve->initial || !curve->cycle || curve->initial_count<2 || curve->cycle_count<2 ||
                curve->loop_end!=s->loop_end || curve->period!=s->loop_end-s->loop_start-s->crossfade) goto fail;
        }
        for (uint64_t k=0;k<s->media.frame_count*(uint64_t)s->media.channels;++k)
            if (!isfinite(s->media.samples[k])) goto fail;
        // New banks are centered mono at preparation; preserve original violin stereo arithmetic exactly.
        if (bank->preset!=ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN && s->media.channels==2) {
            for (uint64_t k=0;k<s->media.frame_count;++k)
                s->media.samples[k]=(s->media.samples[k*2]+s->media.samples[k*2+1])*.5f;
            float* smaller=realloc(s->media.samples,(size_t)s->media.frame_count*sizeof(float));
            if (smaller) s->media.samples=smaller;
            s->media.channels=1;
        }
    }
    for (int note=bank->info.minimum_note;note<=bank->info.maximum_note;++note) {
        int distance=128,soft=0;
        for (size_t i=0;i<bank->count;++i)
            if (!samples[i].layer && abs(note-samples[i].note)<distance) {
                distance=abs(note-samples[i].note);soft=(int)i;
            }
        bank->roots[note][0]=soft;
        for (int layer=1;layer<bank->info.layers;++layer) {
            bool found=false;
            for (size_t i=0;i<bank->count;++i)
                if (samples[i].layer==layer && samples[i].note==samples[soft].note) {
                    bank->roots[note][layer]=(int)i;found=true;break;
                }
            if (!found) goto fail;
        }
        bank->ratios[note]=pow(2.,(note-samples[soft].note)/12.);
    }
    bank->ready=true;return;
fail:
    fprintf(stderr,"Sample bank unavailable: %s\n",bank->info.id);
    for (size_t i=0;i<bank->count;++i) audio_media_clip_free(&samples[i].media);
}
// Preloads all fixed banks on source preparation before any audio rendering is admitted.
static void load_bank(void) {
    for (size_t i=0;i<sizeof(banks)/sizeof(banks[0]);++i) load_one_bank(&banks[i]);
}
// Serializes preload callers while keeping I/O and allocations off render functions.
bool engine_instrument_samples_init(void) { pthread_once(&bank_once,load_bank); return banks[0].ready; }
// Preserves the original violin readiness API for existing callers.
bool engine_instrument_samples_ready(void) { return banks[0].ready; }
// Interpolates a stereo recording to the centered solo voice without allocating.
static float at(const InstrumentSample* s, double pos) {
    pos += s->trim;
    uint64_t a = (uint64_t)pos;
    if (a + 1 >= s->media.frame_count) return 0;
    float t = (float)(pos - a), x = 0;
    for (int c = 0; c < s->media.channels; ++c)
        x += s->media.samples[a * s->media.channels + c] * (1-t) +
            s->media.samples[(a+1) * s->media.channels + c] * t;
    return x / s->media.channels;
}
// Repeats phase-matched steady bow material with the prepared per-recording overlap.
static float looped(const InstrumentSample* s, double pos) {
    const double cross = s->crossfade;
    double period = s->loop_end - s->loop_start - cross;
    if (pos >= s->loop_end) pos = s->loop_start + cross + fmod(pos - s->loop_end, period);
    if (pos < s->loop_end - cross) return at(s, pos) * s->gain;
    double u = pos - (s->loop_end - cross);
    float w = (float)(.5 - .5*cos(3.14159265358979323846*u/cross));
    // Compensate the expected overlap energy using the measured positive seam correlation.
    float power=1-2*w*(1-w)*(1-s->seam_correlation);
    return (at(s, pos)*(1-w) + at(s, s->loop_start+u)*w) * s->gain / sqrtf(power);
}
// Reads a pitch-centered prepared bank while leaving the original-bank playback arithmetic intact.
static float looped_pitch_centered(const InstrumentSample* s, double pos, float ratio) {
    const double cross=s->crossfade;
    double period=s->loop_end-s->loop_start-cross;
    if (pos>=s->loop_end) pos=s->loop_start+cross+fmod(pos-s->loop_end,period);
    if (pos<s->loop_end-cross) return at(s,pos*ratio)*s->gain;
    double u=pos-(s->loop_end-cross);
    float w=(float)(.5-.5*cos(3.14159265358979323846*u/cross));
    float power=1-2*w*(1-w)*(1-s->seam_correlation);
    return (at(s,pos*ratio)*(1-w)+at(s,(s->loop_start+u)*ratio)*w)*s->gain/sqrtf(power);
}
// Interpolates a prepared slow energy curve with continuous wrap at the sustain loop boundary.
static InstrumentSampleBlendPoint blend_point(const InstrumentSampleBlendEnvelope* curve, double pos) {
    bool repeat=pos>=curve->loop_end;
    const InstrumentSampleBlendPoint* points=repeat ? curve->cycle : curve->initial;
    size_t count=repeat ? curve->cycle_count : curve->initial_count;
    double index=repeat ? fmod(pos-curve->loop_end,(double)curve->period)*count/curve->period : pos*(count-1)/curve->loop_end;
    size_t low=(size_t)index;
    if (low>=count) low=count-1;
    size_t high=low+1<count ? low+1 : (repeat ? 0 : low);
    float t=(float)(index-low);
    return (InstrumentSampleBlendPoint){
        points[low].soft+(points[high].soft-points[low].soft)*t,
        points[low].strong+(points[high].strong-points[low].strong)*t,
        points[low].covariance+(points[high].covariance-points[low].covariance)*t};
}
// Corrects layer interference within a strict gain range while preserving attacks and dynamics endpoints.
static float prepared_blend_gain(const InstrumentSampleBank* bank, int root, double pos, float c, float s) {
    float old=1/sqrtf(1+2*c*s*bank->blend_correlations[root]);
    if (pos<=bank->sample_rate*.3) return old;
    InstrumentSampleBlendPoint p=blend_point(&bank->blend_envelopes[root],pos);
    float expected=c*c*p.soft+s*s*p.strong;
    float gain=sqrtf(expected/fmaxf(1e-15f,expected+2*c*s*p.covariance));
    gain=fminf(1.35f,fmaxf(.75f,gain));
    float t=(float)fmin(1,(pos-bank->sample_rate*.3)/(bank->sample_rate*.3));
    t=t*t*(3-2*t);
    return old+(gain-old)*t;
}
// Selects a nearby recorded root and blends genuine dynamics while honoring note gate and release.
float engine_instrument_sample_bank_note(EngineInstrumentPresetId preset, uint8_t note, float velocity,
    EngineInstrumentParams p, int rate, uint64_t age, uint64_t gate) {
    const InstrumentSampleBank* bank=find_bank(preset);
    if (!bank || !bank->ready || rate<=0 || note<bank->info.minimum_note || note>bank->info.maximum_note || velocity<=0) return 0;
    if (bank->decaying) {
        int layer=velocity<.35f ? 0 : (velocity<.75f ? 1 : 2);
        const InstrumentSample* strike=&bank->samples[bank->roots[note][layer]];
        double position=(double)age*bank->sample_rate/rate*bank->ratios[note];
        return at(strike,position)*strike->gain*engine_instrument_samples_envelope(p,rate,age,gate)*p.level*(.25f+.75f*velocity);
    }
    const InstrumentSample* soft=&bank->samples[bank->roots[note][0]];
    double pos=(double)age * bank->sample_rate/rate * bank->ratios[note];
    float dynamics=fminf(1,fmaxf(0,p.dynamics+(velocity-.5f)*p.velocity_depth));
    float angle;
    const InstrumentSample* strong;
    if (bank->info.layers==2) {
        strong=&bank->samples[bank->roots[note][1]];
        angle=dynamics*1.57079632679489661923f;
    } else {
        float position=dynamics*(bank->info.layers-1);
        int layer=(int)position;
        if (layer>=bank->info.layers-1) layer=bank->info.layers-2;
        soft=&bank->samples[bank->roots[note][layer]];
        strong=&bank->samples[bank->roots[note][layer+1]];
        angle=(position-layer)*1.57079632679489661923f;
    }
    // Ease wind layer entry and exit so the middle clarinet layer has no timbre-slope corner.
    if (preset!=ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN) {
        float t=angle/1.57079632679489661923f;
        angle=t*t*t*(t*(6*t-15)+10)*1.57079632679489661923f;
    }
    float blend;
    if (bank->pitch_ratios) {
        float a=looped_pitch_centered(soft,pos,bank->pitch_ratios[soft-bank->samples]);
        float b=looped_pitch_centered(strong,pos,bank->pitch_ratios[strong-bank->samples]);
        blend=a*cosf(angle)+b*sinf(angle);
    } else blend=looped(soft,pos)*cosf(angle)+looped(strong,pos)*sinf(angle);
    // Attenuate correlated prepared layers to retain expected blend energy without cancellation boosts.
    if (bank->blend_envelopes && pos>bank->sample_rate*.3) {
        blend*=prepared_blend_gain(bank,bank->roots[note][0],pos,cosf(angle),sinf(angle));
    } else if (bank->blend_correlations) {
        float correlation=bank->blend_correlations[bank->roots[note][0]];
        blend/=sqrtf(1+2*cosf(angle)*sinf(angle)*correlation);
    }
    float env=engine_instrument_samples_envelope(p,rate,age,gate);
    return blend * env * p.level * (.25f+.75f*velocity) * (.55f+.45f*dynamics);
}
// Preserves the violin entry point and its exact two-layer arithmetic.
float engine_instrument_samples_note(uint8_t note, float velocity, EngineInstrumentParams p,
    int rate, uint64_t age, uint64_t gate) {
    return engine_instrument_sample_bank_note(ENGINE_INSTRUMENT_PRESET_SAMPLED_VIOLIN,note,velocity,p,rate,age,gate);
}

// Evaluates the same click-protected S-curve gate used by playback and diagnostic plots.
float engine_instrument_samples_envelope(EngineInstrumentParams p, int rate, uint64_t age, uint64_t gate) {
    if (rate<=0) return 0;
    double attack=fmax(3,p.attack_ms)*rate/1000.;
    double t=fmin(1,(double)age/attack);
    float env=(float)(t*t*(3-2*t));
    if (age >= gate) {
        double release=fmax(4,p.release_ms)*rate/1000.;
        if ((double)(age-gate) >= release) return 0;
        double held=fmin(1,(double)gate/attack);
        double off=(double)(age-gate)/release;
        env=(float)(held*held*(3-2*held)*(1-off*off*(3-2*off)));
    }
    return env;
}

// Chooses an octave of C inside a recorded bank, retaining C4 for other voices.
uint8_t engine_instrument_preview_note(EngineInstrumentPresetId preset) {
    EngineSampleBankInfo info;int note=60;
    if (engine_instrument_sample_bank_info(preset,&info)) {
        while(note<info.minimum_note)note+=12;
        while(note>info.maximum_note)note-=12;
    }
    return (uint8_t)note;
}
