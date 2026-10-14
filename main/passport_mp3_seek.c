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

bool passport_mp3_seek_proportional(passport_mp3_seek_t *s, const uint8_t *b,
                                    size_t size, uint32_t offset, uint64_t total,
                                    uint32_t duration) {
    passport_mp3_frame_t f;
    if (!s || !b || size<4 || !passport_mp3_header(b,&f) || size<f.bytes ||
        !duration || duration>30U*24U*3600U || total>UINT32_MAX || total<=offset || total-offset<f.bytes)
        return false;
    *s=(passport_mp3_seek_t){.audio_offset=offset,.audio_bytes=(uint32_t)(total-offset),
        .sample_rate=f.sample_rate,.frame_samples=f.samples,
        .proportional_seconds=duration,.valid=true};
    return true;
}

bool passport_mp3_seek_plan(const passport_mp3_seek_t *s, uint32_t seconds,
                            passport_mp3_seek_plan_t *p) {
    if (!s || !p || !s->valid || (!s->frames && !s->proportional_seconds) || !s->sample_rate || !s->audio_bytes ||
        (s->frame_samples!=576 && s->frame_samples!=1152) || s->frames>s->audio_bytes/24U ||
        (!s->proportional_seconds && !s->cbr && !s->has_toc)) return false;
    uint64_t samples=s->proportional_seconds ? (uint64_t)s->proportional_seconds*s->sample_rate :
                     (uint64_t)s->frames*s->frame_samples;
    uint64_t target=(uint64_t)seconds*s->sample_rate;
    if (target>=samples) target=samples-1;
    /* One second of frames builds the reservoir and synthesis state; local
     * scanner/decoder must still verify actual frames before making sound. */
    uint64_t start=target>s->sample_rate ? target-s->sample_rate : 0;
    uint64_t byte;
    if (s->cbr || s->proportional_seconds) {
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

int passport_mp3_prefix_step(passport_mp3_prefix_t *p, const uint8_t *b,
                             size_t size, uint64_t total, size_t *consumed) {
    if (!p || !b || !consumed) return PASSPORT_MP3_INVALID;
    *consumed=0;
    if (!p->inspected) {
        if (size<10) return PASSPORT_MP3_NEED_MORE;
        p->inspected=true;
        if (!memcmp(b,"ID3",3)) {
            if (b[3]<2 || b[3]>4 || b[4]==255 || ((b[6]|b[7]|b[8]|b[9])&128))
                return PASSPORT_MP3_INVALID;
            uint8_t allowed=b[3]==2 ? 0xc0 : b[3]==3 ? 0xe0 : 0xf0;
            if (b[5]&~allowed) return PASSPORT_MP3_INVALID;
            uint32_t tag=10U+((uint32_t)b[6]<<21)+((uint32_t)b[7]<<14)+((uint32_t)b[8]<<7)+b[9];
            if (b[3]==4 && (b[5]&16)) tag+=10;
            if (tag>256U*1024U || (total && tag>=total)) return PASSPORT_MP3_INVALID;
            p->remaining=tag;
        }
    }
    if (p->remaining) {
        size_t n=p->remaining<size ? p->remaining : size;
        p->remaining-=n; p->offset+=n; *consumed=n;
        return PASSPORT_MP3_CONSUMED;
    }
    return PASSPORT_MP3_READY;
}

int passport_mp3_scan_step(passport_mp3_scan_t *s, const uint8_t *b,
                           size_t size, uint32_t seconds, size_t *consumed) {
    if (!s || !b || !consumed) return PASSPORT_MP3_INVALID;
    *consumed=0;
    if (size<4) return PASSPORT_MP3_NEED_MORE;
    passport_mp3_frame_t f;
    if (!passport_mp3_header(b,&f)) return PASSPORT_MP3_INVALID;
    if (size<f.bytes) return PASSPORT_MP3_NEED_MORE;
    if (!s->sample_rate) {
        s->sample_rate=f.sample_rate; s->version=f.version; s->channels=f.channels;
    } else if (f.sample_rate!=s->sample_rate || f.version!=s->version || f.channels!=s->channels)
        return PASSPORT_MP3_INVALID;
    uint64_t target=(uint64_t)seconds*s->sample_rate;
    uint64_t start=target>s->sample_rate ? target-s->sample_rate : 0;
    if (s->samples>=start) return PASSPORT_MP3_READY;
    s->samples+=f.samples; *consumed=f.bytes;
    return PASSPORT_MP3_CONSUMED;
}
