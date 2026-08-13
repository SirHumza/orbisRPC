/* jsonlite.c - implementation */
#include "jsonlite.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static jl_val_t *newval(jl_type_t t) {
    jl_val_t *v = (jl_val_t *)calloc(1, sizeof(jl_val_t));
    if (v) v->type = t;
    return v;
}

/* ---- parser ---- */
static void skip_ws(jl_parse_t *p) {
    while (p->cur < p->end && (*p->cur == ' ' || *p->cur == '\t' ||
            *p->cur == '\n' || *p->cur == '\r')) p->cur++;
}

static char *parse_unicode_escape(const char **sp) {
    /* caller ensures 4 hex; returns malloc'd utf8 */
    const char *s = *sp + 2; /* skip 'u' */
    unsigned cp = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        cp <<= 4;
        if (c >= '0' && c <= '9') cp |= (c - '0');
        else if (c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
        else return NULL;
    }
    *sp = s + 4;
    char *out = (char *)malloc(5);
    int n = 0;
    if (cp < 0x80) { out[n++] = (char)cp; }
    else if (cp < 0x800) { out[n++]=(char)(0xC0|(cp>>6)); out[n++]=(char)(0x80|(cp&0x3F)); }
    else { out[n++]=(char)(0xE0|(cp>>12)); out[n++]=(char)(0x80|((cp>>6)&0x3F)); out[n++]=(char)(0x80|(cp&0x3F)); }
    out[n] = 0;
    return out;
}

static jl_val_t *parse_string(jl_parse_t *p) {
    if (*p->cur != '"') { p->err = 1; return NULL; }
    p->cur++;
    jl_val_t *v = newval(JL_STRING);
    if (!v) return NULL;
    size_t cap = 16, len = 0;
    char *buf = (char *)malloc(cap);
    while (p->cur < p->end && *p->cur != '"') {
        char c = *p->cur;
        if (c == '\\') {
            p->cur++;
            if (p->cur >= p->end) { p->err = 1; free(buf); free(v); return NULL; }
            char e = *p->cur++;
            switch (e) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    char *u = parse_unicode_escape(&p->cur);
                    if (!u) { p->err = 1; free(buf); free(v); return NULL; }
                    size_t ul = strlen(u);
                    if (len + ul + 1 > cap) { cap = (len + ul + 1) * 2; buf = (char *)realloc(buf, cap); }
                    memcpy(buf + len, u, ul); len += ul; free(u);
                    continue;
                }
                default: p->err = 1; free(buf); free(v); return NULL;
            }
        }
        if (len + 2 > cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
        buf[len++] = c;
        p->cur++;
    }
    if (p->cur >= p->end) { p->err = 1; free(buf); free(v); return NULL; } /* no closing quote */
    p->cur++; /* skip closing quote */
    buf[len] = 0;
    v->str = buf;
    v->strlen = len;
    return v;
}

static jl_val_t *parse_value(jl_parse_t *p);

static jl_val_t *parse_array(jl_parse_t *p) {
    p->cur++; /* [ */
    jl_val_t *v = newval(JL_ARRAY);
    jl_val_t *tail = NULL;
    skip_ws(p);
    if (p->cur < p->end && *p->cur == ']') { p->cur++; return v; }
    while (p->cur < p->end) {
        jl_val_t *e = parse_value(p);
        if (p->err) { jl_free(v); return NULL; }
        if (!v->child) v->child = e; else { tail->next = e; }
        tail = e; v->count++;
        skip_ws(p);
        if (p->cur < p->end && *p->cur == ',') { p->cur++; skip_ws(p); continue; }
        if (p->cur < p->end && *p->cur == ']') { p->cur++; return v; }
        p->err = 1; jl_free(v); return NULL;
    }
    p->err = 1; jl_free(v); return NULL;
}

static jl_val_t *parse_object(jl_parse_t *p) {
    p->cur++; /* { */
    jl_val_t *v = newval(JL_OBJECT);
    jl_val_t *tail = NULL;
    skip_ws(p);
    if (p->cur < p->end && *p->cur == '}') { p->cur++; return v; }
    while (p->cur < p->end) {
        jl_val_t *key = parse_string(p); /* uses string parser; key->str is the key */
        if (p->err) { jl_free(v); return NULL; }
        skip_ws(p);
        if (p->cur < p->end && *p->cur == ':') { p->cur++; skip_ws(p); } else { p->err = 1; jl_free(key); jl_free(v); return NULL; }
        jl_val_t *val = parse_value(p);
        if (p->err) { jl_free(key); jl_free(v); return NULL; }
        jl_val_t *pair = newval(JL_OBJECT); /* reuse as pair node: pair->str=key, pair->child=val */
        pair->str = key->str; pair->strlen = key->strlen;
        pair->child = val;
        key->str = NULL; free(key); key = pair;
        if (!v->child) v->child = key; else { tail->next = key; }
        tail = key; v->count++;
        skip_ws(p);
        if (p->cur < p->end && *p->cur == ',') { p->cur++; skip_ws(p); continue; }
        if (p->cur < p->end && *p->cur == '}') { p->cur++; return v; }
        p->err = 1; jl_free(v); return NULL;
    }
    p->err = 1; jl_free(v); return NULL;
}

