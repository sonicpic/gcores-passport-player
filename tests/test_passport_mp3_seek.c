#include "passport_mp3_seek.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(unsigned char *p, unsigned v) {
    p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v;
}
static void fixture(unsigned char *b, bool cbr) {
    memset(b,0,2048); b[0]=255; b[1]=251; b[2]=144;
    memcpy(b+36,cbr ? "Info" : "Xing",4); put32(b+40,15);
    put32(b+44,238419); put32(b+48,99649827);
    for (unsigned i=0;i<100;++i) b[52+i]=(unsigned char)(i*256/100);
    memcpy(b+417,b,4); memcpy(b+834,b,4);
}
int main(int argc, char **argv) {
    _Static_assert(sizeof(passport_mp3_seek_t)<=128,"small seek metadata");
    unsigned char b[2048]; passport_mp3_seek_t s; passport_mp3_seek_plan_t p;
    fixture(b,true);
    assert(passport_mp3_seek_parse(&s,b,417,120,99649947));
    assert(s.cbr && s.has_toc && s.frames==238419);
    assert(passport_mp3_seek_plan(&s,3000,&p));
    assert(p.offset>47900000 && p.offset<48100000 && p.preroll_samples==44100);
    assert(p.approximate && p.approximate_seconds==3000);
    assert(passport_mp3_seek_plan(&s,UINT32_MAX,&p));
    assert(p.offset<99649947 && p.approximate_seconds==6228);
    assert(passport_mp3_seek_plan(&s,0,&p) && p.offset==120 && p.preroll_samples==0);
    size_t sync=99;
    assert(passport_mp3_seek_sync(b,838,44100,&sync) && sync==0);
    assert(!passport_mp3_seek_sync(b,837,44100,&sync));
    b[834]=0; assert(!passport_mp3_seek_sync(b,838,44100,&sync));
    fixture(b,false); assert(passport_mp3_seek_parse(&s,b,417,120,99649947));
    assert(passport_mp3_seek_plan(&s,3000,&p) && p.offset>47500000 && p.offset<48500000);
    b[80]=0; assert(!passport_mp3_seek_parse(&s,b,417,120,99649947));
    fixture(b,false); put32(b+40,3); assert(!passport_mp3_seek_parse(&s,b,417,120,99649947));
    fixture(b,true); assert(!passport_mp3_seek_parse(&s,b,3,120,99649947));
    assert(!passport_mp3_seek_parse(&s,b,416,120,99649947));
    assert(!passport_mp3_seek_parse(&s,b,417,120,99649946));
    assert(!passport_mp3_seek_parse(&s,b,417,120,99650100));
    /* CRC-protected mono Info moves by CRC + 17-byte side information. */
    fixture(b,true); b[1]=250; b[3]=192; memmove(b+23,b+36,120);
    assert(passport_mp3_seek_parse(&s,b,417,120,99649947));
    fixture(b,false); memset(b+52,0,100);
    assert(!passport_mp3_seek_parse(&s,b,417,120,99649947));
    fixture(b,true); b[3]=2;
    passport_mp3_frame_t frame;
    assert(!passport_mp3_header(b,&frame));
    fixture(b,true); put32(b+44,10000000); put32(b+48,4170000000U);
    assert(passport_mp3_seek_parse(&s,b,417,0,4170000000U));
    assert(passport_mp3_seek_plan(&s,260000,&p));
    assert(p.offset>4150437000U && p.offset<4150437200U);
    put32(b+44,UINT32_MAX); put32(b+48,417);
    assert(!passport_mp3_seek_parse(&s,b,417,0,417));
    if (argc>1) {
        FILE *f=fopen(argv[1],"rb"); assert(f); unsigned char head[32768];
        size_t count=fread(head,1,sizeof(head),f); fclose(f);
        assert(count==sizeof(head));
        assert(passport_mp3_seek_parse(&s,head+120,count-120,120,99649947));
        assert(s.frames==238419 && s.audio_bytes==99649827 && s.cbr);
        assert(passport_mp3_seek_sync(head+120,count-120,44100,&sync) && sync==0);
    }
    puts("PASS MP3 Info/Xing approximate planning, bounds and three-frame sync");
    return 0;
}
