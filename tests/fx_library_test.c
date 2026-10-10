#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "daw/fx_library.h"
#include "daw/save_file.h"
#include "cJSON.h"
#include "test_assert.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(v,m) daw_test_expect("fx_library_test",(v),(m))
// Removes only this test's fresh private filesystem outputs after all consumers have closed.
static void cleanup(const char* root) {
    DIR* dir=opendir(root);CHECK(dir!=NULL,"cleanup dir");struct dirent* entry;
    while((entry=readdir(dir))){if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;char path[2048];snprintf(path,sizeof(path),"%s/%s",root,entry->d_name);struct stat st;CHECK(!lstat(path,&st),"stat");if(S_ISDIR(st.st_mode))cleanup(path);else CHECK(!unlink(path),"unlink");}
    closedir(dir);CHECK(!rmdir(root),"rmdir");
}
// Reads a complete independently parsed portable fixture without using the library decoder.
static char* bytes(const char* path) {
    FILE* f=fopen(path,"rb");CHECK(f!=NULL,"open JSON");CHECK(!fseek(f,0,SEEK_END),"seek");long size=ftell(f);rewind(f);char* out=calloc(size+1,1);CHECK(out && fread(out,1,size,f)==(size_t)size && !fclose(f),"read JSON");return out;
}
// Exercises versioned storage, explicit revision updates, portable identity and tempo-aware application.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER),"SDL");char temporary[]="/tmp/daw-fx-library-XXXXXX";CHECK(mkdtemp(temporary)!=NULL,"private root");char root[1024];snprintf(root,sizeof(root),"%s/library",temporary);CHECK(!setenv("DAW_PRESET_LIBRARY",root,1),"private library");
    AppState* state=calloc(1,sizeof(*state));EngineRuntimeConfig config;config_set_defaults(&config);state->engine=engine_create(&config);CHECK(state->engine!=NULL,"engine");undo_manager_init(&state->undo);state->tempo=tempo_state_default(config.sample_rate);
    DawFxPresetList* list=calloc(1,sizeof(*list));CHECK(list && daw_fx_library_list(state->engine,list)==DAW_PRESET_OK && list->count==0 && access(root,F_OK)!=0,"read does not create storage");
    uint64_t track=engine_get_tracks(state->engine)[0].runtime_id;DawFxSnapshot before,after;CHECK(daw_action_get_fx(state,engine_instance_id(state->engine),track,&before)==DAW_ACTION_UNCHANGED,"source chain");
    EngineFxChainRequest edit={.op=ENGINE_FX_CHAIN_ADD,.type=1};CHECK(daw_action_edit_fx(state,&before,&edit,&after)==DAW_ACTION_APPLIED,"gain");before=after;
    edit=(EngineFxChainRequest){.op=ENGINE_FX_CHAIN_PARAM,.id=before.chain.items[0].id,.parameter=0,.value=-6};CHECK(daw_action_edit_fx(state,&before,&edit,&after)==DAW_ACTION_APPLIED,"gain value");before=after;
    edit=(EngineFxChainRequest){.op=ENGINE_FX_CHAIN_ADD,.type=50};CHECK(daw_action_edit_fx(state,&before,&edit,&after)==DAW_ACTION_APPLIED,"delay");before=after;
    const EffectParamSpec* specs=NULL;uint32_t count=0;CHECK(engine_fx_registry_get_param_specs(state->engine,50,&specs,&count),"specs");int time=-1;for(uint32_t p=0;p<count;++p)if((specs[p].flags&FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE) && specs[p].unit==FX_PARAM_UNIT_MS){time=p;break;}CHECK(time>=0,"time param");
    edit=(EngineFxChainRequest){.op=ENGINE_FX_CHAIN_PARAM,.id=before.chain.items[1].id,.parameter=(uint32_t)time,.mode=FX_PARAM_MODE_BEATS,.beats=.5};CHECK(daw_action_edit_fx(state,&before,&edit,&after)==DAW_ACTION_APPLIED,"half-beat delay");before=after;
    DawFxPreset saved,loaded;bool durable=false;CHECK(daw_fx_library_save(state->engine,"Warm Echo",&before.chain,NULL,NULL,&saved,&durable)==DAW_PRESET_OK && durable,"save portable");
    CHECK(daw_fx_library_get(state->engine,saved.id,saved.revision,&loaded)==DAW_PRESET_OK && loaded.chain.count==2,"read full chain");
    CHECK(!loaded.chain.items[0].id && !loaded.chain.items[1].id && loaded.chain.items[0].params[0]==-6 && loaded.chain.items[1].params[time]==250 && loaded.chain.items[1].param_beats[time]==.5,"embedded settings without runtime IDs");
    CHECK(daw_fx_library_list(state->engine,list)==DAW_PRESET_OK && list->count==1 && !strcmp(list->items[0].name,"Warm Echo"),"list saved");
    state->tempo.bpm=60;DawFxSnapshot master;CHECK(daw_action_get_fx(state,engine_instance_id(state->engine),0,&master)==DAW_ACTION_UNCHANGED,"master");edit=(EngineFxChainRequest){.op=ENGINE_FX_CHAIN_REPLACE,.restore=&loaded.chain};
    CHECK(daw_action_edit_fx(state,&master,&edit,&after)==DAW_ACTION_APPLIED && after.chain.items[1].params[time]==500 && after.chain.items[1].param_beats[time]==.5,"recall at destination tempo");
    CHECK(after.chain.items[0].id!=before.chain.items[0].id && after.chain.items[1].id!=before.chain.items[1].id,"independent master IDs");
    CHECK(undo_manager_undo(&state->undo,state),"recall undo");CHECK(daw_action_get_fx(state,master.generation,0,&after)==DAW_ACTION_UNCHANGED && daw_fx_snapshot_equal(&master,&after),"empty master restored");
    DawFxPreset updated;CHECK(daw_fx_library_save(state->engine,"Warm Echo v2",&loaded.chain,saved.id,saved.revision,&updated,&durable)==DAW_PRESET_OK && strcmp(updated.revision,saved.revision),"revision update");
    CHECK(daw_fx_library_save(state->engine,"Stale",&loaded.chain,saved.id,saved.revision,&loaded,&durable)==DAW_PRESET_CONFLICT && !durable,"stale update refused");
    char path[2048],backup[2048],lock_path[2048];snprintf(path,sizeof(path),"%s/FxChains/%s.sonics-fx.json",root,saved.id);snprintf(backup,sizeof(backup),"%s.previous",path);CHECK(!access(backup,F_OK),"previous revision retained");char* original=bytes(path);cJSON* doc=cJSON_Parse(original);CHECK(doc!=NULL,"independent JSON");
    const cJSON* first=cJSON_GetArrayItem(cJSON_GetObjectItem(doc,"effects"),0);CHECK(!cJSON_GetObjectItem(first,"id"),"document runtime ID omitted");
    cJSON_ReplaceItemInObject(doc,"version",cJSON_CreateNumber(2));char* future=cJSON_PrintUnformatted(doc);CHECK(daw_save_file_write(path,future,strlen(future))!=DAW_SAVE_FAILED,"future fixture");CHECK(daw_fx_library_get(state->engine,saved.id,NULL,&loaded)==DAW_PRESET_UNSUPPORTED,"future schema refusal");free(future);cJSON_Delete(doc);
    doc=cJSON_Parse(original);cJSON* parameters=cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(doc,"effects"),0),"parameters");cJSON_AddItemToArray(parameters,cJSON_Duplicate(parameters->child,true));char* duplicate=cJSON_PrintUnformatted(doc);CHECK(daw_save_file_write(path,duplicate,strlen(duplicate))!=DAW_SAVE_FAILED,"duplicate fixture");CHECK(daw_fx_library_get(state->engine,saved.id,NULL,&loaded)==DAW_PRESET_INVALID,"duplicate setting refusal");free(duplicate);cJSON_Delete(doc);CHECK(daw_save_file_write(path,original,strlen(original))!=DAW_SAVE_FAILED,"restore fixture");
    snprintf(lock_path,sizeof(lock_path),"%s/FxChains/.library.lock",root);int lock=open(lock_path,O_RDWR);CHECK(lock>=0 && !flock(lock,LOCK_EX|LOCK_NB),"lock fixture");CHECK(daw_fx_library_save(state->engine,"Busy",&updated.chain,updated.id,updated.revision,&loaded,&durable)==DAW_PRESET_BUSY,"writer contention");flock(lock,LOCK_UN);close(lock);
    char* final=bytes(path);CHECK(!strcmp(final,original),"refusals preserve file bytes");free(final);free(original);
    CHECK(!unlink(path) && !symlink(backup,path),"symlink fixture");CHECK(daw_fx_library_get(state->engine,saved.id,NULL,&loaded)==DAW_PRESET_IO,"symlink refused");
    CHECK(daw_fx_library_list(state->engine,list)==DAW_PRESET_OK && list->invalid_count==1 && list->count==0,"invalid library diagnostics");
    const FxRegistryEntry* entries=NULL;int catalog_count=0;CHECK(engine_fx_get_registry(state->engine,&entries,&catalog_count) && catalog_count==54,"complete catalog");FxTypeId types[54];for(int i=0;i<catalog_count;++i)types[i]=entries[i].id;
    int incompatible=0;
    for(int i=0;i<catalog_count;++i){FxInstId effect=engine_fx_master_add(state->engine,types[i]);CHECK(effect!=0,"catalog effect creation");FxMasterSnapshot one;CHECK(engine_fx_master_snapshot(state->engine,&one) && one.count==1,"catalog one effect");
        DawFxPreset value;DawPresetStatus saved_status=daw_fx_library_save(state->engine,"Catalog Compatibility",&one,NULL,NULL,&value,&durable);if(saved_status!=DAW_PRESET_OK)fprintf(stderr,"catalog type %u save status %d params %u\n",types[i],saved_status,one.items[0].param_count);if(saved_status!=DAW_PRESET_OK){++incompatible;const EffectParamSpec* catalog_specs=NULL;uint32_t n=0;engine_fx_registry_get_param_specs(state->engine,types[i],&catalog_specs,&n);for(uint32_t p=0;p<n && p<one.items[0].param_count;++p)fprintf(stderr,"  %s range %g..%g default %g actual %g\n",catalog_specs[p].id,catalog_specs[p].min_value,catalog_specs[p].max_value,catalog_specs[p].default_value,one.items[0].params[p]);CHECK(engine_fx_master_remove(state->engine,effect),"catalog teardown");continue;}CHECK(daw_fx_library_get(state->engine,value.id,value.revision,&loaded)==DAW_PRESET_OK,"catalog chain recall");
        DawFxSnapshot original_settings={.chain=one},read_settings={.chain=loaded.chain};original_settings.chain.items[0].id=0;CHECK(daw_fx_snapshot_equal(&original_settings,&read_settings),"catalog exact complete settings");CHECK(engine_fx_master_remove(state->engine,effect),"catalog teardown");
    }
    CHECK(!incompatible,"all catalog types compatible");
    free(list);undo_manager_free(&state->undo);engine_destroy(state->engine);free(state);cleanup(temporary);unsetenv("DAW_PRESET_LIBRARY");SDL_Quit();
    puts("fx_library_test: success (complete portable settings, no-create reads, random identities, tempo recall, one-step history, revisions/backup, strict schema, lock and symlink refusals)");return 0;
}
