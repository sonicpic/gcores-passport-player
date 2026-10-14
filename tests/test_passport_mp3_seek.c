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
    // A 5645-byte ID3v2.4 tag is removed across arbitrary input chunks,
    // retaining its absolute offset without allocating a tag-size buffer.
    passport_mp3_prefix_t prefix={0}; size_t consumed;
    memset(b,0,sizeof(b)); memcpy(b,"ID3",3); b[3]=4;
    unsigned tag_size=5645-10;
    b[6]=(tag_size>>21)&127; b[7]=(tag_size>>14)&127;
    b[8]=(tag_size>>7)&127; b[9]=tag_size&127;
    assert(passport_mp3_prefix_step(&prefix,b,9,83828613,&consumed)==PASSPORT_MP3_NEED_MORE && !consumed);
    assert(passport_mp3_prefix_step(&prefix,b,1000,83828613,&consumed)==PASSPORT_MP3_CONSUMED && consumed==1000);
    assert(passport_mp3_prefix_step(&prefix,b,2048,83828613,&consumed)==PASSPORT_MP3_CONSUMED && consumed==2048);
    assert(passport_mp3_prefix_step(&prefix,b,2048,83828613,&consumed)==PASSPORT_MP3_CONSUMED && consumed==2048);
    assert(passport_mp3_prefix_step(&prefix,b,1000,83828613,&consumed)==PASSPORT_MP3_CONSUMED && consumed==549);
    assert(prefix.offset==5645 && !prefix.remaining);
    fixture(b,true);
    assert(passport_mp3_prefix_step(&prefix,b,417,83828613,&consumed)==PASSPORT_MP3_READY && !consumed);
    // Exact encoded scanning accepts VBR, accumulates actual frame samples,
    // retains partial frames, and stops one second before the requested time.
    passport_mp3_scan_t scan={0};
    assert(passport_mp3_scan_step(&scan,b,3,3,&consumed)==PASSPORT_MP3_NEED_MORE);
    assert(passport_mp3_scan_step(&scan,b,416,3,&consumed)==PASSPORT_MP3_NEED_MORE && !consumed && !scan.samples);
    for (unsigned frame_no=0;frame_no<77;++frame_no) {
        b[2]=frame_no%2 ? 0xb0 : 0x90; // 192/128 kbit/s, both 1152 samples.
        passport_mp3_frame_t f; assert(passport_mp3_header(b,&f));
        assert(passport_mp3_scan_step(&scan,b,f.bytes,3,&consumed)==PASSPORT_MP3_CONSUMED && consumed==f.bytes);
    }
    assert(scan.samples==77U*1152U && scan.sample_rate==44100);
    assert(passport_mp3_scan_step(&scan,b,2048,3,&consumed)==PASSPORT_MP3_READY && !consumed);
    assert(scan.samples>=2U*44100 && scan.samples<3U*44100);
    b[2]=0x94; // Timebase changed to 48 kHz: reject rather than mix clocks.
    assert(passport_mp3_scan_step(&scan,b,2048,3,&consumed)==PASSPORT_MP3_INVALID);
    b[0]=0;
    assert(passport_mp3_scan_step(&scan,b,2048,3,&consumed)==PASSPORT_MP3_INVALID);
    // Invalid/truncated ID3 is rejected; a new session resets all state.
    prefix=(passport_mp3_prefix_t){0}; memcpy(b,"ID3",3); b[3]=4; b[6]=128;
    assert(passport_mp3_prefix_step(&prefix,b,10,83828613,&consumed)==PASSPORT_MP3_INVALID);
    prefix=(passport_mp3_prefix_t){0}; b[6]=0; b[7]=0; b[8]=1; b[9]=0;
    assert(passport_mp3_prefix_step(&prefix,b,10,100,&consumed)==PASSPORT_MP3_INVALID);
    // No TOC: RSS duration proportional mapping with large ID3 excluded.
    fixture(b,true);
    assert(passport_mp3_seek_proportional(&s,b,417,5645,83828613,5238));
    assert(s.valid && !s.cbr && !s.has_toc && s.proportional_seconds==5238);
    assert(passport_mp3_seek_plan(&s,1236,&p));
    assert(p.offset>19700000 && p.offset<19800000 && p.preroll_samples==44100);
    assert(p.approximate && p.approximate_seconds==1236);
    assert(passport_mp3_seek_plan(&s,UINT32_MAX,&p) && p.offset<83828613);
    assert(passport_mp3_seek_plan(&s,0,&p) && p.offset==5645 && !p.preroll_samples);
    assert(!passport_mp3_seek_proportional(&s,b,417,5645,83828613,0));
    assert(!passport_mp3_seek_proportional(&s,b,417,5645,UINT64_MAX,5238));
    assert(!passport_mp3_seek_proportional(&s,b,3,5645,83828613,5238));
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