static jl_val_t *parse_value(jl_parse_t *p) {
    skip_ws(p);
    if (p->cur >= p->end) { p->err = 1; return NULL; }
    char c = *p->cur;
    if (c == '{') return parse_object(p);
    if (c == '[') return parse_array(p);
    if (c == '"') return parse_string(p);
    if (c == 't') { if (p->end - p->cur >= 4 && !memcmp(p->cur,"true",4)) { p->cur += 4; jl_val_t *b=newval(JL_BOOL); b->num=1; return b; } p->err=1; return NULL; }
    if (c == 'f') { if (p->end - p->cur >= 5 && !memcmp(p->cur,"false",5)) { p->cur += 5; jl_val_t *b=newval(JL_BOOL); b->num=0; return b; } p->err=1; return NULL; }
    if (c == 'n') { if (p->end - p->cur >= 4 && !memcmp(p->cur,"null",4)) { p->cur += 4; return newval(JL_NULL); } p->err=1; return NULL; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *endp; double d = strtod(p->cur, &endp);
        if (endp == p->cur) { p->err = 1; return NULL; }
        p->cur = endp;
        jl_val_t *n = newval(JL_NUMBER); n->num = d; return n;
    }
    p->err = 1; return NULL;
}

jl_val_t *jl_parse(const char *s, size_t len) {
    jl_parse_t p = { s, s + (len ? len : strlen(s)), 0 };
    jl_val_t *v = parse_value(&p);
    if (p.err) { jl_free(v); return NULL; }
    return v;
}

void jl_free(jl_val_t *v) {
    if (!v) return;
    if (v->type == JL_STRING || v->type == JL_OBJECT) {
        free(v->str);
    }
    /* for object pairs, child==val owned separately, str owned above */
    jl_val_t *c = v->child;
    while (c) {
        jl_val_t *n = c->next;
        if (c->type == JL_OBJECT) { /* pair node: free val(child) + key(str free'd above) */
            if (c->child) jl_free(c->child);
            jl_free(c);
        } else {
            jl_free(c);
        }
        c = n;
    }
    free(v);
}

const jl_val_t *jl_obj_get(const jl_val_t *obj, const char *key) {
    if (!obj || obj->type != JL_OBJECT) return NULL;
    jl_val_t *p = obj->child;
    while (p) {
        if (p->type == JL_OBJECT && p->str && !strcmp(p->str, key)) return p->child;
        /* also tolerate plain-keyed entries */
        if (p->type != JL_OBJECT && p->str && !strcmp(p->str, key)) return p;
        p = p->next;
    }
    return NULL;
}

const jl_val_t *jl_arr_at(const jl_val_t *arr, size_t i) {
    if (!arr || arr->type != JL_ARRAY) return NULL;
    jl_val_t *e = arr->child;
    while (e && i--) e = e->next;
    return e;
}

/* ---- serializer ---- */
static void emit(jl_val_t *v, char **out, size_t *cap, size_t *len);

static void escstr(const char *s, char **out, size_t *cap, size_t *len) {
    size_t l = strlen(s);
    size_t need = *len + l * 6 + 2;
    if (need > *cap) { *cap = need * 2; *out = (char *)realloc(*out, *cap); }
    char *p = *out + *len; *p++ = '"';
    for (size_t i = 0; i < l; i++) {
        char c = s[i];
        if (c=='"'||c=='\\'||c=='\n'||c=='\r'||c=='\t') {
            *p++='\\';
            switch(c){case '"':*p++='"';break;case '\\':*p++='\\';break;case '\n':*p++='n';break;case '\r':*p++='r';break;case '\t':*p++='t';break;}
        } else if ((unsigned char)c < 0x20) {
            *p++='\\'; *p++='u'; *p++='0'; *p++='0';
            static const char hx[]="0123456789abcdef";
            *p++=hx[(c>>4)&0xf]; *p++=hx[c&0xf];
        } else { *p++=c; }
    }
    *p++='"';
    *len = p - *out;
}

