#include "passport_fm_metadata.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static uint16_t rd16(const uint8_t*p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t rd32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr16(uint8_t*p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void wr32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static bool number(cJSON *v,uint32_t*out){if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>UINT32_MAX||floor(v->valuedouble)!=v->valuedouble)return false;*out=(uint32_t)v->valuedouble;return true;}
static bool copy_string(cJSON *v,char*out,size_t cap){if(!cJSON_IsString(v)||!v->valuestring)return false;size_t n=strlen(v->valuestring);if(!n||n>=cap)return false;memcpy(out,v->valuestring,n+1);return true;}
static bool resource(passport_fm_metadata_t*p){
 p->object[p->object_used]=0;const char*end=NULL;
 cJSON*r=cJSON_ParseWithOpts(p->object,&end,1);if(!r)return false;
 cJSON*t=cJSON_GetObjectItemCaseSensitive(r,"type"),*a=cJSON_GetObjectItemCaseSensitive(r,"attributes");bool ok=true;
 if(cJSON_IsString(t)&&cJSON_IsObject(a)){
  if(!strcmp(t->valuestring,"timelines")){
   uint32_t at; cJSON*v=cJSON_GetObjectItemCaseSensitive(a,"title");
   ok=number(cJSON_GetObjectItemCaseSensitive(a,"at"),&at)&&cJSON_IsString(v)&&v->valuestring;
   size_t n=ok?strlen(v->valuestring):0;
   ok=ok&&n&&n<=PASSPORT_FM_TITLE_BYTES&&p->count<PASSPORT_FM_METADATA_MAX_ENTRIES&&(!p->count||at>p->last_at)&&p->used+6+n<=p->capacity;
   if(ok){wr32(p->table+p->used,at);wr16(p->table+p->used+4,(uint16_t)n);memcpy(p->table+p->used+6,v->valuestring,n);p->used+=6+n;p->last_at=at;wr16(p->table,++p->count);}
  }else if(!strcmp(t->valuestring,"medias")){
   cJSON*v=cJSON_GetObjectItemCaseSensitive(a,"audio");if(v)ok=copy_string(v,p->audio_url,sizeof(p->audio_url));
   v=cJSON_GetObjectItemCaseSensitive(a,"duration");if(ok&&v)ok=number(v,&p->duration_s);
  }else if(!strcmp(t->valuestring,"radios")){
   cJSON*v=cJSON_GetObjectItemCaseSensitive(a,"published-at");if(v)ok=copy_string(v,p->published_at,sizeof(p->published_at));
   v=cJSON_GetObjectItemCaseSensitive(a,"duration");if(ok&&v)ok=number(v,&p->duration_s);
  }
 }
 cJSON_Delete(r);return ok;
}
void passport_fm_metadata_init(passport_fm_metadata_t*p,uint8_t*t,size_t cap){memset(p,0,sizeof(*p));p->table=t;p->capacity=cap>PASSPORT_FM_METADATA_MAX_TABLE?PASSPORT_FM_METADATA_MAX_TABLE:cap;p->used=2;if(!t||cap<2)p->failed=true;else wr16(t,0);}
/* Frame states: object key/end=0, colon=1, value=2, comma/end=3,
 * key after comma=4; array value/end=0, comma/end=3, value after comma=2. */
