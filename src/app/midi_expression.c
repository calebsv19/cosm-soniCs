#include "daw/midi_expression.h"
#include "app_state.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
// Returns a typed refusal without borrowing mutable state or caller text.
static DawMidiResult refused(DawMidiStatus status, const char* message) {
    return (DawMidiResult){status, message, 0};
}
// Produces a deterministic bounded signed random value independent of global RNG state.
static double variation(uint32_t* seed) {
    uint32_t x=*seed;x^=x<<13;x^=x>>17;x^=x<<5;*seed=x;
    return ((double)x / UINT32_MAX)*2.0-1.0;
}
// Finds the closest swung subdivision, with earlier-grid ties, in region-relative beat space.
static double nearest(double beat, double grid, double swing) {
    double center=floor(beat/grid), best=0, distance=INFINITY;
    for(int offset=-2;offset<=2;++offset){
        double index=center+offset;if(index<0)continue;
        double candidate=(index+((fmod(index,2.0)==1.0)?swing:0))*grid;
        double next=fabs(candidate-beat);
        if(next<distance){distance=next;best=candidate;}
    }
    return best;
}
// Validates operation-specific settings rather than silently ignoring unrelated controls.
static bool valid(DawMidiExpression p) {
    if(p.kind==DAW_MIDI_QUANTIZE)return isfinite(p.grid_beats) && p.grid_beats>=.03125 && p.grid_beats<=4 &&
        isfinite(p.strength) && p.strength>=0 && p.strength<=1 && isfinite(p.swing) && p.swing>=0 && p.swing<=.75 &&
        p.velocity_scale==0 && p.timing_ms==0 && p.velocity_amount==0 && p.seed==0;
    if(p.kind==DAW_MIDI_VELOCITY_SCALE)return isfinite(p.velocity_scale) && p.velocity_scale>=0 && p.velocity_scale<=4 &&
        p.grid_beats==0 && p.strength==0 && p.swing==0 && p.timing_ms==0 && p.velocity_amount==0 && p.seed==0;
    return p.kind==DAW_MIDI_HUMANIZE && isfinite(p.timing_ms) && p.timing_ms>=0 && p.timing_ms<=100 &&
        isfinite(p.velocity_amount) && p.velocity_amount>=0 && p.velocity_amount<=.5 &&
        p.grid_beats==0 && p.strength==0 && p.swing==0 && p.velocity_scale==0;
}
// Prepares all transformed notes privately and lets the existing transaction reserve and publish history.
DawMidiResult daw_action_transform_midi(AppState* state,const DawMidiSnapshot* expected,DawMidiExpression p) {
    if(!expected || !valid(p))return refused(DAW_MIDI_INVALID,"Invalid expression settings or unrelated fields.");
    DawMidiSnapshot current={0};DawMidiResult result=daw_action_get_midi(state,expected->target,&current);
    if(result.status!=DAW_MIDI_UNCHANGED)return result;
    EngineMidiNote* notes=NULL;
    if(daw_midi_actions_busy(state)){result=refused(DAW_MIDI_BUSY,"Finish the current edit and stop transport before transforming notes.");goto done;}
    if(!daw_midi_snapshot_equal(expected,&current)){result=refused(DAW_MIDI_STATE_CONFLICT,"Notes, region bounds or tempo changed; refresh before transforming.");goto done;}
    if(current.offset_frames!=0 || current.start_frame>INT64_MAX || current.duration_frames>INT64_MAX-current.start_frame){
        result=refused(DAW_MIDI_INVALID,"Expression requires an untrimmed region within the signed timeline.");goto done;
    }
    bool changes_timing=(p.kind==DAW_MIDI_QUANTIZE && p.strength>0) || (p.kind==DAW_MIDI_HUMANIZE && p.timing_ms>0);
    if(changes_timing && current.start_frame+current.duration_frames>9007199254740991ULL){result=refused(DAW_MIDI_INVALID,"Timing expression exceeds exact floating-point frame conversion range.");goto done;}
    if(current.note_count){notes=malloc((size_t)current.note_count*sizeof(*notes));if(!notes){result=refused(DAW_MIDI_ENGINE_REJECTED,"Expression preparation allocation failed.");goto done;}}
    double origin=tempo_map_samples_to_beats(&current.tempo,(int64_t)current.start_frame);
    uint32_t seed=p.seed?p.seed:0x534f4e49u;
    for(int i=0;i<current.note_count;++i){
        notes[i]=current.notes[i];double position=(double)notes[i].start_frame;
        if(p.kind==DAW_MIDI_QUANTIZE && p.strength>0){
            double beat=tempo_map_samples_to_beats(&current.tempo,(int64_t)(current.start_frame+notes[i].start_frame))-origin;
            double snapped=nearest(beat,p.grid_beats,p.swing);
            double absolute=tempo_map_beats_to_samples(&current.tempo,origin+snapped);
            position+=p.strength*(absolute-(double)current.start_frame-position);
        }else if(p.kind==DAW_MIDI_HUMANIZE){
            position+=variation(&seed)*p.timing_ms*.001*current.tempo.sample_rate;
            notes[i].velocity=(float)fmax(0,fmin(1,notes[i].velocity+variation(&seed)*p.velocity_amount));
        }else if(p.kind==DAW_MIDI_VELOCITY_SCALE){
            notes[i].velocity=(float)fmax(0,fmin(1,notes[i].velocity*p.velocity_scale));
        }
        if(!changes_timing)continue;
        // Timing boundary crossings reject the complete phrase instead of shortening/clamping notes.
        if(!isfinite(position) || position<0 || position>=(double)INT64_MAX){result=refused(DAW_MIDI_INVALID,"Expression would move a note beyond the region.");goto done;}
        notes[i].start_frame=(uint64_t)llround(position);
        if(notes[i].start_frame>current.duration_frames || notes[i].duration_frames>current.duration_frames-notes[i].start_frame){result=refused(DAW_MIDI_INVALID,"Expression would move a note beyond the region.");goto done;}
    }
    result=daw_action_edit_midi(state,&(DawMidiRequest){expected,notes,current.note_count,DAW_MIDI_REPLACE});
done:
    free(notes);daw_midi_snapshot_free(&current);return result;
}
