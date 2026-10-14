#include "passport_mp3_seek.h"
#include <limits.h>
#include <string.h>

bool passport_mp3_header(const uint8_t b[4], passport_mp3_frame_t *f) {
    static const uint16_t rates[3][3]={{11025,12000,8000},{22050,24000,16000},{44100,48000,32000}};
    static const uint16_t br1[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const uint16_t br2[16]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
    unsigned version=(b[1]>>3)&3, layer=(b[1]>>1)&3, rate=(b[2]>>2)&3, bitrate=b[2]>>4;
    if (b[0]!=0xff || (b[1]&0xe0)!=0xe0 || version==1 || layer!=1 || rate==3 || !bitrate || bitrate==15 || (b[3]&3)==2)
        return false; // Layer III only; free-format and mixed layers are not indexed.
    unsigned ri=version==3 ? 2 : version==2 ? 1 : 0;
    uint32_t hz=rates[ri][rate], bits=(version==3 ? br1[bitrate] : br2[bitrate])*1000U;
    uint16_t size=(version==3 ? 144U : 72U)*bits/hz+((b[2]>>1)&1);
    uint8_t channels=(b[3]>>6)==3 ? 1 : 2, crc=(b[1]&1) ? 0 : 2;
    uint8_t side=version==3 ? (channels==1 ? 17 : 32) : (channels==1 ? 9 : 17);
    if (size<=4U+crc+side) return false;
    *f=(passport_mp3_frame_t){.bytes=size,.main_data_bytes=size-4U-crc-side,
        .samples=version==3 ? 1152 : 576,.sample_rate=hz,.bitrate=bits,
        .version=version,.channels=channels,.crc_bytes=crc,.side_bytes=side};
    return true;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

bool passport_mp3_seek_parse(passport_mp3_seek_t *s, const uint8_t *b,
                             size_t size, uint32_t offset, uint64_t total) {
    memset(s,0,sizeof(*s));
    passport_mp3_frame_t f;
    if (!b || size<4 || !passport_mp3_header(b,&f) || size<f.bytes) return false;
    size_t at=4U+f.crc_bytes+f.side_bytes;
    if (at+8U>f.bytes) return false;
    bool cbr=!memcmp(b+at,"Info",4);
    if (!cbr && memcmp(b+at,"Xing",4)) return false;
    uint32_t flags=be32(b+at+4);
    if ((flags&~15U) || (flags&3U)!=3U) return false;
    at+=8;
    if (at+8U>f.bytes) return false;
    uint32_t frames=be32(b+at), audio_bytes=be32(b+at+4); at+=8;
    if (!frames || frames>audio_bytes/24U || audio_bytes<f.bytes || total>UINT32_MAX ||
        (uint64_t)offset+audio_bytes>total || total-offset-audio_bytes>128U) return false;
    /* Allow a final ID3v1 tag, reject a stale/truncated resource length. */
    if (flags&4U) {
        if (at+100U>f.bytes) return false;
        for (unsigned i=1;i<100;++i) if (b[at+i]<b[at+i-1]) return false;
        if (b[at]!=0 || (!cbr && !b[at+99])) return false;
        memcpy(s->toc,b+at,100); at+=100;
        s->has_toc=true;
    }
    if ((flags&8U) && at+4U>f.bytes) return false;
    if (!cbr && !s->has_toc) { memset(s,0,sizeof(*s)); return false; }
    s->audio_offset=offset; s->audio_bytes=audio_bytes; s->frames=frames;
    s->sample_rate=f.sample_rate; s->frame_samples=f.samples; s->cbr=cbr; s->valid=true;
    return true;
}

bool passport_mp3_seek_plan(const passport_mp3_seek_t *s, uint32_t seconds,
                            passport_mp3_seek_plan_t *p) {
    if (!s || !p || !s->valid || !s->frames || !s->sample_rate || !s->audio_bytes ||
        (s->frame_samples!=576 && s->frame_samples!=1152) || s->frames>s->audio_bytes/24U ||
        (!s->cbr && !s->has_toc)) return false;
    uint64_t samples=(uint64_t)s->frames*s->frame_samples;
    uint64_t target=(uint64_t)seconds*s->sample_rate;
    if (target>=samples) target=samples-1;
    /* One second of frames builds the reservoir and synthesis state; local
     * scanner/decoder must still verify actual frames before making sound. */
    uint64_t start=target>s->sample_rate ? target-s->sample_rate : 0;
    uint64_t byte;
    if (s->cbr) {
        byte=(start*(1ULL<<24)/samples)*s->audio_bytes/(1ULL<<24);
    } else {
        uint64_t percent=start*100U;
        unsigned index=(unsigned)(percent/samples);
        uint64_t fraction=percent%samples;
        unsigned low=s->toc[index], high=index==99 ? 256U : s->toc[index+1];
        /* Divide before multiply to avoid samples * audio_bytes overflow. */
        uint64_t fraction256=((uint64_t)low<<16)+(uint64_t)(high-low)*(fraction*(1U<<16)/samples);
        byte=fraction256*s->audio_bytes/(256ULL<<16);
    }
    if (byte>=s->audio_bytes) byte=s->audio_bytes-1U;
    p->offset=s->audio_offset+(uint32_t)byte;
    p->approximate_seconds=(uint32_t)(target/s->sample_rate);
    p->preroll_samples=(uint32_t)(target-start);
    p->approximate=true;
    return true;
}

bool passport_mp3_seek_sync(const uint8_t *b, size_t size,
                            uint32_t rate, size_t *offset) {
    if (!b || !offset || size<4) return false;
    for (size_t i=0;i+4U<=size;++i) {
        passport_mp3_frame_t first;
        if (!passport_mp3_header(b+i,&first) || first.sample_rate!=rate) continue;
        size_t at=i+first.bytes;
        bool confirmed=true;
        for (unsigned n=0;n<2;++n) {
            passport_mp3_frame_t next;
            if (at+4U>size || !passport_mp3_header(b+at,&next) ||
                next.sample_rate!=rate || next.version!=first.version || next.channels!=first.channels) {
                confirmed=false; break;
            }
            at+=next.bytes;
        }
        if (confirmed) { *offset=i; return true; }
    }
    return false;
}
