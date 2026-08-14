/* jsonlite.c */
#include "jsonlite.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static jl_val_t *newval(jl_type_t t) {
    jl_val_t *v = (jl_val_t*)calloc(1, sizeof(jl_val_t));
    if (v) v->type = t;
    return v;
}

/* ---- parser ---- */
static void skip_ws(jl_parse_t *p) {
    while (p->cur < p->end) { char c = *p->cur;
        if (c==' '||c=='\t'||c=='\n'||c=='\r') p->cur++; else break; }
}

static jl_val_t *parse_value(jl_parse_t *p);

static jl_val_t *parse_string(jl_parse_t *p) {
    if (p->cur >= p->end || *p->cur != '"') { p->err=1; return NULL; }
    p->cur++;
    jl_val_t *v = newval(JL_STRING); if(!v){p->err=1;return NULL;}
    size_t cap=32, len=0;
    char *buf = (char*)malloc(cap);
    while (p->cur < p->end && *p->cur != '"') {
        char c = *p->cur++;
        if (c=='\\') {
            if (p->cur >= p->end){p->err=1;break;}
            char e = *p->cur++;
            switch(e){ case '"':c='"';break; case '\\':c='\\';break; case '/':c='/';break;
                case 'b':c='\b';break; case 'f':c='\f';break; case 'n':c='\n';break;
                case 'r':c='\r';break; case 't':c='\t';break;
                case 'u':{ /* decode \uXXXX to utf8 */ if (p->cur+4>p->end){p->err=1;break;}
                    unsigned cp=0; for(int i=0;i<4;i++){char h=*p->cur++;
                        cp<<=4; if(h>='0'&&h<='9')cp|=(h-'0'); else if(h>='a'&&h<='f')cp|=(h-'a'+10);
                        else if(h>='A'&&h<='F')cp|=(h-'A'+10); else {p->err=1;break;}}
                    if(p->err)break; char outb[5]; int on=0; if(cp<0x80)outb[on++]=cp;
                    else if(cp<0x800){outb[on++]=(char)(0xC0|(cp>>6));outb[on++]=(char)(0x80|(cp&0x3F));}
                    else {outb[on++]=(char)(0xE0|(cp>>12));outb[on++]=(char)(0x80|((cp>>6)&0x3F));outb[on++]=(char)(0x80|(cp&0x3F));}
                    if(len+(size_t)on+1>cap){cap=(len+on+1)*2;buf=(char*)realloc(buf,cap);}
                    memcpy(buf+len,outb,on);len+=on; continue;}
                default: p->err=1; break;
            }
            if(p->err)break;
        }
        if(len+2>cap){cap*=2;buf=(char*)realloc(buf,cap);}
        buf[len++]=c;
    }
    if(p->err){free(buf);jl_free(v);return NULL;}
    if(p->cur>=p->end){p->err=1;free(buf);jl_free(v);return NULL;}
    p->cur++; /* closing quote */
    buf[len]=0; v->str=buf; v->strlen=len;
    return v;
}

static jl_val_t *parse_array(jl_parse_t *p) {
    p->cur++; jl_val_t *v=newval(JL_ARRAY); if(!v){p->err=1;return NULL;}
    jl_val_t *tail=NULL;
    skip_ws(p);
    if(p->cur<p->end && *p->cur==']'){p->cur++;return v;}
    while(p->cur<p->end){
        jl_val_t *e=parse_value(p); if(p->err){jl_free(v);return NULL;}
        if(!v->child)v->child=e;else{tail->next=e;}
        tail=e; v->count++;
        skip_ws(p);
        if(p->cur<p->end&&*p->cur==','){p->cur++;skip_ws(p);continue;}
        if(p->cur<p->end&&*p->cur==']'){p->cur++;return v;}
        p->err=1;jl_free(v);return NULL;
    }
    p->err=1;jl_free(v);return NULL;
}

static jl_val_t *parse_object(jl_parse_t *p) {
    p->cur++; jl_val_t *v=newval(JL_OBJECT); if(!v){p->err=1;return NULL;}
    jl_val_t *tail=NULL;
    skip_ws(p);
    if(p->cur<p->end && *p->cur=='}'){p->cur++;return v;}
    while(p->cur<p->end){
        jl_val_t *key=parse_string(p); if(p->err){jl_free(v);return NULL;}
        skip_ws(p);
        if(p->cur<p->end&&*p->cur==':'){p->cur++;skip_ws(p);}else{p->err=1;jl_free(key);jl_free(v);return NULL;}
        jl_val_t *val=parse_value(p); if(p->err){jl_free(key);jl_free(v);return NULL;}
        jl_val_t *pair=newval(JL_OBJECT); pair->str=key->str; pair->strlen=key->strlen; pair->child=val;
        free(key); /* moved key->str into pair */
        if(!v->child)v->child=pair;else{tail->next=pair;}
        tail=pair; v->count++;
        skip_ws(p);
        if(p->cur<p->end&&*p->cur==','){p->cur++;skip_ws(p);continue;}
        if(p->cur<p->end&&*p->cur=='}'){p->cur++;return v;}
        p->err=1;jl_free(v);return NULL;
    }
    p->err=1;jl_free(v);return NULL;
}