static bool value_done(passport_fm_metadata_t*p){if(!p->depth)return false;unsigned i=p->depth-1;uint8_t s=p->frames[i].state;if(p->frames[i].type=='{'?s!=2:(s!=0&&s!=2))return false;p->frames[i].state=3;return true;}
static bool primitive_valid(const char *s) {
 if (!strcmp(s,"true") || !strcmp(s,"false") || !strcmp(s,"null")) return true;
 if (*s=='-') s++;
 if (*s=='0') s++;
 else { if (*s<'1'||*s>'9') return false; while (*s>='0'&&*s<='9') s++; }
 if (*s=='.') { s++; if (*s<'0'||*s>'9') return false; while (*s>='0'&&*s<='9') s++; }
 if (*s=='e'||*s=='E') { s++; if (*s=='+'||*s=='-') s++; if (*s<'0'||*s>'9') return false; while (*s>='0'&&*s<='9') s++; }
 return !*s;
}
static bool primitive_done(passport_fm_metadata_t*p){
 p->primitive[p->primitive_used]=0;
 bool ok=primitive_valid(p->primitive);
 p->token=0;p->primitive_used=0;return ok&&value_done(p);
}
static int hex(unsigned c){if(c>='0'&&c<='9')return (int)c-'0';if(c>='a'&&c<='f')return (int)c-'a'+10;if(c>='A'&&c<='F')return (int)c-'A'+10;return -1;}
static bool string_byte(passport_fm_metadata_t*p,unsigned c){
 if(p->hex_left){int h=hex(c);if(h<0)return false;p->unicode=(uint16_t)((p->unicode<<4)|h);if(!--p->hex_left){
  if(p->low_required){if(p->unicode<0xDC00||p->unicode>0xDFFF)return false;p->low_required=false;p->high_surrogate=false;}
  else if(p->unicode>=0xD800&&p->unicode<=0xDBFF)p->high_surrogate=true;
  else if(p->unicode>=0xDC00&&p->unicode<=0xDFFF)return false;
  if(!p->unicode)return false; /* cJSON strings cannot represent embedded NUL. */
 }return true;}
 if(p->escape){p->escape=0;if(c=='u'){p->hex_left=4;p->unicode=0;return true;}if(p->low_required)return false;return c=='"'||c=='\\'||c=='/'||c=='b'||c=='f'||c=='n'||c=='r'||c=='t';}
 if(p->utf_left){if(c<p->utf_min||c>p->utf_max)return false;p->utf_left--;p->utf_min=0x80;p->utf_max=0xBF;return true;}
 if(p->high_surrogate){if(c!='\\')return false;p->escape=1;p->low_required=true;return true;}
 if(c=='\\'){p->escape=1;return true;}
 if(c=='"'){
  p->token=0;unsigned i=p->depth-1;
  if(p->frames[i].type=='{'&&(p->frames[i].state==0||p->frames[i].state==4)){
   p->key[p->key_used]=0;p->frames[i].field=!strcmp(p->key,"data")?1:!strcmp(p->key,"included")?2:0;p->frames[i].state=1;return true;
  }return value_done(p);
 }
 if(c<0x20)return false;
 if(c>=0x80){p->utf_min=0x80;p->utf_max=0xBF;if(c>=0xC2&&c<=0xDF)p->utf_left=1;else if(c>=0xE0&&c<=0xEF){p->utf_left=2;if(c==0xE0)p->utf_min=0xA0;if(c==0xED)p->utf_max=0x9F;}else if(c>=0xF0&&c<=0xF4){p->utf_left=3;if(c==0xF0)p->utf_min=0x90;if(c==0xF4)p->utf_max=0x8F;}else return false;}
 if(p->key_used<sizeof(p->key)-1)p->key[p->key_used++]=(char)c;
 return true;
}
static bool byte(passport_fm_metadata_t*p,unsigned c){
 if(p->capture_depth){if(p->object_used>=sizeof(p->object)-1)return false;p->object[p->object_used++]=(char)c;}
 if(p->token==1)return string_byte(p,c);
 if(p->token==2){if(c!=','&&c!=']'&&c!='}'&&c!=' '&&c!='\t'&&c!='\r'&&c!='\n'){if(p->primitive_used>=sizeof(p->primitive)-1)return false;p->primitive[p->primitive_used++]=(char)c;return true;}if(!primitive_done(p))return false;}
 if(c==' '||c=='\t'||c=='\r'||c=='\n')return true;
 if(p->root_done)return false;
 if(!p->depth){if(p->root_started||c!='{')return false;p->root_started=true;p->frames[p->depth++].type='{';return true;}
 unsigned i=p->depth-1;uint8_t s=p->frames[i].state,t=p->frames[i].type;
 if(c=='"'){if(t=='{'?(s!=0&&s!=4&&s!=2):(s!=0&&s!=2))return false;p->token=1;p->key_used=0;return true;}
 if(c==':'){if(t!='{'||s!=1)return false;p->frames[i].state=2;return true;}
 if(c==','){if(s!=3)return false;p->frames[i].state=t=='{'?4:2;return true;}
 if(c=='}'||c==']'){
  if((c=='}'&&t!='{')||(c==']'&&t!='[')||(s!=0&&s!=3))return false;
  if(p->capture_depth==p->depth){if(!resource(p))return false;p->capture_depth=0;p->object_used=0;}
  p->depth--;if(!p->depth){p->root_done=true;return true;}return value_done(p);
 }
 if(t=='{'?s!=2:(s!=0&&s!=2))return false;
 if(c=='{'||c=='['){
  if(p->depth>=sizeof(p->frames)/sizeof(p->frames[0]))return false;
  bool capture=c=='{'&&!p->capture_depth&&((p->depth==1&&p->frames[i].field==1)||(p->depth==2&&t=='['&&p->frames[i].field==2));
  unsigned field=p->depth==1?p->frames[i].field:0;p->frames[p->depth].type=(uint8_t)c;p->frames[p->depth].state=0;p->frames[p->depth].field=(uint8_t)field;p->depth++;
  if(capture){p->capture_depth=p->depth;p->object_used=1;p->object[0]='{';}return true;
 }
 p->token=2;p->primitive_used=1;p->primitive[0]=(char)c;return true;
}
bool passport_fm_metadata_feed(passport_fm_metadata_t*p,const uint8_t*d,size_t n){if(!p||p->failed||(!d&&n))return false;for(size_t i=0;i<n;i++)if(!byte(p,d[i])){p->failed=true;return false;}return true;}
bool passport_fm_metadata_finish(passport_fm_metadata_t*p){return p&&!p->failed&&p->root_done&&!p->depth&&!p->token&&p->count&&p->audio_url[0]&&p->published_at[0]&&p->duration_s&&p->last_at<p->duration_s;}
static bool title_valid(const uint8_t *p, size_t n) {
 unsigned left=0,min=0x80,max=0xBF;
 for(size_t i=0;i<n;i++){
  unsigned c=p[i];
  if(left){if(c<min||c>max)return false;left--;min=0x80;max=0xBF;continue;}
  if(!c)return false;
  if(c<0x80)continue;
  min=0x80;max=0xBF;
  if(c>=0xC2&&c<=0xDF)left=1;
  else if(c>=0xE0&&c<=0xEF){left=2;if(c==0xE0)min=0xA0;if(c==0xED)max=0x9F;}
  else if(c>=0xF0&&c<=0xF4){left=3;if(c==0xF0)min=0x90;if(c==0xF4)max=0x8F;}
  else return false;
 }
 return !left;
}
bool passport_fm_metadata_lookup(const uint8_t*t,size_t n,uint32_t pos,passport_fm_program_t out[3]){
 if(!t||!out||n<2||n>PASSPORT_FM_METADATA_MAX_TABLE)return false;
 unsigned count=rd16(t);if(!count||count>PASSPORT_FM_METADATA_MAX_ENTRIES)return false;
 size_t off=2;uint32_t last=0;unsigned current=0;
 for(unsigned i=0;i<count;i++){
  if(off+6>n)return false;
  uint32_t at=rd32(t+off);uint16_t len=rd16(t+off+4);
  if(!len||len>PASSPORT_FM_TITLE_BYTES||off+6+len>n||(i&&at<=last))return false;
  if(!title_valid(t+off+6,len))return false;
  last=at;if(at<=pos)current=i;off+=6+len;
 }
 if(off!=n)return false;
 off=2;
 for(unsigned i=0;i<count;i++){
  uint16_t len=rd16(t+off+4);
  for(unsigned j=0;j<3;j++)if(i==(current+j)%count){
   out[j].at_s=rd32(t+off);memcpy(out[j].title,t+off+6,len);out[j].title[len]=0;
  }
  off+=6+len;
 }
 return true;
}