static void emit(jl_val_t *v, char **out, size_t *cap, size_t *len) {
    if (!v) return;
    char buf[64];
    switch (v->type) {
        case JL_NULL: { size_t l=strlen("null"); if(*len+l+1>*cap){*cap=(*len+l+2)*2;*out=realloc(*out,*cap);} memcpy(*out+*len,"null",l); *len+=l; break; }
        case JL_BOOL: emit_number_fallback: ;
        case JL_NUMBER: snprintf(buf,sizeof buf,"%g", v->num); break;
        case JL_STRING: escstr(v->str,out,cap,len); return;
        case JL_ARRAY: {
            size_t l = (*len)+1; if(l>*cap){*cap=l*2;*out=realloc(*out,*cap);} (*out)[(*len)++]='[';
            jl_val_t *e=v->child; int first=1;
            while(e){ if(!first){ size_t l2=*len+1; if(l2>*cap){*cap=l2*2;*out=realloc(*out,*cap);} (*out)[(*len)++]=','; } first=0; emit(e,out,cap,len); e=e->next; }
            l=*len+1; if(l>*cap){*cap=l*2;*out=realloc(*out,*cap);} (*out)[(*len)++]=']'; return;
        }
        case JL_OBJECT: {
            size_t l = (*len)+1; if(l>*cap){*cap=l*2;*out=realloc(*out,*cap);} (*out)[(*len)++]='{';
            jl_val_t *p=v->child; int first=1;
            while(p){ if(p->str){ if(!first){ size_t l2=*len+1; if(l2>*cap){*cap=l2*2;*out=realloc(*out,*cap);} (*out)[(*len)++]=','; } first=0;
                escstr(p->str,out,cap,len);
                size_t l2=*len+1; if(l2>*cap){*cap=l2*2;*out=realloc(*out,*cap);} (*out)[(*len)++]=':';
                if(p->type==JL_OBJECT && p->child){ emit(p->child,out,cap,len); }
                else if(p->type==JL_STRING){ escstr(p->str,out,cap,len); }
                else { snprintf(buf,sizeof buf,"%g",p->num); size_t lb=strlen(buf); if(*len+lb+1>*cap){*cap=(*len+lb+2)*2;*out=realloc(*out,*cap);} memcpy(*out+*len,buf,lb); *len+=lb; }
                } p=p->next; }
            l=*len+1; if(l>*cap){*cap=l*2;*out=realloc(*out,*cap);} (*out)[(*len)++]='}'; return;
        }
    }
    if (buf[0]) { size_t lb=strlen(buf); if(*len+lb+1>*cap){*cap=(*len+lb+2)*2;*out=realloc(*out,*cap);} memcpy(*out+*len,buf,lb); *len+=lb; }
}

char *jl_stringify(const jl_val_t *v) {
    (void)v; char *out=NULL; size_t cap=256,len=0;
    /* stringify mutable copy path: emit takes jl_val_t* ; cast away const (safe: we don't mutate) */
    emit((jl_val_t*)v, &out, &cap, &len);
    if(!out){ out=(char*)malloc(1); out[0]=0; }
    else { char *t=(char*)realloc(out,len+1); t[len]=0; out=t; }
    return out;
}

/* ---- builders ---- */
jl_val_t *jl_new_string(const char *s){ jl_val_t *v=newval(JL_STRING); v->str=strdup(s); v->strlen=strlen(s); return v; }
jl_val_t *jl_new_number(double n){ jl_val_t *v=newval(JL_NUMBER); v->num=n; return v; }
jl_val_t *jl_new_bool(int b){ jl_val_t *v=newval(JL_BOOL); v->num=b; return v; }
jl_val_t *jl_new_object(void){ return newval(JL_OBJECT); }
jl_val_t *jl_new_array(void){ return newval(JL_ARRAY); }
void jl_obj_set(jl_val_t *obj, const char *key, jl_val_t *val){
    jl_val_t *pair=newval(JL_OBJECT); pair->str=strdup(key); pair->strlen=strlen(key); pair->child=val;
    if(!obj->child){obj->child=pair;}else{ jl_val_t*t=obj->child; while(t->next)t=t->next; t->next=pair; }
}
void jl_arr_push(jl_val_t *arr, jl_val_t *val){
    jl_val_t *e=newval(JL_ARRAY); /* reuse array node as element holder? simpler: push val directly chained */
    /* To keep iteration simple, store elements as siblings under child (type kept as the val's type) */
    if(!arr->child){arr->child=val; val->next=NULL;}
    else { jl_val_t *t=arr->child; while(t->next)t=t->next; t->next=val; }
    arr->count++;
}
void jl_set_type_string(jl_val_t *v, const char *s){ if(v->type==JL_STRING){free(v->str);v->str=strdup(s);v->strlen=strlen(s);} }
void jl_set_type_number(jl_val_t *v, double n){ v->num=n; }
