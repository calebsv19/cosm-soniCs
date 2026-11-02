#define _POSIX_C_SOURCE 200809L
#include "session.h"
#include "daw/data_paths.h"
#include "audio/media_registry.h"
#include "ui/shared_theme_font_adapter.h"
#include <sys/stat.h>
#include "test_temp_path.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// Replaces one token in a generated fixture, leaving all unrelated valid document fields intact.
static void mutate(const char* path,const char* needle,const char* replacement) {
    FILE* f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    char* text=calloc(1,(size_t)n+1);assert(text&&fread(text,1,(size_t)n,f)==(size_t)n);fclose(f);
    char* at=strstr(text,needle);assert(at);f=fopen(path,"wb");assert(f);
    fwrite(text,1,(size_t)(at-text),f);fputs(replacement,f);fputs(at+strlen(needle),f);assert(!fclose(f));free(text);
}
// Proves exact frame recall and all-or-nothing rejection across parser and preference boundaries.
int main(void) {
    DAW_TEST_TEMP_PATH(root,"daw-l1-XXXXXX");assert(mkdtemp(root));char path[1024];snprintf(path,sizeof(path),"%s/project.json",root);
    SessionDocument source,accepted;session_document_init(&source);session_document_init(&accepted);
    source.transport_frame=UINT64_C(9007199254740993);source.timeline.playhead_frame=source.transport_frame;
    source.tempo.bpm=123.456789f;assert(session_document_write_file(&source,path));assert(session_document_read_file(path,&accepted));
    assert(accepted.transport_frame==source.transport_frame&&accepted.timeline.playhead_frame==source.timeline.playhead_frame&&accepted.tempo.bpm==source.tempo.bpm);
    SessionDocument before=accepted;
    const char* needles[]={"\"version\":", "\"version\":", "\"version\": 26", "\"transport_frame\": 9007199254740993", "\"sample_rate\": 48000"};
    const char* replacements[]={"\"unknown_semantic\":1,\"version\":", "\"version\":26,\"version\":", "\"version\": 999", "\"transport_frame\": 18446744073709551616", "\"sample_rate\": 1e999"};
    for(size_t i=0;i<sizeof(needles)/sizeof(*needles);++i) {
        assert(session_document_write_file(&source,path));mutate(path,needles[i],replacements[i]);
        assert(!session_document_read_file(path,&accepted));assert(!memcmp(&before,&accepted,sizeof(before)));
    }
    const char* malformed[]={"{\"version\":26,}","{\"version\":026}","{\"version\":26 \"engine\":{}}","{\"version\":26,\"engine\":{\"sample_rate\":1e999}}","{\"version\":26,\"tempo\":{\"bpm\":1e100}}","{\"version\":26,\"engine\":{\"block_size\":2147483647}}"};
    for(size_t i=0;i<sizeof(malformed)/sizeof(*malformed);++i){FILE* f=fopen(path,"wb");assert(f);fputs(malformed[i],f);fclose(f);assert(!session_document_read_file(path,&accepted));assert(!memcmp(&before,&accepted,sizeof(before)));}
    DawDataPaths roots,prior;daw_data_paths_set_defaults(&roots);prior=roots;
    FILE* f=fopen(path,"wb");assert(f);fputs("input_root=/candidate\ninput_root=/duplicate\n",f);fclose(f);
    assert(!daw_data_paths_load_file(path,&roots));assert(!memcmp(&prior,&roots,sizeof(roots)));
    MediaRegistry registry;media_registry_init(&registry,path);registry.entries=calloc(1,sizeof(*registry.entries));assert(registry.entries);
    registry.count=registry.capacity=1;strcpy(registry.entries[0].id,"asset_1");strcpy(registry.entries[0].path,"quoted \" media.wav");strcpy(registry.entries[0].name,"音\nSound");
    registry.entries[0].file_size=UINT64_C(9007199254740991);registry.dirty=true;assert(media_registry_save(&registry));
    assert(media_registry_load(&registry));assert(!strcmp(registry.entries[0].name,"音\nSound"));assert(registry.entries[0].file_size==UINT64_C(9007199254740991));
    MediaRegistry before_registry=registry;f=fopen(path,"wb");assert(f);fputs("{\"type\":\"wrong_kind\",\"entries\":[]}",f);fclose(f);
    assert(!media_registry_load(&registry));assert(!memcmp(&before_registry,&registry,sizeof(registry)));free(registry.entries);
    char copied[1024];snprintf(copied,sizeof(copied),"%s/copied.wav",root);
    unsigned char payload[256],recalled[256];for(int i=0;i<256;++i)payload[i]=(unsigned char)i;
    FILE* copy_source=fopen(path,"wb");assert(copy_source);assert(fwrite(payload,1,sizeof(payload),copy_source)==sizeof(payload));assert(!fclose(copy_source));
    assert(daw_save_file_copy_new(path,copied)!=DAW_SAVE_FAILED);
    assert(daw_save_file_copy_new(path,copied)==DAW_SAVE_FAILED);
    assert(!unlink(path));FILE* independent=fopen(copied,"rb");assert(independent);assert(fread(recalled,1,sizeof(recalled),independent)==sizeof(recalled) && !memcmp(payload,recalled,sizeof(payload)) && fgetc(independent)==EOF);assert(!fclose(independent));assert(!unlink(copied));
    char original_cwd[4096];assert(getcwd(original_cwd,sizeof(original_cwd)));assert(!chdir(root));assert(!mkdir("config",0700));
    const char* prefs[]={"config/font_preset.txt","config/theme_preset.txt","config/font_zoom_step.txt"};
    bool (*save_prefs[])(void)={daw_shared_font_save_persisted,daw_shared_theme_save_persisted,daw_shared_font_zoom_save_persisted};
    for(int i=0;i<3;++i) {
        FILE* malformed_pref=fopen(prefs[i],"wb");assert(malformed_pref);fputs("unsupported\ntrailing\n",malformed_pref);fclose(malformed_pref);
        assert(!save_prefs[i]());malformed_pref=fopen(prefs[i],"rb");assert(malformed_pref && fgetc(malformed_pref)=='u');fclose(malformed_pref);
        assert(!unlink(prefs[i]));assert(save_prefs[i]());assert(!unlink(prefs[i]));
    }
    assert(!rmdir("config"));assert(!chdir(original_cwd));
    session_document_free(&accepted);session_document_free(&source);
    char backup[1030];snprintf(backup,sizeof(backup),"%s.bak",path);unlink(path);unlink(backup);assert(!rmdir(root));
    puts("l1_persistence_test passed (exact frames, malformed/future/duplicate/unknown admission, retained candidate and roots)");return 0;
}
