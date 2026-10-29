#define _XOPEN_SOURCE 700
#include "session/project_collections.h"
#include "app_state.h"
#include "daw/data_paths.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>

// Resolves an existing real directory and refuses bundle-contained project destinations.
static bool directory(const char* path, char* out) {
    char real[4096]; struct stat st;
    if (!path || !realpath(path, real) || strlen(real) >= SESSION_PATH_MAX ||
        stat(real, &st) || !S_ISDIR(st.st_mode) || !daw_data_path_is_safe_write_root(real)) return false;
    SDL_strlcpy(out, real, SESSION_PATH_MAX); return true;
}
// Adds one canonical directory and deduplicates aliases without touching its contents.
static void add(ProjectCollections* c, const char* label, const char* path) {
    char resolved[SESSION_PATH_MAX];
    if (c->count == PROJECT_COLLECTION_MAX || !directory(path, resolved)) return;
    for (int i=0;i<c->count;++i) if (!strcmp(c->items[i].directory,resolved)) return;
    ProjectCollection* item=&c->items[c->count++];
    SDL_strlcpy(item->label,label,sizeof(item->label)); SDL_strlcpy(item->directory,resolved,sizeof(item->directory));
}
// Orders independent folders by their displayed names.
static int compare_collection(const void* a,const void* b) {
    return strcmp(((const ProjectCollection*)a)->label,((const ProjectCollection*)b)->label);
}
// Identifies a project's parent directory independently of its saved display name.
static bool project_parent(const AppState* s,char* out) {
    if (!s->project.path[0]) return false;
    char path[SESSION_PATH_MAX]; SDL_strlcpy(path,s->project.path,sizeof(path));
    char* slash=strrchr(path,'/'); if (!slash) return directory(".",out);
    if (slash==path) slash[1]=0; else *slash=0; return directory(path,out);
}
// Discovers only child collections with projects/; supporting JSONs never become project entries.
bool project_collections_refresh(AppState* s,const char* root) {
    if (!s) return false;
    ProjectCollections next={.initialized=true}; char chosen[SESSION_PATH_MAX]={0};
    ProjectCollections* old=&s->project_collections;
    if (old->initialized && old->selected>=0 && old->selected<old->count)
        SDL_strlcpy(chosen,old->items[old->selected].directory,sizeof(chosen));
    const char* candidate=root ? root : (old->root[0] ? old->root : getenv("DAW_COLLECTIONS_ROOT"));
    if (!candidate || !candidate[0]) candidate="music_projects";
    if (!directory(candidate,next.root) && root) return false;
    char preferred[SESSION_PATH_MAX];
    if (snprintf(preferred,sizeof(preferred),"%s/projects",s->data_paths.output_root[0] ? s->data_paths.output_root : "config")<(int)sizeof(preferred))
        add(&next,"Default projects",preferred);
    add(&next,"Legacy projects","config/projects");
    if(next.root[0]) {
        char own[SESSION_PATH_MAX];const char* name=strrchr(next.root,'/');
        if(snprintf(own,sizeof(own),"%s/projects",next.root)<(int)sizeof(own))add(&next,name ? name+1 : "Collection",own);
    }
    int fixed=next.count;
    DIR* dir=next.root[0] ? opendir(next.root) : NULL;
    if (dir) {
        struct dirent* ent;
        while ((ent=readdir(dir))) {
            if (ent->d_name[0]=='.') continue;
            char path[SESSION_PATH_MAX];
            if (snprintf(path,sizeof(path),"%s/%s/projects",next.root,ent->d_name)<(int)sizeof(path)) add(&next,ent->d_name,path);
        }
        closedir(dir);
    }
    qsort(next.items+fixed,next.count-fixed,sizeof(next.items[0]),compare_collection);
    char current[SESSION_PATH_MAX]={0};
    if (project_parent(s,current)) add(&next,"Current project folder",current);
    next.selected=0;
    for (int i=0;i<next.count;++i) if (!strcmp(next.items[i].directory,chosen[0] ? chosen : current)) {next.selected=i;break;}
    next.revision=old->revision+1; *old=next; return true;
}
// Changes browsing identity only; stale modal presses are invalidated by the revision.
bool project_collections_select(AppState* s,int index) {
    if (!s || index<0 || index>=s->project_collections.count) return false;
    s->project_collections.selected=index; ++s->project_collections.revision; return true;
}
// Orders versioned checkpoints lexically within exactly one folder.
static int compare_project(const void* a,const void* b) { return strcmp(((const ProjectInfo*)a)->name,((const ProjectInfo*)b)->name); }
// Reads only a selected folder's complete regular-file checkpoints; invalid JSON is excluded.
bool project_collections_list(AppState* s,ProjectInfo* items,int capacity,int* count) {
    if (count) *count=0;
    if (!s || !items || capacity<=0) return false;
    ProjectCollections* c=&s->project_collections;
    if (!c->initialized && !project_collections_refresh(s,NULL)) return false;
    if (!c->count || c->selected<0 || c->selected>=c->count) return false;
    DIR* dir=opendir(c->items[c->selected].directory); if (!dir) return false;
    int n=0;struct dirent* ent;
    while ((ent=readdir(dir))) {
        const char* dot=strrchr(ent->d_name,'.'); if (ent->d_name[0]=='.' || !dot || strcmp(dot,".json")) continue;
        char path[SESSION_PATH_MAX]; struct stat st;
        if (snprintf(path,sizeof(path),"%s/%s",c->items[c->selected].directory,ent->d_name)>=(int)sizeof(path) ||
            lstat(path,&st) || !S_ISREG(st.st_mode)) continue;
        SessionDocument doc;session_document_init(&doc);
        bool valid=session_document_read_file(path,&doc);session_document_free(&doc);if (!valid) continue;
        ProjectInfo info;if (!project_manager_get_info(path,&info)) continue;
        if (n<capacity) items[n++]=info;
        else { int last=0;for(int i=1;i<n;++i)if(compare_project(&items[last],&items[i])<0)last=i;
            if(compare_project(&info,&items[last])<0)items[last]=info; }
    }
    closedir(dir);qsort(items,n,sizeof(*items),compare_project);if(count)*count=n;return true;
}
// Splits an optional _vNNN token while preserving descriptive suffixes such as _ThemeStudy.
static void split_name(const char* name,char* prefix,char* suffix) {
    char clean[SESSION_NAME_MAX];size_t n=0;
    for (const unsigned char* p=(const unsigned char*)(name && name[0] ? name : "project");*p && n+1<sizeof(clean);++p)
        clean[n++]=(isalnum(*p)||*p=='_'||*p=='-') ? *p : '_';
    clean[n]=0;SDL_strlcpy(prefix,clean,SESSION_NAME_MAX);suffix[0]=0;
    for(char* p=clean;*p;++p) if(p[0]=='_' && p[1]=='v' && isdigit((unsigned char)p[2])) {
        char* end=p+2;while(isdigit((unsigned char)*end))++end;
        if(!*end || *end=='_') {SDL_strlcpy(suffix,end,SESSION_NAME_MAX);*p=0;SDL_strlcpy(prefix,clean,SESSION_NAME_MAX);break;}
    }
}
// Scans matching checkpoint names and never fills an older gap or replaces an existing version.
bool project_collections_next_path(AppState* s,const char* name,char* path,size_t size) {
    if (!s || !path || !size) return false;
    ProjectCollections* c=&s->project_collections;
    if (!c->initialized && !project_collections_refresh(s,NULL)) return false;
    if (!c->count || c->selected<0 || c->selected>=c->count) return false;
    char resolved[SESSION_PATH_MAX];if(!directory(c->items[c->selected].directory,resolved) || strcmp(resolved,c->items[c->selected].directory))return false;
    char prefix[SESSION_NAME_MAX],suffix[SESSION_NAME_MAX];split_name(name,prefix,suffix);
    unsigned high=0;DIR* dir=opendir(resolved);if(!dir)return false;struct dirent* ent;
    while((ent=readdir(dir))) {
        size_t len=strlen(prefix);if(strncmp(ent->d_name,prefix,len) || strncmp(ent->d_name+len,"_v",2))continue;
        char* end;unsigned long v=strtoul(ent->d_name+len+2,&end,10);
        char tail[SESSION_NAME_MAX+8];snprintf(tail,sizeof(tail),"%s.json",suffix);
        if(end==ent->d_name+len+2 || strcmp(end,tail))continue;
        if(v>=999999){closedir(dir);return false;}if(v>high)high=(unsigned)v;
    }
    closedir(dir);
    if(strlen(prefix)+strlen(suffix)+12>=SESSION_NAME_MAX)return false;
    return snprintf(path,size,"%s/%s_v%03u%s.json",resolved,prefix,high+1,suffix)<(int)size;
}
// Publishes a new validated session without clobbering another process's concurrent checkpoint.
bool project_collections_save_version(AppState* s,const char* name) {
    char path[SESSION_PATH_MAX];if(!project_collections_next_path(s,name,path,sizeof(path)))return false;
    SessionDocument doc;session_document_init(&doc);
    bool ok=session_document_capture(s,&doc) && session_document_write_file_new(&doc,path);session_document_free(&doc);
    if(!ok)return false;
    SDL_strlcpy(s->project.path,path,sizeof(s->project.path));
    const char* base=strrchr(path,'/');SDL_strlcpy(s->project.name,base+1,sizeof(s->project.name));
    char* dot=strrchr(s->project.name,'.');if(dot)*dot=0;s->project.has_name=true;
    if(!project_manager_remember_last(s,path))SDL_Log("Collection version saved; startup marker update failed: %s",path);
    return true;
}
