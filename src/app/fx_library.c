#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "daw/fx_library.h"
#include "daw/save_file.h"
#include "cJSON.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
// Resolves a private chain subdirectory; read paths never create missing storage.
static bool root(char out[1024], bool create) {
    char parent[1024];if(!daw_preset_library_root(parent,sizeof(parent),create) || snprintf(out,1024,"%s/FxChains",parent)>=1024)return false;
    struct stat st;if(create && mkdir(out,0700) && errno!=EEXIST)return false;
    if(lstat(out,&st))return !create && errno==ENOENT;
    return S_ISDIR(st.st_mode) && st.st_uid==geteuid() && !(st.st_mode&0077);
}
// Refuses unknown or duplicate fields before decoding any portable values.
static bool fields(const cJSON* object,const char* const* keys) {
    if(!cJSON_IsObject(object))return false;
    for(const cJSON* a=object->child;a;a=a->next){bool found=false;for(int i=0;keys[i];++i)if(!strcmp(keys[i],a->string))found=true;
        if(!found)return false;for(const cJSON* b=a->next;b;b=b->next)if(!strcmp(a->string,b->string))return false;}
    return true;
}
// Reads a JSON string without coercing missing or numeric fields.
static const char* string(const cJSON* object,const char* key) {
    const cJSON* value=cJSON_GetObjectItemCaseSensitive(object,key);return cJSON_IsString(value)?value->valuestring:NULL;
}
// Checks complete registry-compatible settings while retaining stored native fallback and beat values.
static bool chain_valid(const Engine* engine,const FxMasterSnapshot* chain) {
    if(!engine || !engine_is_control_thread(engine) || !chain || chain->count<0 || chain->count>FX_MASTER_MAX)return false;
    for(int i=0;i<chain->count;++i){const FxMasterInstanceInfo* item=&chain->items[i];if(item->automation.count)return false;const EffectParamSpec* specs=NULL;uint32_t count=0;FxDesc desc;
        if(!engine_fx_registry_get_desc(engine,item->type,&desc) || !engine_fx_registry_get_param_specs(engine,item->type,&specs,&count) || count!=item->param_count || count>FX_MAX_PARAMS)return false;
        for(uint32_t p=0;p<count;++p){const EffectParamSpec* spec=&specs[p];float value=item->params[p],beats=item->param_beats[p];FxParamMode mode=item->param_mode[p];
            if(!isfinite(value) || value<spec->min_value || value>spec->max_value || !isfinite(beats) || mode<FX_PARAM_MODE_NATIVE || mode>FX_PARAM_MODE_BEAT_RATE)return false;
            if((spec->type==FX_PARAM_TYPE_BOOL || spec->type==FX_PARAM_TYPE_ENUM || spec->type==FX_PARAM_TYPE_INT) && value!=floorf(value))return false;
            if(mode==FX_PARAM_MODE_NATIVE && beats!=0)return false;
            if(mode!=FX_PARAM_MODE_NATIVE && (!(spec->flags&FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE) || beats<=0))return false;
            if(mode==FX_PARAM_MODE_BEATS && spec->unit!=FX_PARAM_UNIT_MS && spec->unit!=FX_PARAM_UNIT_SECONDS)return false;
            if(mode==FX_PARAM_MODE_BEAT_RATE && spec->unit!=FX_PARAM_UNIT_HZ)return false;
            if((spec->flags&FX_PARAM_FLAG_READ_ONLY_METER) && (value!=spec->default_value || mode!=FX_PARAM_MODE_NATIVE))return false;
        }
    }return true;
}
// Serializes stable type/parameter identities and every ordered setting, omitting runtime instance IDs.
static char* encode(const Engine* engine,const DawFxPreset* preset) {
    cJSON* doc=cJSON_CreateObject();if(!doc)return NULL;
    cJSON_AddStringToObject(doc,"format","sonics-fx-chain");cJSON_AddNumberToObject(doc,"version",1);cJSON_AddNumberToObject(doc,"effects_version",1);
    cJSON_AddStringToObject(doc,"id",preset->id);cJSON_AddStringToObject(doc,"revision",preset->revision);cJSON_AddStringToObject(doc,"name",preset->name);
    cJSON* effects=cJSON_AddArrayToObject(doc,"effects");
    for(int i=0;i<preset->chain.count;++i){const FxMasterInstanceInfo* item=&preset->chain.items[i];const EffectParamSpec* specs=NULL;uint32_t count=0;
        if(!engine_fx_registry_get_param_specs(engine,item->type,&specs,&count)){cJSON_Delete(doc);return NULL;}
        cJSON* effect=cJSON_CreateObject();cJSON_AddItemToArray(effects,effect);cJSON_AddNumberToObject(effect,"type_id",item->type);cJSON_AddBoolToObject(effect,"enabled",item->enabled);
        cJSON* params=cJSON_AddArrayToObject(effect,"parameters");
        for(uint32_t p=0;p<count;++p){cJSON* param=cJSON_CreateObject();cJSON_AddItemToArray(params,param);cJSON_AddStringToObject(param,"id",specs[p].id);
            cJSON_AddNumberToObject(param,"value",item->params[p]);cJSON_AddNumberToObject(param,"mode",item->param_mode[p]);cJSON_AddNumberToObject(param,"beats",item->param_beats[p]);}
    }
    char* bytes=cJSON_PrintUnformatted(doc);cJSON_Delete(doc);return bytes;
}
// Decodes a complete versioned chain and rejects missing, unknown, duplicated or incompatible settings.
static DawPresetStatus decode(const Engine* engine,const char* bytes,DawFxPreset* out) {
    const char* const keys[]={"format","version","effects_version","id","revision","name","effects",NULL};
    const char* const effect_keys[]={"type_id","enabled","parameters",NULL};const char* const param_keys[]={"id","value","mode","beats",NULL};
    cJSON* doc=cJSON_ParseWithOpts(bytes,NULL,true);DawPresetStatus status=DAW_PRESET_INVALID;DawFxPreset candidate={0};
    if(!fields(doc,keys))goto done;const char* format=string(doc,"format");const cJSON* version=cJSON_GetObjectItemCaseSensitive(doc,"version"),*dsp=cJSON_GetObjectItemCaseSensitive(doc,"effects_version");
    if(!format || strcmp(format,"sonics-fx-chain") || !cJSON_IsNumber(version) || !cJSON_IsNumber(dsp))goto done;
    if(version->valuedouble!=1 || dsp->valuedouble!=1){status=DAW_PRESET_UNSUPPORTED;goto done;}
    const char* id=string(doc,"id"),*revision=string(doc,"revision"),*name=string(doc,"name");
    if(!daw_preset_id_valid(id) || !daw_preset_id_valid(revision) || !daw_preset_name_valid(name))goto done;
    const cJSON* effects=cJSON_GetObjectItemCaseSensitive(doc,"effects");if(!cJSON_IsArray(effects) || cJSON_GetArraySize(effects)>FX_MASTER_MAX)goto done;
    for(const cJSON* effect=effects->child;effect;effect=effect->next){if(!fields(effect,effect_keys))goto done;
        const cJSON* type=cJSON_GetObjectItemCaseSensitive(effect,"type_id"),*enabled=cJSON_GetObjectItemCaseSensitive(effect,"enabled"),*params=cJSON_GetObjectItemCaseSensitive(effect,"parameters");
        if(!cJSON_IsNumber(type) || !isfinite(type->valuedouble) || type->valuedouble<1 || type->valuedouble>UINT32_MAX || type->valuedouble!=floor(type->valuedouble) || !cJSON_IsBool(enabled) || !cJSON_IsArray(params))goto done;
        FxMasterInstanceInfo* item=&candidate.chain.items[candidate.chain.count++];item->type=(FxTypeId)type->valuedouble;item->enabled=cJSON_IsTrue(enabled);
        const EffectParamSpec* specs=NULL;uint32_t count=0;if(!engine_fx_registry_get_param_specs(engine,item->type,&specs,&count) || count>FX_MAX_PARAMS){status=DAW_PRESET_UNSUPPORTED;goto done;}
        if(cJSON_GetArraySize(params)!=(int)count)goto done;item->param_count=count;bool seen[FX_MAX_PARAMS]={0};
        for(const cJSON* param=params->child;param;param=param->next){if(!fields(param,param_keys))goto done;const char* key=string(param,"id");int index=-1;
            for(uint32_t p=0;key && p<count;++p)if(!strcmp(key,specs[p].id))index=p;if(index<0 || seen[index])goto done;seen[index]=true;
            const cJSON* value=cJSON_GetObjectItemCaseSensitive(param,"value"),*mode=cJSON_GetObjectItemCaseSensitive(param,"mode"),*beats=cJSON_GetObjectItemCaseSensitive(param,"beats");
            if(!cJSON_IsNumber(value) || !cJSON_IsNumber(mode) || !cJSON_IsNumber(beats) || !isfinite(value->valuedouble) || !isfinite(beats->valuedouble) || !isfinite(mode->valuedouble) || mode->valuedouble<0 || mode->valuedouble>2 || mode->valuedouble!=floor(mode->valuedouble))goto done;
            if(value->valuedouble<specs[index].min_value || value->valuedouble>specs[index].max_value || ((specs[index].type==FX_PARAM_TYPE_BOOL || specs[index].type==FX_PARAM_TYPE_ENUM || specs[index].type==FX_PARAM_TYPE_INT) && value->valuedouble!=floor(value->valuedouble)))goto done;
            item->params[index]=(float)value->valuedouble;item->param_mode[index]=(FxParamMode)mode->valuedouble;item->param_beats[index]=(float)beats->valuedouble;
        }
    }
    if(!chain_valid(engine,&candidate.chain))goto done;strcpy(candidate.id,id);strcpy(candidate.revision,revision);strcpy(candidate.name,name);*out=candidate;status=DAW_PRESET_OK;
done:cJSON_Delete(doc);return status;
}
// Reads a bounded regular private file without following links or accepting embedded null bytes.
static DawPresetStatus read_file(const Engine* engine,const char* path,DawFxPreset* out) {
    int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return errno==ENOENT?DAW_PRESET_NOT_FOUND:DAW_PRESET_IO;struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&0077) || st.st_size<=0 || st.st_size>131072){close(fd);return DAW_PRESET_INVALID;}
    char* data=malloc((size_t)st.st_size+1);if(!data){close(fd);return DAW_PRESET_IO;}size_t used=0;DawPresetStatus status=DAW_PRESET_IO;
    while(used<(size_t)st.st_size){ssize_t n=read(fd,data+used,(size_t)st.st_size-used);if(n<0 && errno==EINTR)continue;if(n<=0)goto done;used+=(size_t)n;}
    data[used]=0;status=memchr(data,0,used)?DAW_PRESET_INVALID:decode(engine,data,out);