static jl_val_t *parse_value(jl_parse_t *p) {
    skip_ws(p);
    if(p->cur>=p->end){p->err=1;return NULL;}
    char c=*p->cur;
    if(c=='{')return parse_object(p);
    if(c=='[')return parse_array(p);
    if(c=='"')return parse_string(p);
    if(c=='t'){ if(p->end-p->cur>=4&&!memcmp(p->cur,"true",4)){p->cur+=4;jl_val_t*b=newval(JL_BOOL);b->num=1;return b;} p->err=1;return NULL;}
    if(c=='f'){ if(p->end-p->cur>=5&&!memcmp(p->cur,"false",5)){p->cur+=5;jl_val_t*b=newval(JL_BOOL);b->num=0;return b;} p->err=1;return NULL;}
    if(c=='n'){ if(p->end-p->cur>=4&&!memcmp(p->cur,"null",4)){p->cur+=4;return newval(JL_NULL);} p->err=1;return NULL;}
    if(c=='-'||(c>='0'&&c<='9')){
        char *ep; double d=strtod(p->cur,&ep); if(ep==p->cur){p->err=1;return NULL;} p->cur=ep;
        jl_val_t *n=newval(JL_NUMBER); n->num=d; return n;
    }
    p->err=1; return NULL;
}

jl_val_t *jl_parse(const char *s, size_t len){
    size_t l = len?len:strlen(s);
    jl_parse_t p={s,s+l,0};
    jl_val_t *v=parse_value(&p);
    if(p.err){jl_free(v);return NULL;}
    return v;
}

/* Nodes: an OBJECT node is either a pair (str=key, child=value) when it is a
 * child of an object, or an object value (str=NULL, child=pair list) when it is
 * a child of an array or a pair's value. Distinguish via str to free everything. */
void jl_free(jl_val_t *v){
    if(!v)return;
    if(v->type==JL_STRING){ if(v->str)free(v->str); free(v); return; }
    jl_val_t *c=v->child;
    while(c){
        jl_val_t *n=c->next;
        if(c->type==JL_OBJECT){
            if(c->str){ /* pair: child=value owns, str=key */
                if(c->child)jl_free(c->child);
                free(c->str);
                free(c);
            } else {    /* object value: owns its whole pair list */
                jl_free(c);
            }
        } else {
            if(c->str)free(c->str);
            if(c->child)jl_free(c->child);
            free(c);
        }
        c=n;
    }
    free(v);
}

const jl_val_t *jl_obj_get(const jl_val_t *obj, const char *key){
    if(!obj||obj->type!=JL_OBJECT)return NULL;
    jl_val_t *p=obj->child;
    while(p){
        if(p->str && !strcmp(p->str,key)){
            /* pair node -> return its value (child), unless value is missing */
            return p->child? p->child : p;
        }
        p=p->next;
    }
    return NULL;
}

const jl_val_t *jl_arr_at(const jl_val_t *arr, size_t i){
    if(!arr||arr->type!=JL_ARRAY)return NULL;
    jl_val_t *e=arr->child;
    while(e&&i--){ e=e->next; }
    return e;
}

/* ---- serializer ---- */
static void escstr(const char *s, char **out, size_t *cap, size_t *len){
    size_t l=strlen(s);
    size_t need=*len+l*6+2;
    if(need>*cap){*cap=need*2;*out=(char*)realloc(*out,*cap);}
    char *p=*out+*len; *p++='"';
    for(size_t i=0;i<l;i++){
        char c=s[i];
        if(c=='"'||c=='\\'||(c>=0&&c<0x20)){
            *p++='\\';
            switch(c){case '"':*p++='"';break;case '\\':*p++='\\';break;
                case '\n':*p++='n';break;case '\r':*p++='r';break;case '\t':*p++='t';break;
                case '\b':*p++='b';break;case '\f':*p++='f';break;
                default:{*p++='u';*p++='0';*p++='0';const char hx[]="0123456789abcdef";
                    *p++=hx[(c>>4)&0xf];*p++=hx[c&0xf];break;}}
        } else { *p++=c; }
    }
    *p++='"'; *len=p-*out;
}

