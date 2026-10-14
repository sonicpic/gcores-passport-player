#include "passport_audio_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static passport_http_response_t response(const char *range,const char *etag) {
    passport_http_response_t r={0};
    if (range) passport_http_header(&r,"Content-Range",range);
    if (etag) passport_http_header(&r,"ETag",etag);
    return r;
}
static passport_http_decision_t decide(passport_http_response_t *r,int status,int64_t length,
                                      uint64_t offset,uint64_t total,const char *validator) {
    uint64_t returned=12345;
    passport_http_decision_t d=passport_http_validate(r,status,length,offset,total,validator,&returned);
    assert(d!=PASSPORT_HTTP_ACCEPT?returned==12345:returned==(offset?total:length>0?(uint64_t)length:0));
    return d;
}
static void test_strict_range_and_duplicate_headers(void) {
    const char *bad[]={"","b","byte 10-19/20","bytes10-19/20","bytes -10-19/20","bytes +10-19/20",
        "bytes 10-9/20","bytes 10-20/20","bytes 10-19/*","bytes */20","bytes 10-19/20junk",
        "bytes 10-19/20\r\n","bytes 18446744073709551616-19/20","bytes 10-19/18446744073709551616"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        passport_http_response_t r=response(bad[i],"\"same\"");
        assert(!r.range_valid && r.invalid);
        assert(decide(&r,206,10,10,20,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    }
    passport_http_response_t r=response(" BYTES\t10-19/20\t","\"same\"");
    assert(r.range_valid && r.range_start==10 && r.range_end==19 && r.range_total==20);
    passport_http_header(&r,"Content-Range","bytes 10-19/20"); assert(!r.invalid);
    passport_http_header(&r,"Content-Range","bytes 11-19/20"); assert(r.invalid && !r.range_valid);
    r=response("bytes 18446744073709551613-18446744073709551614/18446744073709551615","\"same\"");
    assert(r.range_valid && r.range_total==UINT64_MAX);
    assert(decide(&r,206,2,UINT64_MAX-2,UINT64_MAX,"\"same\"")==PASSPORT_HTTP_ACCEPT);
}
static void test_etag_strength_order_and_size(void) {
    assert(passport_http_strong_etag("\"\"")); assert(passport_http_strong_etag("\"abc\""));
    const char *bad[]={NULL,"","abc","W/\"abc\"","w/\"abc\"","\"a b\"","\"a\r\nb\"","\"a\"junk","\"a\x7f\""};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!passport_http_strong_etag(bad[i]));
    passport_http_response_t r=response(NULL,NULL);
    passport_http_header(&r,"Last-Modified","Wed, 21 Oct 2015 07:28:00 GMT");
    assert(!r.etag_strong && !r.validator[0]);
    passport_http_header(&r,"eTaG","\"same\""); assert(r.etag_strong);
    passport_http_header(&r,"ETag","\"same\""); assert(!r.etag_invalid);
    passport_http_header(&r,"ETag","\"different\""); assert(r.etag_invalid && !r.etag_strong && !r.validator[0]);
    r=response(NULL,"W/\"same\""); assert(!r.etag_strong && !r.etag_invalid);
    passport_http_header(&r,"ETag","W/\"other\"");
    assert(!r.etag_strong && !r.validator[0]);
    assert(decide(&r,200,100,0,0,NULL)==PASSPORT_HTTP_ACCEPT);
    assert(decide(&r,206,90,10,100,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    passport_http_header(&r,"ETag","\"same\""); assert(r.etag_invalid);
    r=response(NULL,"\"same\""); passport_http_header(&r,"ETag","W/\"same\"");
    assert(r.etag_invalid && !r.etag_strong && !r.validator[0]);
    char large[130]; large[0]='"'; memset(large+1,'a',126); large[127]='"'; large[128]=0;
    assert(!passport_http_strong_etag(large));
    r=response(NULL,large); assert(r.etag_invalid && !r.validator[0]);
    large[126]='"'; large[127]=0; assert(passport_http_strong_etag(large));
}
static void test_full_length_identity_and_fallback(void) {
    passport_http_response_t r=response("bytes 10-99/100","\"same\"");
    assert(decide(&r,206,90,10,100,"\"same\"")==PASSPORT_HTTP_ACCEPT);
    assert(decide(&r,206,-1,10,100,"\"same\"")==PASSPORT_HTTP_ACCEPT);
    assert(decide(&r,206,89,10,100,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    assert(decide(&r,206,0,10,100,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    assert(decide(&r,206,90,11,100,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    assert(decide(&r,206,90,10,101,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    assert(decide(&r,206,90,10,0,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    assert(decide(&r,206,90,10,100,"\"changed\"")==PASSPORT_HTTP_RESTART_ZERO);
    assert(decide(&r,200,100,10,100,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    assert(decide(&r,416,0,10,100,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    assert(decide(&r,404,0,10,100,"\"same\"")==PASSPORT_HTTP_REJECT);
    r=response("bytes 10-49/100","\"same\"");
    assert(decide(&r,206,40,10,100,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
    r=response(NULL,"W/\"same\""); assert(decide(&r,200,100,0,0,NULL)==PASSPORT_HTTP_ACCEPT);
    assert(decide(&r,200,-1,0,0,NULL)==PASSPORT_HTTP_ACCEPT);
    assert(decide(&r,200,0,0,0,NULL)==PASSPORT_HTTP_ACCEPT);
    passport_http_header(&r,"Content-Encoding"," identity\t");
    assert(decide(&r,200,100,0,0,NULL)==PASSPORT_HTTP_ACCEPT);
    passport_http_header(&r,"Content-Encoding","gzip");
    assert(decide(&r,200,100,0,0,NULL)==PASSPORT_HTTP_REJECT);
    r=response("bytes 10-99/100",NULL);
    assert(decide(&r,206,90,10,100,"\"same\"")==PASSPORT_HTTP_RESTART_ZERO);
}
int main(void) {
    test_strict_range_and_duplicate_headers(); test_etag_strength_order_and_size();
    test_full_length_identity_and_fallback();
    puts("PASS HTTP strict range/ETag/full-resource validation and safe zero fallback");
    return 0;
}