done:free(data);close(fd);return status;
}
// Constructs filenames solely from validated opaque preset identities.
static bool path_for(char path[1024],const char* directory,const char* id) {
    return daw_preset_id_valid(id) && snprintf(path,1024,"%s/%s.sonics-fx.json",directory,id)<1024;
}
// Returns only a current compatible preset and independently checks its optional expected revision.
DawPresetStatus daw_fx_library_get(const Engine* engine,const char* id,const char* revision,DawFxPreset* out) {
    if(!engine || !engine_is_control_thread(engine) || !out || !daw_preset_id_valid(id) || (revision && !daw_preset_id_valid(revision)))return DAW_PRESET_INVALID;
    char directory[1024],path[1024];if(!root(directory,false) || !path_for(path,directory,id))return DAW_PRESET_IO;
    DawFxPreset candidate;DawPresetStatus status=read_file(engine,path,&candidate);if(status!=DAW_PRESET_OK)return status;
    if(strcmp(candidate.id,id))return DAW_PRESET_INVALID;if(revision && strcmp(candidate.revision,revision))return DAW_PRESET_CONFLICT;*out=candidate;return DAW_PRESET_OK;
}
// Orders duplicate labels deterministically without merging independently identified presets.
static int compare(const void* a,const void* b) { const DawFxPreset* x=a,*y=b;int n=strcmp(x->name,y->name);return n?n:strcmp(x->id,y->id); }
// Scans a bounded directory without creating it and reports unsupported or invalid entries separately.
DawPresetStatus daw_fx_library_list(const Engine* engine,DawFxPresetList* out) {
    if(!engine || !engine_is_control_thread(engine) || !out)return DAW_PRESET_INVALID;memset(out,0,sizeof(*out));char directory[1024];if(!root(directory,false))return DAW_PRESET_IO;
    DIR* dir=opendir(directory);if(!dir)return errno==ENOENT?DAW_PRESET_OK:DAW_PRESET_IO;struct dirent* entry;int scanned=0;
    while((entry=readdir(dir))){if(++scanned>4096){out->truncated=true;break;}if(strlen(entry->d_name)!=47 || strcmp(entry->d_name+32,".sonics-fx.json"))continue;
        char id[33];memcpy(id,entry->d_name,32);id[32]=0;DawFxPreset value;if(daw_fx_library_get(engine,id,NULL,&value)!=DAW_PRESET_OK){++out->invalid_count;continue;}
        if(out->count==DAW_FX_LIBRARY_CAPACITY){out->truncated=true;continue;}out->items[out->count++]=value;
    }closedir(dir);qsort(out->items,out->count,sizeof(out->items[0]),compare);return DAW_PRESET_OK;
}
// Generates cryptographically random opaque identities instead of deriving filenames from labels.
static bool new_id(char out[33]) {
    unsigned char bytes[16];int fd=open("/dev/urandom",O_RDONLY);if(fd<0)return false;size_t used=0;
    while(used<sizeof(bytes)){ssize_t n=read(fd,bytes+used,sizeof(bytes)-used);if(n<0 && errno==EINTR)continue;if(n<=0){close(fd);return false;}used+=(size_t)n;}close(fd);
    for(int i=0;i<16;++i)snprintf(out+i*2,3,"%02x",bytes[i]);return true;
}
// Locks cooperating writers and publishes a complete portable document with explicit revision replacement.
DawPresetStatus daw_fx_library_save(const Engine* engine,const char* name,const FxMasterSnapshot* chain,const char* id,const char* revision,DawFxPreset* out,bool* durable) {
    if(durable)*durable=false;if(!out || !daw_preset_name_valid(name) || !chain_valid(engine,chain) || (!!id!=!!revision) || (id && (!daw_preset_id_valid(id) || !daw_preset_id_valid(revision))))return DAW_PRESET_INVALID;
    char directory[1024],lock_path[1024],path[1024];if(!root(directory,true) || snprintf(lock_path,sizeof(lock_path),"%s/.library.lock",directory)>=(int)sizeof(lock_path))return DAW_PRESET_IO;
    int lock=open(lock_path,O_CREAT|O_RDWR|O_NOFOLLOW,0600);if(lock<0)return DAW_PRESET_IO;struct stat st;
    if(fstat(lock,&st) || !S_ISREG(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&0077)){close(lock);return DAW_PRESET_IO;}
    if(flock(lock,LOCK_EX|LOCK_NB)){close(lock);return DAW_PRESET_BUSY;}
    DawPresetStatus status=DAW_PRESET_IO;DawFxPreset preset={.chain=*chain};strcpy(preset.name,name);for(int i=0;i<preset.chain.count;++i)preset.chain.items[i].id=0;
    if(id){DawFxPreset previous;status=daw_fx_library_get(engine,id,revision,&previous);if(status!=DAW_PRESET_OK)goto done;strcpy(preset.id,id);}else if(!new_id(preset.id))goto done;
    status=DAW_PRESET_IO;
    if(!new_id(preset.revision) || !path_for(path,directory,preset.id))goto done;
    if(!id && (!lstat(path,&st) || errno!=ENOENT)){status=DAW_PRESET_CONFLICT;goto done;}
    char* bytes=encode(engine,&preset);DawFxPreset decoded;if(!bytes || decode(engine,bytes,&decoded)!=DAW_PRESET_OK){free(bytes);status=DAW_PRESET_IO;goto done;}
    if(id){char backup[1024];if(snprintf(backup,sizeof(backup),"%s.previous",path)>=(int)sizeof(backup) || daw_save_file_copy(path,backup)==DAW_SAVE_FAILED){free(bytes);status=DAW_PRESET_IO;goto done;}}
    DawSaveResult saved=daw_save_file_write(path,bytes,strlen(bytes));free(bytes);status=saved==DAW_SAVE_FAILED?DAW_PRESET_IO:DAW_PRESET_OK;
    if(status==DAW_PRESET_OK){*out=preset;if(durable)*durable=saved==DAW_SAVE_SYNCED;}
done:flock(lock,LOCK_UN);close(lock);return status;
}
