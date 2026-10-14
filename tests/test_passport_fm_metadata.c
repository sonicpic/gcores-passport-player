#include "passport_fm_metadata.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t live, peak;
static void *tracked_malloc(size_t n){size_t*p=malloc(n+sizeof(size_t));if(!p)return NULL;*p=n;live+=n;if(live>peak)peak=live;return p+1;}
static void tracked_free(void*v){if(v){size_t*p=(size_t*)v-1;live-=*p;free(p);}}
static passport_fm_metadata_t parser;
static uint8_t table[8192];
static const char *base="{\"data\":{\"type\":\"radios\",\"attributes\":{\"duration\":100,\"published-at\":\"2026-01-01T00:00:00Z\"}},\"included\":[{\"type\":\"medias\",\"attributes\":{\"audio\":\"https://a/b\"}},{\"type\":\"timelines\",\"attributes\":{\"at\":0,\"title\":\"A { x } [ ] \\\" unicode \\uD83D\\uDE00\"}},{\"type\":\"timelines\",\"attributes\":{\"at\":50,\"title\":\"B\"}}]}";
static bool parse(const char*s,size_t n,size_t chunk){passport_fm_metadata_init(&parser,table,sizeof(table));for(size_t i=0;i<n;i+=chunk){size_t z=n-i<chunk?n-i:chunk;if(!passport_fm_metadata_feed(&parser,(const uint8_t*)s+i,z))return false;}return passport_fm_metadata_finish(&parser);}
static void reject(const char*s){assert(!parse(s,strlen(s),1));assert(live==0);}
static void replace_reject(const char*old,const char*replacement){char b[4096];const char*at=strstr(base,old);assert(at);size_t n=(size_t)(at-base);memcpy(b,base,n);strcpy(b+n,replacement);strcat(b,at+strlen(old));reject(b);}
int main(int argc,char**argv){
 passport_fm_program_t out[3];
 cJSON_Hooks hooks={tracked_malloc,tracked_free};cJSON_InitHooks(&hooks);
 for(size_t chunk=1;chunk<70;chunk++){assert(parse(base,strlen(base),chunk));assert(live==0);assert(parser.count==2);passport_fm_program_t out[3];assert(passport_fm_metadata_lookup(table,parser.used,49,out));assert(out[0].at_s==0&&out[1].at_s==50&&out[2].at_s==0);assert(strstr(out[0].title,"unicode"));assert(passport_fm_metadata_lookup(table,parser.used,50,out)&&!strcmp(out[0].title,"B"));}
 for(size_t n=0;n<strlen(base);n++)assert(!parse(base,n,7));
 replace_reject("\"duration\":100","\"duration\":100.");replace_reject("\"duration\":100","\"duration\":1e+");reject("{}");reject("{]");reject("{\"x\":[1,]}");reject("{\"x\":{\"a\":1,}}");reject("{\"x\":true false}");reject("{\"x\":01}");reject("{\"x\":NaN}");
 replace_reject("\\uD83D\\uDE00","\\uD83Dx");replace_reject("\\uD83D\\uDE00","\\uDE00");replace_reject("\\uD83D\\uDE00","\\uD83D\\u0001");replace_reject("\\uD83D\\uDE00","\\u0000");replace_reject("\\uD83D\\uDE00","\\z");replace_reject("\\uD83D\\uDE00","\xC0\x80");replace_reject("\"at\":50","\"at\":0");replace_reject("\"at\":50","\"at\":100");replace_reject("\"at\":50","\"at\":1.5");
 char many[16000];size_t many_used=(size_t)snprintf(many,sizeof(many),"{\"data\":{\"type\":\"radios\",\"attributes\":{\"duration\":2000,\"published-at\":\"2026-01-01T00:00:00Z\"}},\"included\":[{\"type\":\"medias\",\"attributes\":{\"audio\":\"https://a/b\"}}");
 for(unsigned i=0;i<129;i++){
  many_used+=(size_t)snprintf(many+many_used,sizeof(many)-many_used,",{\"type\":\"timelines\",\"attributes\":{\"at\":%u,\"title\":\"a\"}}",i);
  if(i==127){strcpy(many+many_used,"]}");assert(parse(many,many_used+2,7));assert(parser.count==128);assert(passport_fm_metadata_lookup(table,parser.used,127,out));assert(out[1].at_s==0);}
 }
 strcpy(many+many_used,"]}");reject(many);
 passport_fm_metadata_init(&parser,table,10);assert(!passport_fm_metadata_feed(&parser,(const uint8_t*)base,strlen(base)));
 assert(live==0);
 char large[600];memset(large,'a',sizeof(large));large[0]='"';large[sizeof(large)-2]='"';large[sizeof(large)-1]=0;replace_reject("\"B\"",large);
 assert(parse(base,strlen(base),7));size_t used=parser.used;assert(!passport_fm_metadata_lookup(table,used-1,0,out));assert(!passport_fm_metadata_lookup(table,used+1,0,out));
 uint8_t saved=table[8];table[8]=0;assert(!passport_fm_metadata_lookup(table,used,0,out));table[8]=0xFF;assert(!passport_fm_metadata_lookup(table,used,0,out));table[8]=saved;
 if(argc>1){FILE*f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long len=ftell(f);assert(len>0);rewind(f);char*s=malloc((size_t)len);assert(s&&fread(s,1,(size_t)len,f)==(size_t)len);fclose(f);size_t chunks[]={1,7,64,257};for(unsigned k=0;k<4;k++){assert(parse(s,(size_t)len,chunks[k]));assert(parser.count==44&&parser.duration_s==214582);for(uint32_t sec=0;sec<parser.duration_s;sec++){assert(passport_fm_metadata_lookup(table,parser.used,sec,out));assert(out[0].at_s<=sec);assert(out[1].at_s>sec||out[1].at_s==0);}assert(passport_fm_metadata_lookup(table,parser.used,214581,out));assert(out[1].at_s==0);printf("real fixture count=%u packed=%zu\n",parser.count,parser.used);}free(s);}
 assert(live==0);printf("FM metadata PASS parser=%zu temporary cJSON peak=%zu lookup output=%zu\n",sizeof(parser),peak,sizeof(out));return 0;
}
