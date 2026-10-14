/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "history_test_stubs.h"
#include "passport_history.h"
static unsigned char slots[32][80];
static size_t sizes[32];
static unsigned writes, commits;
static int fail_set, fail_commit;
static unsigned slot(const char *key) { unsigned n = 99; assert(sscanf(key,"h%u",&n)==1 && n<32); return n; }
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out) { assert(!strcmp(name,"gpp_history")&&mode==1); *out=1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) {
    assert(h==1); unsigned i=slot(key); if (!sizes[i]) return ESP_ERR_NVS_NOT_FOUND;
    if (*size<sizes[i]) { *size=sizes[i]; return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(out,slots[i],sizes[i]);*size=sizes[i];return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size) {
    assert(h==1&&size==64); ++writes;if(fail_set)return ESP_FAIL;
    memcpy(slots[slot(key)],in,size);sizes[slot(key)]=size;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h==1); ++commits; return fail_commit?ESP_FAIL:ESP_OK; }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *m) { return m; }
int xSemaphoreTake(SemaphoreHandle_t m,uint32_t timeout) { assert(m&&timeout==UINT32_MAX);return 1; }
int xSemaphoreGive(SemaphoreHandle_t m) { assert(m);return 1; }
void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->hash=2166136261u; }
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int is224) { assert(!is224);(void)c;return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *in,size_t size) { while(size--)c->hash=(c->hash^*in++)*16777619u;return 0; }
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char *out) { for(unsigned i=0;i<32;i++)out[i]=(unsigned char)(c->hash>>(8*(i%4)));return 0; }
void mbedtls_sha256_free(mbedtls_sha256_context *c) { (void)c; }
static void expect(int channel,const char *guid,uint32_t pos,bool completed) { passport_history_progress_t p;assert(passport_history_lookup(channel,guid,&p)&&p.position_seconds==pos&&p.completed==completed); }
int main(void) {
    passport_history_progress_t p={777,888,true};
    assert(!passport_history_lookup(0,"x",&p)&&p.position_seconds==777);
    assert(passport_history_init()==ESP_OK&&passport_history_init()==ESP_OK);
    assert(passport_history_save(-1,"x",0,0,false)==ESP_ERR_INVALID_ARG);
    assert(passport_history_save(5,"x",0,0,false)==ESP_ERR_INVALID_ARG);
    assert(passport_history_save(0,"",0,0,false)==ESP_ERR_INVALID_ARG);
    assert(passport_history_save(0,"x",101,100,false)==ESP_ERR_INVALID_ARG);
    assert(passport_history_save(0,"guid",10,100,false)==ESP_OK);expect(0,"guid",10,false);
    unsigned old=writes;assert(passport_history_save(0,"guid",10,100,false)==ESP_OK&&writes==old);
    assert(passport_history_save(1,"guid",20,100,false)==ESP_OK);expect(0,"guid",10,false);expect(1,"guid",20,false);
    fail_set=1;assert(passport_history_save(0,"guid",30,100,false)==ESP_FAIL);fail_set=0;expect(0,"guid",10,false);
    fail_commit=1;assert(passport_history_save(0,"guid",40,100,false)==ESP_FAIL);fail_commit=0;
    /* Interrupted write/commit allows complete old or complete new blob; a torn
       or corrupt blob is rejected, never surfaced as a partial position. */
    expect(0,"guid",40,false);slots[0][48]^=1;assert(!passport_history_lookup(0,"guid",&p));
    assert(passport_history_save(0,"guid",100,100,true)==ESP_OK);expect(0,"guid",100,true);
    sizes[2]=79;memset(slots[2],0,79); /* malformed oversize ignored/reused */
    for(unsigned i=0;i<30;i++) { char guid[32];snprintf(guid,sizeof(guid),"episode-%u",i);assert(passport_history_save(0,guid,i,100,false)==ESP_OK); }
    expect(0,"episode-0",0,false);expect(0,"episode-29",29,false);
    /* A changed save promotes guid; unchanged reads never write/promote. */
    assert(passport_history_save(0,"guid",99,100,false)==ESP_OK);
    assert(passport_history_save(0,"new-episode",1,100,false)==ESP_OK);
    assert(!passport_history_lookup(1,"guid",&p));expect(0,"guid",99,false);
    assert(passport_history_save(0,"new-episode-2",2,100,false)==ESP_OK);
    assert(!passport_history_lookup(0,"episode-0",&p));expect(0,"episode-29",29,false);
    unsigned count=0;for(unsigned i=0;i<32;i++)count+=sizes[i]!=0;assert(count==32);
    assert(commits<writes);puts("PASS: history identities,32-slot bound,LRU,dedup,completion,corruption,write failures");
    return 0;
}
