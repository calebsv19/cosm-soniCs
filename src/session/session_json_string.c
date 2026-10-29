#include "session_io_read_internal.h"
#include <stdint.h>
// Decodes one hex digit without accepting invalid escape bytes.
static int hex(char c){return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : c>='A' && c<='F' ? c-'A'+10 : -1;}
// Reads one exact UTF-16 escape word while advancing only a bounded source reader.
static bool word(JsonReader* r,uint32_t* out){if(r->length-r->pos<4)return false;*out=0;for(int i=0;i<4;++i){int v=hex(r->data[r->pos++]);if(v<0)return false;*out=(*out<<4)|v;}return true;}
// Appends a whole Unicode scalar without truncating a multi-byte character or embedding a C-string NUL.
static bool append(char* dst,size_t capacity,size_t* pos,uint32_t cp){
    if(!cp || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
    unsigned char bytes[4];int n=0;
    if(cp<0x80)bytes[n++]=cp;else if(cp<0x800){bytes[n++]=0xc0|(cp>>6);bytes[n++]=0x80|(cp&63);}
    else if(cp<0x10000){bytes[n++]=0xe0|(cp>>12);bytes[n++]=0x80|((cp>>6)&63);bytes[n++]=0x80|(cp&63);}
    else{bytes[n++]=0xf0|(cp>>18);bytes[n++]=0x80|((cp>>12)&63);bytes[n++]=0x80|((cp>>6)&63);bytes[n++]=0x80|(cp&63);}
    if(dst){if(*pos+(size_t)n>=capacity)return false;for(int i=0;i<n;++i)dst[(*pos)++]=(char)bytes[i];}return true;
}
// Preserves raw UTF-8 and JSON Unicode escapes identically; rejects malformed, incomplete and overlong strings.
bool json_parse_string(JsonReader* r,char* dst,size_t capacity){
    if(!r || (dst && !capacity) || !json_expect(r,'"'))return false;size_t pos=0;
    while(r->pos<r->length){uint32_t cp=(unsigned char)r->data[r->pos++];
        if(cp=='"'){if(dst)dst[pos]=0;return true;}
        if(cp=='\\'){
            if(r->pos>=r->length)return false;char escaped=r->data[r->pos++];
            switch(escaped){case '"':cp='"';break;case '\\':cp='\\';break;case '/':cp='/';break;case 'b':cp='\b';break;case 'f':cp='\f';break;case 'n':cp='\n';break;case 'r':cp='\r';break;case 't':cp='\t';break;
                case 'u':if(!word(r,&cp))return false;if(cp>=0xd800 && cp<=0xdbff){uint32_t low;
                    if(r->length-r->pos<6 || r->data[r->pos]!='\\' || r->data[r->pos+1]!='u')return false;r->pos+=2;
                    if(!word(r,&low) || low<0xdc00 || low>0xdfff)return false;cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}break;
                default:return false;}
        }else if(cp<0x20)return false;
        else if(cp>=0x80){int follow;uint32_t minimum;
            if(cp>=0xc2 && cp<=0xdf){follow=1;minimum=0x80;cp&=31;}
            else if(cp>=0xe0 && cp<=0xef){follow=2;minimum=0x800;cp&=15;}
            else if(cp>=0xf0 && cp<=0xf4){follow=3;minimum=0x10000;cp&=7;}else return false;
            if(r->length-r->pos<(size_t)follow)return false;
            for(int i=0;i<follow;++i){unsigned char byte=r->data[r->pos++];if((byte&0xc0)!=0x80)return false;cp=(cp<<6)|(byte&63);}
            if(cp<minimum)return false;
        }
        if(!append(dst,capacity,&pos,cp))return false;
    }return false;
}
