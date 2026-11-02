#include "audio/media_registry.h"
#include "daw/save_file.h"
#include "cJSON.h"
#include "daw/json_admission.h"
#include <math.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEDIA_REGISTRY_MAGIC "media_registry"

static uint64_t fnv1a64_stream(FILE* fp) {
    const uint64_t prime = 1099511628211ULL;
    uint64_t hash = 1469598103934665603ULL;
    unsigned char buffer[4096];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        for (size_t i = 0; i < read; ++i) {
            hash ^= (uint64_t)buffer[i];
            hash *= prime;
        }
    }
    return hash;
}

static bool media_registry_hash_file(const char* path, char* out_id, size_t out_len, uint64_t* out_size) {
    if (!path || !out_id || out_len == 0) {
        return false;
    }
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    if (fseek(fp, 0, SEEK_END)) { fclose(fp); return false; }
    long size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET)) { fclose(fp); return false; }
    uint64_t hash = fnv1a64_stream(fp);
    bool ok=!ferror(fp);if(fclose(fp))ok=false;if(!ok)return false;
    snprintf(out_id, out_len, "%016llx", (unsigned long long)hash);
    if (out_size) {
        *out_size = (uint64_t)size;
    }
    return true;
}

static bool registry_ensure_capacity(MediaRegistry* registry, int count_needed) {
    if (!registry) return false;
    if (registry->capacity >= count_needed) return true;
    int next = registry->capacity > 0 ? registry->capacity : 32;
    while (next < count_needed) next *= 2;
    MediaRegistryEntry* resized = (MediaRegistryEntry*)realloc(registry->entries, sizeof(MediaRegistryEntry) * (size_t)next);
    if (!resized) return false;
    registry->entries = resized;
    registry->capacity = next;
    return true;
}

// Serializes escaped names and exact metadata numbers using the already-adopted JSON owner.
static void registry_write_json(FILE* file,const MediaRegistry* registry) {
    cJSON* root=cJSON_CreateObject();cJSON* entries=root?cJSON_AddArrayToObject(root,"entries"):NULL;
    bool ok=root && entries && cJSON_AddStringToObject(root,"type",MEDIA_REGISTRY_MAGIC);
    for(int i=0;ok && i<registry->count;++i) {
        const MediaRegistryEntry* e=&registry->entries[i];cJSON* item=cJSON_CreateObject();
        if(!item){ok=false;break;}cJSON_AddItemToArray(entries,item);
        char size_token[32];snprintf(size_token,sizeof(size_token),"%llu",(unsigned long long)e->file_size);
        ok=cJSON_AddStringToObject(item,"id",e->id)&&cJSON_AddStringToObject(item,"path",e->path)&&cJSON_AddStringToObject(item,"name",e->name)&&
            cJSON_AddNumberToObject(item,"duration",e->duration_seconds)&&cJSON_AddNumberToObject(item,"sample_rate",e->sample_rate)&&
            cJSON_AddNumberToObject(item,"channels",e->channels)&&e->file_size<=UINT64_C(9007199254740991)&&cJSON_AddRawToObject(item,"file_size",size_token);
    }
    char* bytes=ok?cJSON_PrintUnformatted(root):NULL;
    if(bytes){fputs(bytes,file);free(bytes);}cJSON_Delete(root);
    /* Empty/failed serialization is refused by the validating save caller. */
}
// Rejects unsupported and repeated semantic members in registry documents.
static bool registry_fields(const cJSON* object,const char* const* keys) {
    if(!cJSON_IsObject(object))return false;
    for(const cJSON* a=object->child;a;a=a->next) {
        bool known=false;for(int i=0;keys[i];++i)if(!strcmp(a->string,keys[i]))known=true;
        if(!known)return false;for(const cJSON* b=a->next;b;b=b->next)if(!strcmp(a->string,b->string))return false;
    }return true;
}
// Copies a complete bounded JSON string without accepting truncation.
static bool registry_string(const cJSON* object,const char* key,char* out,size_t capacity) {
    const cJSON* v=cJSON_GetObjectItemCaseSensitive(object,key);
    if(!cJSON_IsString(v)||strlen(v->valuestring)>=capacity)return false;strcpy(out,v->valuestring);return true;
}
// Reads finite bounded metadata with exact integral values where required.
static bool registry_number(const cJSON* item,const char* key,double maximum,bool integer,double* out) {
    const cJSON* v=cJSON_GetObjectItemCaseSensitive(item,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>maximum||(integer&&floor(v->valuedouble)!=v->valuedouble))return false;
    *out=v->valuedouble;return true;
}
// Parses a complete registry candidate; no partially read entries reach the live registry.
static bool registry_parse_json(MediaRegistry* registry,const char* data,size_t len) {
    if(!daw_json_structure_valid(data,len,"type"))return false;
    const char* end=NULL;cJSON* root=cJSON_ParseWithLengthOpts(data,len+1,&end,1);bool ok=false;
    const char* root_keys[]={"type","entries",NULL};const char* entry_keys[]={"id","path","name","duration","sample_rate","channels","file_size",NULL};
    char type[32];
    if(!root || !registry_fields(root,root_keys)||!registry_string(root,"type",type,sizeof(type))||strcmp(type,MEDIA_REGISTRY_MAGIC))goto done;
    const cJSON* entries=cJSON_GetObjectItemCaseSensitive(root,"entries");if(!cJSON_IsArray(entries)||cJSON_GetArraySize(entries)>65536)goto done;
    for(const cJSON* item=entries->child;item;item=item->next) {
        MediaRegistryEntry e={0};double duration,rate,channels,size;
        if(!registry_fields(item,entry_keys)||!registry_string(item,"id",e.id,sizeof(e.id))||!e.id[0]||
           !registry_string(item,"path",e.path,sizeof(e.path))||!e.path[0]||!registry_string(item,"name",e.name,sizeof(e.name))||
           !registry_number(item,"duration",3.402823466e38,false,&duration)||!registry_number(item,"sample_rate",INT_MAX,true,&rate)||
           !registry_number(item,"channels",INT_MAX,true,&channels)||!registry_number(item,"file_size",9007199254740991.0,true,&size))goto done;
        e.duration_seconds=(float)duration;e.sample_rate=(int)rate;e.channels=(int)channels;e.file_size=(uint64_t)size;
        for(int i=0;i<registry->count;++i)if(!strcmp(registry->entries[i].id,e.id))goto done;
        if(!registry_ensure_capacity(registry,registry->count+1))goto done;registry->entries[registry->count++]=e;
    }ok=true;
done:cJSON_Delete(root);return ok;
}

