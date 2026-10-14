#!/usr/bin/env python3
"""Exercise production FM title/cache helpers with bounded NVS and lock mocks."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'main/passport_fm.c').read_text(encoding='utf-8')
def function(name):
    match = re.search(r'(?:static )?[^\n]*\b' + name + r'\(', SOURCE)
    start = match.start()
    brace = SOURCE.index('{', match.end())
    depth, i = 1, brace + 1
    while depth:
        depth += (SOURCE[i] == '{') - (SOURCE[i] == '}')
        i += 1
    return SOURCE[start:i]
CODE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "passport_fm_stream.h"
#include "passport_fm_metadata.h"
#define FM_CACHED_URI_BYTES 192
#define FM_RX_BYTES 1024
#define FM_URL_BYTES 768
#define ESP_OK 0
#define NVS_READONLY 0
typedef unsigned nvs_handle_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locks, unlocked, lock_depth, opens, queries, reads, closes;
static int open_result, query_result, read_result;
static uint8_t blob[10000];
static size_t blob_size;
#define taskENTER_CRITICAL(p) do {(void)(p);locks++;assert(!lock_depth++);} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);unlocked++;assert(lock_depth--==1);} while(0)
static int nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"gpp_fm")&&mode==NVS_READONLY);opens++;*h=42;return open_result;}
static int nvs_get_blob(nvs_handle_t h,const char *key,void *dst,size_t *size){assert(h==42&&!strcmp(key,"names"));if(!dst){queries++;*size=blob_size;return query_result;}reads++;if(read_result)return read_result;if(*size<blob_size)return -1;memcpy(dst,blob,blob_size);*size=blob_size;return 0;}
static void nvs_close(nvs_handle_t h){assert(h==42);closes++;}
static void memory_log(const char*p){(void)p;}
#define ESP_LOGI(...) ((void)0)
'''
CODE += SOURCE[SOURCE.index('typedef struct {'):SOURCE.index('static void memory_log')]
CODE += function('stopped') + '\n' + function('audio_sink')
CODE += r'''
static uint32_t decoded;
static size_t emitted;
static bool cancelled(uint32_t g){(void)g;return false;}
static bool emit(const uint8_t*d,size_t n,void*ctx){(void)d;(void)ctx;emitted+=n;return true;}
static uint32_t elapsed(void*ctx){(void)ctx;return decoded;}
static void names(const char*c,const char*n){char current[385],next[385];passport_fm_get_titles(current,sizeof(current),next,sizeof(next));assert(!strcmp(current,c)&&!strcmp(next,n));assert(!lock_depth&&locks==unlocked);}
static uint8_t table[256];
static size_t table_size;
static void make_table(void){const char*titles[]={"First","Second","Third"};unsigned off=2;table[0]=3;table[1]=0;for(unsigned i=0;i<3;i++){unsigned at=i*10,n=(unsigned)strlen(titles[i]);table[off++]=(uint8_t)at;table[off++]=0;table[off++]=0;table[off++]=0;table[off++]=(uint8_t)n;table[off++]=0;memcpy(table+off,titles[i],n);off+=n;}table_size=off;}
static void make_blob(void){memset(blob,0,sizeof(blob));fm_name_cache_t*c=(fm_name_cache_t*)blob;c->magic=FM_NAMES_MAGIC;c->identity=7;c->table_size=(uint32_t)table_size;strcpy(c->manifest_url,"https://a/b.m3u8");memcpy(c->table,table,table_size);blob_size=sizeof(*c)+table_size;open_result=query_result=read_result=0;}
static fm_context_t context(void){fm_context_t fm;memset(&fm,0,sizeof(fm));fm.names_ready=true;fm.duration_s=30;fm.cancelled=cancelled;fm.emit=emit;fm.elapsed=elapsed;fm.forwarding=true;return fm;}
int main(void){
 make_table();make_blob();fm_context_t fm=context();clear_names();names("","");
 fm_name_cache_t*c=read_names();assert(c&&c->table_size==table_size);free(c);assert(closes==opens);
 update_names(&fm,0);names("First","Second");assert(fm.next_at_s==10);
 unsigned read_before=reads;
 for(unsigned pos=0;pos<10;pos++)update_names(&fm,pos);
 assert(reads==read_before);names("First","Second");
 update_names(&fm,10);assert(reads==read_before+1);names("Second","Third");
 update_names(&fm,29);names("Third","First");assert(fm.next_at_s==30);
 update_names(&fm,0);names("First","Second");
 update_names(&fm,20);names("Third","First");
 update_names(&fm,5);names("First","Second"); /* backward position re-evaluates */
 char tiny[3],tiny_next[2];passport_fm_get_titles(tiny,sizeof(tiny),tiny_next,sizeof(tiny_next));assert(!strcmp(tiny,"Fi")&&!strcmp(tiny_next,"S"));passport_fm_get_titles(NULL,10,NULL,0);
 /* Network can prefetch arbitrarily far, but names follow decoded position. */
 fm.start_s=9;fm.last_position=0;fm.next_at_s=0;decoded=0;uint8_t bytes[8]={0};assert(audio_sink(&fm,bytes,sizeof(bytes)));names("First","Second");decoded=1;assert(audio_sink(&fm,bytes,sizeof(bytes)));names("Second","Third");decoded=21;assert(audio_sink(&fm,bytes,sizeof(bytes)));names("First","Second");assert(emitted==24);
 fm=context();fm.fallback_table=table;fm.fallback_size=table_size;read_before=reads;update_names(&fm,20);names("Third","First");assert(reads==read_before);
 fm.names_ready=false;clear_names();update_names(&fm,10);names("","");assert(reads==read_before);
 fm=context();open_result=-1;clear_names();update_names(&fm,10);names("","");assert(!read_names());make_blob();
 query_result=-1;assert(!read_names());make_blob();read_result=-1;assert(!read_names());make_blob();
 blob_size=sizeof(fm_name_cache_t)+1;read_before=reads;assert(!read_names()&&reads==read_before);make_blob();blob_size=sizeof(fm_name_cache_t)+PASSPORT_FM_METADATA_MAX_TABLE+1;assert(!read_names()&&reads==read_before);make_blob();
 ((fm_name_cache_t*)blob)->magic=0;assert(!read_names());make_blob();((fm_name_cache_t*)blob)->table_size++;assert(!read_names());make_blob();memset(((fm_name_cache_t*)blob)->manifest_url,'x',384);assert(!read_names());make_blob();
 /* Structurally valid header with a corrupted packed table never changes text. */
 fm=context();update_names(&fm,0);names("First","Second");((fm_name_cache_t*)blob)->table[0]=128;update_names(&fm,10);names("First","Second");make_blob();((fm_name_cache_t*)blob)->table[8]=0;update_names(&fm,10);names("First","Second");make_blob();
 clear_names();names("","");assert(name_identity("a",100,30)==name_identity("a",100,30));assert(name_identity("a",100,30)!=name_identity("b",100,30));assert(name_identity("a",100,30)!=name_identity("a",101,30));assert(name_identity("a",100,30)!=name_identity("a",100,31));
 assert(!lock_depth&&locks==unlocked);printf("FM names/cache PASS (%u NVS reads, %u balanced lock pairs)\n",reads,locks);return 0;
}
'''
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cjson-dir', type=Path)
    args = ap.parse_args()
    cc = shutil.which(os.environ.get('CC', 'gcc'))
    if not cc:
        raise SystemExit('gcc unavailable; set CC')
    candidates = [args.cjson_dir, Path(os.environ.get('IDF_PATH', '')) / 'components/json/cJSON', ROOT.parent / '.tools/esp-idf-v5.5.3/components/json/cJSON']
    cj = next((p for p in candidates if p and (p / 'cJSON.c').is_file()), None)
    if cj is None:
        raise SystemExit('cJSON unavailable; set IDF_PATH or --cjson-dir')
    build = ROOT / 'build/host'
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fm-names-', dir=build) as work:
        code = Path(work) / 'names.c'
        code.write_text(CODE, encoding='utf-8')
        exe = Path(work) / ('names.exe' if os.name == 'nt' else 'names')
        subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-I', str(ROOT / 'main'), '-I', str(cj), str(code), str(ROOT / 'main/passport_fm_metadata.c'), str(cj / 'cJSON.c'), '-lm', '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
if __name__ == '__main__':
    main()
