#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/sound_inspection.h"
#include "cJSON.h"
#include <string.h>
// Adds finite bounded numeric arrays without passing pointers to clients.
static void array(cJSON* object,const char* key,const float* values,int count) {
    cJSON* a=cJSON_AddArrayToObject(object,key);for(int i=0;i<count;++i)cJSON_AddItemToArray(a,cJSON_CreateNumber(values[i]));
}
// Declares enough rendering and calibration metadata to reproduce and interpret every measurement.
static cJSON* analysis_json(const DawSoundAnalysis* a) {
    cJSON* j=cJSON_CreateObject();cJSON_AddStringToObject(j,"base_voice",engine_instrument_preset_id_string(a->sound.base));
    cJSON* params=cJSON_AddObjectToObject(j,"parameters");
    for(int i=0;i<ENGINE_INSTRUMENT_PARAM_COUNT;++i){EngineInstrumentParamSpec spec;engine_instrument_param_spec(i,&spec);cJSON_AddNumberToObject(params,spec.id,engine_instrument_params_get(a->sound.params,i));}
    cJSON_AddStringToObject(j,"mode",a->sound.chord?"chord":"note");cJSON_AddStringToObject(j,"window",a->window==0?"attack":a->window==1?"sustain":"release");
    cJSON_AddNumberToObject(j,"synthesis_version",1);cJSON_AddNumberToObject(j,"sample_rate",DAW_SOUND_RATE);cJSON_AddNumberToObject(j,"channels",2);
    cJSON_AddNumberToObject(j,"frames",DAW_SOUND_FRAMES);cJSON_AddNumberToObject(j,"gate_frames",DAW_SOUND_GATE);cJSON_AddNumberToObject(j,"velocity",.5);
    uint8_t notes[4];int count=daw_sound_preview_notes(a->sound,notes);float pitches[4];for(int i=0;i<count;++i)pitches[i]=notes[i];array(j,"notes",pitches,count);
    cJSON_AddBoolToObject(j,"normalized",false);cJSON_AddBoolToObject(j,"effects",false);cJSON_AddBoolToObject(j,"project_automation",false);
    cJSON_AddNumberToObject(j,"peak",a->peak);cJSON_AddNumberToObject(j,"rms",a->rms);cJSON_AddNumberToObject(j,"dc",a->dc);cJSON_AddNumberToObject(j,"clipped_samples",a->clipped_samples);
    cJSON_AddNumberToObject(j,"window_start_frame",a->window_start);cJSON_AddNumberToObject(j,"waveform_span_frames",960);
    cJSON_AddNumberToObject(j,"spectrum_window_frames",2048);cJSON_AddStringToObject(j,"spectrum_calibration","Hann coherent-gain tonal peak amplitude dBFS, floor -160; not PSD or loudness");
    cJSON_AddStringToObject(j,"waveform_channels","mono mean");cJSON_AddStringToObject(j,"envelope_measure","stereo RMS of consecutive blocks covering all frames");
    array(j,"waveform",a->waveform,DAW_SOUND_POINTS);array(j,"envelope_rms",a->envelope_rms,DAW_SOUND_POINTS);
    array(j,"frequency_hz",a->frequency_hz,DAW_SOUND_BINS);array(j,"spectrum_db",a->spectrum_db,DAW_SOUND_BINS);return j;
}
// Executes bounded read-only analysis on the application owner using captured target or library identity.
cJSON* daw_control_analysis(DawControlBridge* bridge,AppState* state,const cJSON* request,const DawActionTarget* target) {
    const cJSON* mode=cJSON_GetObjectItemCaseSensitive(request,"mode"),*window=cJSON_GetObjectItemCaseSensitive(request,"window");
    cJSON* result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",false);
    if(!cJSON_IsString(mode) || (strcmp(mode->valuestring,"note") && strcmp(mode->valuestring,"chord")) || !cJSON_IsString(window)){
        cJSON_AddStringToObject(result,"code","invalid_request");return result;}
    DawSoundWindow selection;
    if(!strcmp(window->valuestring,"attack"))selection=DAW_SOUND_ATTACK;else if(!strcmp(window->valuestring,"sustain"))selection=DAW_SOUND_SUSTAIN;
    else if(!strcmp(window->valuestring,"release"))selection=DAW_SOUND_RELEASE;else {cJSON_AddStringToObject(result,"code","invalid_request");return result;}
    DawSoundAnalysis analysis;bool chord=!strcmp(mode->valuestring,"chord");
    if(target){DawActionResult action=daw_action_analyze_instrument(state,*target,chord,selection,&analysis);cJSON_Delete(result);result=daw_control_action_json(bridge,action);
        if(action.status>DAW_ACTION_UNCHANGED)return result;
    }else{
        const cJSON* id=cJSON_GetObjectItemCaseSensitive(request,"preset_id"),*revision=cJSON_GetObjectItemCaseSensitive(request,"preset_revision");
        if(!cJSON_IsString(id) || !cJSON_IsString(revision)){cJSON_AddStringToObject(result,"code","invalid_request");return result;}
        DawPresetStatus status=daw_action_analyze_preset(state,id->valuestring,revision->valuestring,chord,selection,&analysis);
        const char* codes[]={"preset_analysis","invalid_preset","preset_not_found","preset_conflict","preset_storage_failed","busy","unsupported_preset"};
        cJSON_AddStringToObject(result,"code",codes[status]);cJSON_AddStringToObject(result,"message",daw_preset_status_message(status));
        if(status!=DAW_PRESET_OK)return result;cJSON_ReplaceItemInObject(result,"ok",cJSON_CreateBool(true));
        cJSON_AddStringToObject(result,"preset_id",id->valuestring);cJSON_AddStringToObject(result,"preset_revision",revision->valuestring);
    }
    cJSON_AddItemToObject(result,"analysis",analysis_json(&analysis));return result;
}