void media_registry_init(MediaRegistry* registry, const char* manifest_path) {
    if (!registry) return;
    memset(registry, 0, sizeof(*registry));
    if (manifest_path) {
        strncpy(registry->manifest_path, manifest_path, sizeof(registry->manifest_path) - 1);
        registry->manifest_path[sizeof(registry->manifest_path) - 1] = '\0';
    }
}

void media_registry_shutdown(MediaRegistry* registry) {
    if (!registry) return;
    if (registry->dirty) {
        media_registry_save(registry);
    }
    free(registry->entries);
    registry->entries = NULL;
    registry->count = 0;
    registry->capacity = 0;
}

bool media_registry_load(MediaRegistry* registry) {
    if (!registry || registry->manifest_path[0] == '\0') return false;
    FILE* file = fopen(registry->manifest_path, "rb");
    if (!file) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size <= 0 || size>16*1024*1024) {
        fclose(file);
        return false;
    }
    fseek(file, 0, SEEK_SET);
    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return false;
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    bool io_ok=!ferror(file);if(fclose(file))io_ok=false;
    buffer[read] = '\0';
    MediaRegistry candidate;media_registry_init(&candidate,registry->manifest_path);
    bool ok=io_ok && read==(size_t)size && registry_parse_json(&candidate,buffer,read);
    free(buffer);
    if(!ok){free(candidate.entries);return false;}
    free(registry->entries);*registry=candidate;return true;
}

// Clears dirty state only after a complete manifest has been published.
bool media_registry_save(MediaRegistry* registry) {
    if (!registry || registry->manifest_path[0] == '\0') return false;
    DawSaveFile save;
    if (!daw_save_file_begin(&save, registry->manifest_path)) return false;
    registry_write_json(save.file, registry);
    if (!daw_save_file_prepare(&save)) {
        daw_save_file_abort(&save);
        return false;
    }
    MediaRegistry verified;media_registry_init(&verified,save.temporary);
    if(!media_registry_load(&verified)){free(verified.entries);daw_save_file_abort(&save);return false;}
    free(verified.entries);
    if (daw_save_file_commit(&save) == DAW_SAVE_FAILED) return false;
    registry->dirty = false;
    return true;
}

const MediaRegistryEntry* media_registry_find_by_id(const MediaRegistry* registry, const char* id) {
    if (!registry || !id || id[0] == '\0') return NULL;
    for (int i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].id, id) == 0) {
            return &registry->entries[i];
        }
    }
    return NULL;
}

MediaRegistryEntry* media_registry_find_by_path(MediaRegistry* registry, const char* path) {
    if (!registry || !path || path[0] == '\0') return NULL;
    for (int i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].path, path) == 0) {
            return &registry->entries[i];
        }
    }
    return NULL;
}