static void emit(jl_val_t *v, char **out, size_t *cap, size_t *len){
    char buf[64];
    switch(v->type){
        case JL_NULL: { size_t l=4; if(*len+l+1>*cap){*cap=(*len+l+2)*2;*out=realloc(*out,*cap);} memcpy(*out+*len,"null",4);*len+=4; return; }
        case JL_BOOL: { const char*s=v->num?"true":"false"; size_t l=strlen(s);
            if(*len+l+1>*cap){*cap=(*len+l+2)*2;*out=realloc(*out,*cap);} memcpy(*out+*len,s,l);*len+=l; return; }
        case JL_NUMBER: snprintf(buf,sizeof buf,"%.17g",v->num); break;
        case JL_STRING: escstr(v->str,out,cap,len); return;
        case JL_ARRAY:{
            size_t l=*len+1; if(l>*cap){*cap=l*2;*out=realloc(*out,*cap);} (*out)[(*len)++]='[';
            jl_val_t*e=v->child;int first=1;
            while(e){ if(!first){ size_t l2=*len+1; if(l2>*cap){*cap=l2*2;*out=realloc(*out,*cap);} (*out)[(*len)++]=','; }
                first=0; emit(e,out,cap,len); e=e->next; }
            l=*len+1; if(l>*cap){*cap=l*2;*out=realloc(*out,*cap);} (*out)[(*len)++]=']'; return; }
        case JL_OBJECT:{
            size_t l=*len+1; if(l>*cap){*cap=l*2;*out=realloc(*out,*cap);} (*out)[(*len)++]='{';
            jl_val_t*p=v->child;int first=1;
            while(p){ if(p->str){
                if(!first){ size_t l2=*len+1; if(l2>*cap){*cap=l2*2;*out=realloc(*out,*cap);} (*out)[(*len)++]=','; }
                first=0;
                escstr(p->str,out,cap,len);
                size_t l3=*len+1; if(l3>*cap){*cap=l3*2;*out=realloc(*out,*cap);} (*out)[(*len)++]=':';
                /* value is the pair's child OR the pair holds value via child */
                if(p->child) emit(p->child,out,cap,len); else {
                    size_t lz=*len+1; if(lz>*cap){*cap=lz*2;*out=realloc(*out,*cap);} (*out)[(*len)++]='n';
                }
                /* for non-pair objects where a STRING key sits directly: handled above via child */
                } p=p->next; }
            l=*len+1; if(l>*cap){*cap=l*2;*out=realloc(*out,*cap);} (*out)[(*len)++]='}'; return; }
    }
    if(buf[0]){ size_t lb=strlen(buf);
        if(*len+lb+1>*cap){*cap=(*len+lb+2)*2;*out=realloc(*out,*cap);}
        memcpy(*out+*len,buf,lb);*len+=lb; }
}

char *jl_stringify(const jl_val_t *v){
    char *out=(char*)malloc(256); if(!out) return NULL;
    size_t cap=256,len=0;
    emit((jl_val_t*)v,&out,&cap,&len);
    if(!out){ out=(char*)malloc(1); out[0]=0; }
    else { char *t=(char*)realloc(out,len+1); t[len]=0; out=t; }
    return out;
}

jl_val_t *jl_new_string(const char *s){ jl_val_t *v=newval(JL_STRING); v->str=strdup(s?s:""); v->strlen=strlen(v->str); return v; }
jl_val_t *jl_new_number(double n){ jl_val_t *v=newval(JL_NUMBER); v->num=n; return v; }
jl_val_t *jl_new_bool(int b){ jl_val_t *v=newval(JL_BOOL); v->num=b?1:0; return v; }
jl_val_t *jl_new_object(void){ return newval(JL_OBJECT); }
jl_val_t *jl_new_array(void){ return newval(JL_ARRAY); }
void jl_obj_set(jl_val_t *obj,const char *key,jl_val_t *val){
    jl_val_t *pair=newval(JL_OBJECT); pair->str=strdup(key); pair->strlen=strlen(key); pair->child=val;
    if(!obj->child){obj->child=pair;}else{ jl_val_t*t=obj->child; while(t->next)t=t->next; t->next=pair; }
}
void jl_arr_push(jl_val_t *arr,jl_val_t *val){
    if(!arr->child){arr->child=val;val->next=NULL;}
    else{ jl_val_t*t=arr->child; while(t->next)t=t->next; t->next=val; }
}
