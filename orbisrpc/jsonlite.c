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
    if(!buf){ p->err=1; jl_free(v); return NULL; }
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
                    if(len+(size_t)on+1>cap){
                        size_t ncap=(len+(size_t)on+1)*2;
                        char *nb=(char*)realloc(buf,ncap);
                        if(!nb){ free(buf); jl_free(v); p->err=1; return NULL; }
                        buf=nb; cap=ncap;
                    }
                    memcpy(buf+len,outb,on);len+=on; continue;}
                default: p->err=1; break;
            }
            if(p->err)break;
        }
        if(len+2>cap){
            size_t ncap=cap*2;
            char *nb=(char*)realloc(buf,ncap);
            if(!nb){ free(buf); jl_free(v); p->err=1; return NULL; }
            buf=nb; cap=ncap;
        }
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
    if(!s) return NULL;
    size_t l = len;
    jl_parse_t p={s,s+l,0};
    jl_val_t *v=parse_value(&p);
    skip_ws(&p);
    if(p.err || p.cur != p.end){jl_free(v);return NULL;}
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

jl_val_t *jl_obj_get(const jl_val_t *obj, const char *key){
    if(!obj||obj->type!=JL_OBJECT||!key)return NULL;
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

jl_val_t *jl_arr_at(const jl_val_t *arr, size_t i){
    if(!arr||arr->type!=JL_ARRAY)return NULL;
    jl_val_t *e=arr->child;
    while(e&&i--){ e=e->next; }
    return e;
}

/* ---- serializer ---- */
static int ensure_capacity(char **out, size_t *cap, size_t need);

static int escstr(const char *s, char **out, size_t *cap, size_t *len){
    if(!s) s = "";
    size_t l=strlen(s);
    if(l > ((size_t)-1 - *len - 3) / 6) return -1;
    size_t need=*len+l*6+2;
    if(ensure_capacity(out,cap,need+1)<0) return -1;
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
    *p++='"'; *len=(size_t)(p-*out);
    return 0;
}

static int ensure_capacity(char **out, size_t *cap, size_t need){
    if (need <= *cap) return 0;
    size_t ncap = *cap;
    while (ncap < need) {
        if (ncap > (size_t)-1 / 2) return -1;
        ncap *= 2;
    }
    char *p = (char *)realloc(*out, ncap);
    if (!p) return -1;
    *out = p;
    *cap = ncap;
    return 0;
}

static int emit(jl_val_t *v, char **out, size_t *cap, size_t *len){
    char buf[64];
    switch(v->type){
        case JL_NULL: { size_t l=4; if(ensure_capacity(out,cap,*len+l+1)<0)return -1; memcpy(*out+*len,"null",4);*len+=4; return 0; }
        case JL_BOOL: { const char*s=v->num?"true":"false"; size_t l=strlen(s);
            if(ensure_capacity(out,cap,*len+l+1)<0)return -1; memcpy(*out+*len,s,l);*len+=l; return 0; }
        case JL_NUMBER: snprintf(buf,sizeof buf,"%.17g",v->num); break;
        case JL_STRING: return escstr(v->str,out,cap,len);
        case JL_ARRAY:{
            size_t l=*len+1; if(ensure_capacity(out,cap,l+1)<0)return -1; (*out)[(*len)++]='[';
            jl_val_t*e=v->child;int first=1;
            while(e){ if(!first){ size_t l2=*len+1; if(ensure_capacity(out,cap,l2+1)<0)return -1; (*out)[(*len)++]=','; }
                first=0; if(emit(e,out,cap,len)<0)return -1; e=e->next; }
            l=*len+1; if(ensure_capacity(out,cap,l+1)<0)return -1; (*out)[(*len)++]=']'; return 0; }
        case JL_OBJECT:{
            size_t l=*len+1; if(ensure_capacity(out,cap,l+1)<0)return -1; (*out)[(*len)++]='{';
            jl_val_t*p=v->child;int first=1;
            while(p){ if(p->str){
                if(!first){ size_t l2=*len+1; if(ensure_capacity(out,cap,l2+1)<0)return -1; (*out)[(*len)++]=','; }
                first=0;
                if(escstr(p->str,out,cap,len)<0)return -1;
                size_t l3=*len+1; if(ensure_capacity(out,cap,l3+1)<0)return -1; (*out)[(*len)++]= ':';
                if(p->child){ if(emit(p->child,out,cap,len)<0)return -1; }
                else { size_t lz=*len+4; if(ensure_capacity(out,cap,lz+1)<0)return -1; memcpy(*out+*len,"null",4);*len+=4; }
                } p=p->next; }
            l=*len+1; if(ensure_capacity(out,cap,l+1)<0)return -1; (*out)[(*len)++]='}'; return 0; }
    }
    if(buf[0]){ size_t lb=strlen(buf);
        if(ensure_capacity(out,cap,*len+lb+1)<0)return -1;
        memcpy(*out+*len,buf,lb);*len+=lb;
    }
    return 0;
}

char *jl_stringify(const jl_val_t *v){
    if(!v) return NULL;
    char *out=(char*)malloc(256); if(!out) return NULL;
    size_t cap=256,len=0;
    if(emit((jl_val_t*)v,&out,&cap,&len)<0){ free(out); return NULL; }
    char *t=(char*)realloc(out,len+1);
    if(t){ t[len]=0; return t; }
    out[len]=0; return out; /* realloc failed: still return what we have */
}

jl_val_t *jl_new_string(const char *s){
    jl_val_t *v=newval(JL_STRING);
    if(!v) return NULL;
    v->str=strdup(s?s:"");
    if(!v->str){ free(v); return NULL; }
    v->strlen=strlen(v->str);
    return v;
}
jl_val_t *jl_new_number(double n){ jl_val_t *v=newval(JL_NUMBER); if(v)v->num=n; return v; }
jl_val_t *jl_new_bool(int b){ jl_val_t *v=newval(JL_BOOL); if(v)v->num=b?1:0; return v; }
jl_val_t *jl_new_object(void){ return newval(JL_OBJECT); }
jl_val_t *jl_new_array(void){ return newval(JL_ARRAY); }
void jl_obj_set(jl_val_t *obj,const char *key,jl_val_t *val){
    if(!obj || obj->type!=JL_OBJECT || !key || !val) return;
    jl_val_t *pair=newval(JL_OBJECT);
    if(!pair) return;
    pair->str=strdup(key);
    if(!pair->str){ free(pair); return; }
    pair->strlen=strlen(key); pair->child=val;
    if(!obj->child){obj->child=pair;}else{ jl_val_t*t=obj->child; while(t->next)t=t->next; t->next=pair; }
    obj->count++;
}
void jl_arr_push(jl_val_t *arr,jl_val_t *val){
    if(!arr || arr->type!=JL_ARRAY || !val) return;
    if(!arr->child){arr->child=val;val->next=NULL;}
    else{ jl_val_t*t=arr->child; while(t->next)t=t->next; t->next=val; }
    arr->count++;
}
