from pathlib import Path
import argparse
import os
import shlex
import shutil
import subprocess
import re
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description='Host checks for production FM clock and compressed AAC positioning')
parser.add_argument('--ts', type=Path, help='Optional already-downloaded MPEG-TS fixture; never fetches the network')
args = parser.parse_args()
source = (root / 'main/passport_fm.c').read_text(encoding='utf-8')
context = source[source.index('typedef struct {'):source.index('/* A single replaceable')]
def function(name):
    start = re.search(r'static [^\n]*\b' + name + r'\(', source).start()
    brace = source.index('{', start)
    nesting, i = 1, brace + 1
    while nesting:
        nesting += (source[i] == '{') - (source[i] == '}')
        i += 1
    return source[start:i]
code = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "passport_fm_stream.h"
#define FM_RX_BYTES 1024
#define FM_URL_BYTES 768
#define FM_CACHED_URI_BYTES 192
#define ESP_LOGI(...) ((void)0)
static void memory_log(const char*x) {(void)x;}
'''+context+function('allowed_transport')+function('media_transport')+function('stopped')+function('publication_time')+function('station_position_ms')+'static void update_names(fm_context_t*f,uint32_t p){(void)f;(void)p;}\n'+function('audio_sink')+r'''
static bool cancel_now=false;
static uint8_t output[2*1024*1024], raw[2*1024*1024];
static size_t used;
static size_t raw_used;
static bool raw_sink(void*ctx,const uint8_t*d,size_t n){(void)ctx;assert(raw_used+n<=sizeof(raw));memcpy(raw+raw_used,d,n);raw_used+=n;return true;}
static const char *ts_fixture;
static void actual_ts(passport_fm_audio_sink_t sink,void*ctx){
 passport_fm_ts_t ts;passport_fm_ts_init(&ts,sink,ctx);
 FILE*fp=fopen(ts_fixture,"rb");assert(fp);uint8_t chunk[221];size_t n;
 while((n=fread(chunk,1,sizeof(chunk),fp)))assert(passport_fm_ts_feed(&ts,chunk,n));
 assert(passport_fm_ts_finish(&ts));fclose(fp);
}
static bool cancelled(uint32_t g) {(void)g;return cancel_now;}
static bool emit(const uint8_t*d,size_t n,void*ctx){(void)ctx;assert(used+n<=sizeof(output));memcpy(output+used,d,n);used+=n;return true;}
static void reset(fm_context_t*f,uint64_t skip){memset(f,0,sizeof(*f));f->cancelled=cancelled;f->emit=emit;f->skip_us=skip;used=0;cancel_now=false;}
int main(int argc,char **argv) {
 assert(allowed_transport("https://www.gcores.com/gapi/v1/radios"));
 assert(allowed_transport("http://alioss.gcores.com/uploads/fm_programs/7/a.ts"));
 assert(!allowed_transport("http://www.gcores.com/gapi/v1/radios"));
 assert(!allowed_transport("http://alioss.gcores.com.evil/uploads/fm_programs/a.ts"));
 assert(!allowed_transport("http://alioss.gcores.com/other/a.ts"));
 char media[]="https://alioss.gcores.com/uploads/fm_programs/7/a.ts";
 media_transport(media);assert(!strcmp(media,"http://alioss.gcores.com/uploads/fm_programs/7/a.ts"));
 char api[]="https://www.gcores.com/gapi/v1/radios";media_transport(api);
 assert(!strcmp(api,"https://www.gcores.com/gapi/v1/radios"));
 assert(publication_time("2026-10-08T18:00:00.000+08:00")==INT64_C(1791453600));
 assert(publication_time("2026-10-08T18:00:00.000+08:00")==publication_time("2026-10-08T10:00:00Z"));
 assert(publication_time("2026-10-08T08:00:00-02:00")==publication_time("2026-10-08T10:00:00Z"));
 assert(publication_time("2026-10-08T18:00:00")==0);
 assert(publication_time("2026-10-08T18:00:00.000+80:00")==0);
 assert(publication_time("2026-10-08T18:00:00.000+08:00junk")==0);
 assert(publication_time(NULL)==0);
 assert(station_position_ms(INT64_C(1791453600)+214582+120,INT64_C(1791453600),214582)==120000);
 assert(station_position_ms(INT64_C(1791453600),INT64_C(1791453600),0)==0);
 assert(station_position_ms(0,INT64_C(1791453600),214582)==0);
 assert(station_position_ms(INT64_C(1791453600)-1,INT64_C(1791453600),214582)==0);
 assert(station_position_ms(INT64_C(1791453600),0,214582)==0);
 /* ADTS AAC-LC 44100 stereo,20-byteframe. Feed every byte separately. */
 uint8_t frame[20]={0xff,0xf1,0x50,0x80,0x02,0x9f,0xfc};
 memset(frame+7,0x55,13);
 fm_context_t f;
 reset(&f, 46438); /* two complete frames23.219ms each */
 for(unsigned j=0;j<4;++j)for(size_t i=0;i<sizeof(frame);++i)assert(audio_sink(&f,frame+i,1));
 assert(used==40 && !memcmp(output,frame,20) && !memcmp(output+20,frame,20));
 reset(&f,0);
 assert(audio_sink(&f,frame,sizeof(frame)) && used==20 && !memcmp(output,frame,20));
 reset(&f,1000000);cancel_now=true;assert(!audio_sink(&f,frame,sizeof(frame)) && used==0);
 reset(&f,1000000);frame[0]=0;assert(!audio_sink(&f,frame,sizeof(frame)));frame[0]=0xff;
 if(argc>1){
 ts_fixture=argv[1];actual_ts(raw_sink,NULL);
 uint64_t skips[]={0,1000000,59000000};
 for(unsigned k=0;k<3;++k){
   size_t offset=0;uint64_t remaining=skips[k];
   while(offset+7<=raw_used){
      uint8_t*h=raw+offset;assert(h[0]==0xff && ((h[2]>>2)&15)==4);
      size_t len=((size_t)(h[3]&3)<<11)|((size_t)h[4]<<3)|(h[5]>>5);
      uint64_t us=UINT64_C(1024000000)*(1+(h[6]&3))/44100;
      if(remaining<us) break;
      remaining-=us;offset+=len;
   }
   reset(&f,skips[k]);actual_ts(audio_sink,&f);
   assert(used==raw_used-offset && !memcmp(output,raw+offset,used));
 }
 }
 printf("FMclient production timestamp/ADTS skip PASS; context=%zu\n",sizeof(f));
 return 0;
}
'''
compiler_text = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
if not compiler_text:
    raise SystemExit('Host C compiler missing: set CC or put gcc/cc on PATH')
compiler = [compiler_text] if Path(compiler_text).is_file() else shlex.split(compiler_text)
build = root / 'build/host'
build.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fm-client-', dir=build) as temporary:
    directory = Path(temporary)
    test = directory / 'client.c'
    binary = directory / ('client.exe' if os.name == 'nt' else 'client')
    test.write_text(code, encoding='utf-8')
    subprocess.run(compiler + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(root/'main'),
                              str(test), str(root/'main/passport_fm_stream.c'), '-o', str(binary)], check=True)
    command = [str(binary)] + ([str(args.ts.resolve())] if args.ts else [])
    subprocess.run(command, cwd=root, check=True)
