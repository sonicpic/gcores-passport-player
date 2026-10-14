/* SPDX-License-Identifier: GPL-3.0-only */
#include "passport_audio_http.h"
#include <stddef.h>
#include <string.h>

static unsigned char lower(unsigned char c) { return c>='A' && c<='Z'?c+('a'-'A'):c; }
static bool equal_name(const char *a,const char *b) {
    while (*a && *b) if (lower((unsigned char)*a++)!=lower((unsigned char)*b++)) return false;
    return *a==*b;
}
static void skip_ows(const char **p) { while (**p==' ' || **p=='\t') ++*p; }
static bool number(const char **p,uint64_t *out) {
    if (**p<'0' || **p>'9') return false;
    uint64_t value=0;
    do {
        unsigned digit=(unsigned)(*(*p)++-'0');
        if (value>(UINT64_MAX-digit)/10) return false;
        value=value*10+digit;
    } while (**p>='0' && **p<='9');
    *out=value; return true;
}
static bool range(const char *p,uint64_t *start,uint64_t *end,uint64_t *total) {
    skip_ows(&p);
    const char *unit="bytes";
    while (*unit) if (lower((unsigned char)*p++)!=(unsigned char)*unit++) return false;
    if (*p!=' ' && *p!='\t') return false;
    skip_ows(&p);
    if (!number(&p,start) || *p++!='-' || !number(&p,end) || *p++!='/' || !number(&p,total)) return false;
    skip_ows(&p);
    return !*p && *end>=*start && *total>*end;
}
static bool etag(const char *value,bool *strong) {
    const unsigned char *p=(const unsigned char *)value;
    *strong=true;
    if (p[0]=='W' && p[1]=='/') { *strong=false; p+=2; }
    if (*p++!='"') return false;
    while (*p && *p!='"') {
        // RFC 9110 etagc: !, # through ~, or obs-text; no escaping syntax.
        if (*p<0x21 || *p==0x7f) return false;
        ++p;
    }
    return *p=='"' && !p[1];
}
bool passport_http_strong_etag(const char *value) {
    bool strong=false;
    return value && strlen(value)<128 && etag(value,&strong) && strong;
}
void passport_http_header(passport_http_response_t *r,const char *key,const char *value) {
    if (!r || !key || !value) return;
    if (equal_name(key,"Content-Range")) {
        uint64_t start=0,end=0,total=0;
        bool valid=range(value,&start,&end,&total);
        if (!valid || (r->range_seen && (!r->range_valid || r->range_start!=start ||
                        r->range_end!=end || r->range_total!=total))) r->invalid=true;
        r->range_seen=true;
        if (valid && !r->invalid) {
            r->range_start=start; r->range_end=end; r->range_total=total; r->range_valid=true;
        } else r->range_valid=false;
    } else if (equal_name(key,"ETag")) {
        bool strong=false;
        bool valid=strlen(value)<sizeof(r->validator) && etag(value,&strong);
        if (!valid || (r->etag_seen && (r->etag_strong!=strong || (strong && strcmp(r->validator,value)))))
            r->etag_invalid=true;
        r->etag_seen=true;
        if (valid && strong && !r->etag_invalid) {
            memcpy(r->validator,value,strlen(value)+1); r->etag_strong=true;
        } else { r->validator[0]=0; r->etag_strong=false; }
    } else if (equal_name(key,"Content-Encoding")) {
        const char *p=value; skip_ows(&p);
        const char *word="identity";
        while (*word && *p && lower((unsigned char)*p)==(unsigned char)*word) { ++p; ++word; }
        skip_ows(&p);
        if (*word || *p) r->encoded=true;
    }
}
passport_http_decision_t passport_http_validate(const passport_http_response_t *r,
    int status,int64_t length,uint64_t offset,uint64_t expected_total,
    const char *expected_validator,uint64_t *full_total) {
    if (!r || !full_total || length<-1) return PASSPORT_HTTP_REJECT;
    if (!offset) {
        if (status!=200 || r->encoded || r->invalid || r->range_seen) return PASSPORT_HTTP_REJECT;
        *full_total=length>0?(uint64_t)length:0;
        return PASSPORT_HTTP_ACCEPT;
    }
    if (status==200 || status==416) return PASSPORT_HTTP_RESTART_ZERO;
    if (status!=206) return PASSPORT_HTTP_REJECT;
    if (r->encoded || r->invalid || !r->range_valid || r->range_start!=offset ||
        r->range_end!=r->range_total-1 || !expected_total || r->range_total!=expected_total ||
        !passport_http_strong_etag(expected_validator) || r->etag_invalid || !r->etag_strong ||
        strcmp(r->validator,expected_validator) ||
        (length>=0 && (uint64_t)length!=r->range_end-r->range_start+1))
        return PASSPORT_HTTP_RESTART_ZERO;
    *full_total=r->range_total;
    return PASSPORT_HTTP_ACCEPT;
}