bool media_registry_ensure_for_path(MediaRegistry* registry,
                                    const char* path,
                                    const char* display_name,
                                    MediaRegistryEntry* out_entry) {
    if (!registry || !path) return false;
    MediaRegistryEntry* existing = media_registry_find_by_path(registry, path);
    if (existing) {
        if (display_name && display_name[0] != '\0' && strcmp(existing->name, display_name) != 0) {
            strncpy(existing->name, display_name, sizeof(existing->name) - 1);
            existing->name[sizeof(existing->name) - 1] = '\0';
            registry->dirty = true;
        }
        if (out_entry) {
            *out_entry = *existing;
        }
        return true;
    }
    MediaRegistryEntry entry = {0};
    if (!media_registry_hash_file(path, entry.id, sizeof(entry.id), &entry.file_size)) {
        return false;
    }
    MediaRegistryEntry* by_id = NULL;
    if (entry.id[0] != '\0') {
        for (int i = 0; i < registry->count; ++i) {
            if (strcmp(registry->entries[i].id, entry.id) == 0) {
                by_id = &registry->entries[i];
                break;
            }
        }
    }
    if (by_id) {
        strncpy(by_id->path, path, sizeof(by_id->path) - 1);
        by_id->path[sizeof(by_id->path) - 1] = '\0';
        if (display_name && display_name[0] != '\0') {
            strncpy(by_id->name, display_name, sizeof(by_id->name) - 1);
            by_id->name[sizeof(by_id->name) - 1] = '\0';
        }
        registry->dirty = true;
        if (out_entry) {
            *out_entry = *by_id;
        }
        return true;
    }
    strncpy(entry.path, path, sizeof(entry.path) - 1);
    entry.path[sizeof(entry.path) - 1] = '\0';
    if (display_name) {
        strncpy(entry.name, display_name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
    }
    if (!registry_ensure_capacity(registry, registry->count + 1)) {
        return false;
    }
    registry->entries[registry->count++] = entry;
    registry->dirty = true;
    if (out_entry) {
        *out_entry = entry;
    }
    return true;
}

bool media_registry_update_path(MediaRegistry* registry,
                                const char* id,
                                const char* new_path,
                                const char* new_name) {
    if (!registry || !id || !new_path) return false;
    for (int i = 0; i < registry->count; ++i) {
        MediaRegistryEntry* entry = &registry->entries[i];
        if (strcmp(entry->id, id) == 0) {
            strncpy(entry->path, new_path, sizeof(entry->path) - 1);
            entry->path[sizeof(entry->path) - 1] = '\0';
            if (new_name && new_name[0] != '\0') {
                strncpy(entry->name, new_name, sizeof(entry->name) - 1);
                entry->name[sizeof(entry->name) - 1] = '\0';
            }
            registry->dirty = true;
            return true;
        }
    }
    return false;
}

// Hashes bounded chunks with cancellation and explicit read-error rejection for background preparation.
bool media_registry_prepare_entry(const char* path, MediaRegistryEntry* out, bool (*cancelled)(void*), void* user) {
    if (!path || !out || strlen(path)>=SESSION_PATH_MAX || (cancelled && cancelled(user))) return false;
    FILE* file=fopen(path,"rb"); if (!file) return false;
    unsigned char buffer[16384]; size_t count; uint64_t hash=UINT64_C(1469598103934665603), size=0;
    while ((count=fread(buffer,1,sizeof(buffer),file))>0) {
        if (cancelled && cancelled(user)) { fclose(file); return false; }
        size+=count;
        for (size_t i=0;i<count;++i) { hash^=buffer[i]; hash*=UINT64_C(1099511628211); }
    }
    bool ok=!ferror(file) && !(cancelled && cancelled(user)); fclose(file); if (!ok) return false;
    *out=(MediaRegistryEntry){.file_size=size};
    snprintf(out->id,sizeof(out->id),"%016llx",(unsigned long long)hash);
    snprintf(out->path,sizeof(out->path),"%s",path);
    const char* name=strrchr(path,'/');snprintf(out->name,sizeof(out->name),"%s",name ? name+1 : path);
    return true;
}

// Commits a privately prepared registry record while keeping earlier content IDs available to old clips.
bool media_registry_adopt_entry(MediaRegistry* registry, const MediaRegistryEntry* entry) {
    if (!registry || !entry || !entry->id[0] || !entry->path[0] ||
        !memchr(entry->id,0,sizeof(entry->id)) || !memchr(entry->path,0,sizeof(entry->path))) return false;
    for (int i=0;i<registry->count;++i) if (!strcmp(registry->entries[i].id,entry->id)) {
        registry->entries[i]=*entry;registry->dirty=true;return true;
    }
    if (!registry_ensure_capacity(registry,registry->count+1)) return false;
    registry->entries[registry->count++]=*entry;registry->dirty=true;return true;
}
