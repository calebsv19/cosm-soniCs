#include "session_io_read_internal.h"
#include <string.h>
#include "daw/json_admission.h"

// Validates bounded grammar and duplicate keys without converting authoritative integer tokens.
static bool value(JsonReader* r,unsigned depth,const char* required,unsigned* remaining) {
    if(depth>32 || !(*remaining)--)return false;
    json_skip_whitespace(r);if(r->pos==r->length)return false;
    char ch=r->data[r->pos];
    if(ch=='{' || ch=='[') {
        bool object=ch=='{';char end=object?'}':']';++r->pos;
        char keys[128][128];unsigned count=0;bool version_seen=false;
        json_skip_whitespace(r);if(r->pos<r->length && r->data[r->pos]==end){++r->pos;return depth!=0;}
        for(;;) {
            if(++count>(object?128u:100000u))return false;
            if(object) {
                if(!json_parse_string(r,keys[count-1],sizeof(keys[0])))return false;
                for(unsigned i=0;i+1<count;++i)if(!strcmp(keys[i],keys[count-1]))return false;
                if(!json_expect(r,':'))return false;
                if(depth==0&&!strcmp(keys[count-1],required))version_seen=true;
            }
            if(!value(r,depth+1,required,remaining))return false;
            json_skip_whitespace(r);if(r->pos>=r->length)return false;
            if(r->data[r->pos]==end){++r->pos;return depth!=0 || version_seen;}
            if(!json_expect(r,','))return false;
        }
    }
    if(ch=='"')return json_parse_string(r,NULL,0);
    if(ch=='t'||ch=='f')return json_parse_bool(r,NULL);
    if(ch=='n')return json_match_literal(r,"null");
    return json_parse_number(r,NULL);
}
// Rejects trailing data, malformed punctuation, excessive nesting and duplicate members.
bool daw_json_structure_valid(const char* data,size_t size,const char* required) {
    JsonReader r;json_reader_init(&r,data,size);
    json_skip_whitespace(&r);if(r.pos==r.length || r.data[r.pos]!='{')return false;
    unsigned remaining=100000;
    if(!value(&r,0,required,&remaining))return false;json_skip_whitespace(&r);return r.pos==r.length;
}
// Accepts exact nonnegative integral frame tokens across the full uint64 range.
bool json_parse_u64(JsonReader* r,uint64_t* out) {
    json_skip_whitespace(r);uint64_t n=0;size_t start=r->pos;
    while(r->pos<r->length && r->data[r->pos]>='0' && r->data[r->pos]<='9') {
        unsigned d=(unsigned)(r->data[r->pos++]-'0');if(n>(UINT64_MAX-d)/10)return false;n=n*10+d;
    }
    if(r->pos==start)return false;
    if(r->pos<r->length && (r->data[r->pos]=='.'||r->data[r->pos]=='e'||r->data[r->pos]=='E'))return false;
    *out=n;return true;
}

// Requires session schema identity before document-specific parsing.
bool session_json_structure_valid(const char* data,size_t size) {
    return daw_json_structure_valid(data,size,"version");
}
