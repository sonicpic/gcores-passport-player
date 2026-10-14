#include "passport_fm_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t output[4096]; static size_t output_used;
static bool collect(void *ctx, const uint8_t *b, size_t n)
{ (void)ctx; assert(output_used + n <= sizeof(output)); memcpy(output + output_used, b, n); output_used += n; return true; }
static bool count(void *ctx, const uint8_t *b, size_t n)
{ (void)b; *(uint64_t *)ctx += n; return true; }
static bool reject(void *ctx, const uint8_t *b, size_t n)
{ (void)ctx; (void)b; (void)n; return false; }
static uint32_t crc(const uint8_t *b, size_t n)
{
    uint32_t c = UINT32_MAX;
    while (n--) { c ^= (uint32_t)*b++ << 24; for (int j = 0; j < 8; ++j) c = (c << 1) ^ ((c & 0x80000000U) ? 0x04c11db7U : 0); }
    return c;
}
static void section_crc(uint8_t *b, size_t n)
{ uint32_t c = crc(b, n - 4); for (int j = 0; j < 4; ++j) b[n - 4 + j] = (uint8_t)(c >> (24 - j * 8)); }
static void packet(uint8_t *out, unsigned pid, unsigned cc, bool start, const uint8_t *payload, size_t n)
{
    assert(n <= 184); memset(out, 0xff, 188);
    out[0] = 0x47; out[1] = (uint8_t)((pid >> 8) | (start ? 0x40 : 0)); out[2] = (uint8_t)pid;
    out[3] = (uint8_t)(cc | (n == 184 ? 0x10 : 0x30));
    size_t at = 4;
    if (n != 184) { out[at++] = (uint8_t)(183 - n); if (183 - n) out[at] = 0; at = 188 - n; }
    memcpy(out + at, payload, n);
}
static void tables(passport_fm_ts_t *p)
{
    uint8_t pat[] = {0, 0,0xb0,13, 0,1,0xc1,0,0, 0,1,0xf0,0, 0,0,0,0};
    uint8_t pmt[] = {0, 2,0xb0,18, 0,1,0xc1,0,0, 0xe1,0,0xf0,0, 0x0f,0xe1,0,0xf0,0, 0,0,0,0};
    uint8_t pkt[188]; section_crc(pat + 1, sizeof(pat) - 1); section_crc(pmt + 1, sizeof(pmt) - 1);
    packet(pkt, 0, 0, true, pat, sizeof(pat)); assert(passport_fm_ts_feed(p, pkt, 188));
    packet(pkt, 0x1000, 0, true, pmt, sizeof(pmt)); assert(passport_fm_ts_feed(p, pkt, 188));
    assert(p->audio_pid == 0x100);
}
static void synthetic(void)
{
    passport_fm_ts_t p; passport_fm_ts_init(&p, collect, NULL); tables(&p); output_used = 0;
    const uint8_t pes[] = {0,0,1,0xc0,0,15,0x80,0x80,5,0x21,0,1,0,1, 0xff,0xf1,0x50,0x80,1,0x1f,0xfc};
    uint8_t pkt[188];
    packet(pkt, 0x100, 0, true, pes, 4);
    for (size_t i = 0; i < 188; ++i) assert(passport_fm_ts_feed(&p, pkt + i, 1));
    packet(pkt, 0x100, 1, false, pes + 4, sizeof(pes) - 4);
    assert(passport_fm_ts_feed(&p, pkt, 188));
    assert(passport_fm_ts_feed(&p, pkt, 188)); /* Duplicate suppressed. */
    assert(output_used == 7 && !memcmp(output, pes + 14, 7)); assert(passport_fm_ts_finish(&p));
    packet(pkt, 0x100, 3, true, pes, sizeof(pes));
    assert(!passport_fm_ts_feed(&p, pkt, 188)); /* Missing continuity packet. */
    passport_fm_ts_init(&p, collect, NULL); pkt[0] = 0; assert(!passport_fm_ts_feed(&p, pkt, 188));
    passport_fm_ts_init(&p, collect, NULL); tables(&p); packet(pkt, 0x100, 0, true, pes, 4);
    assert(passport_fm_ts_feed(&p, pkt, 188)); assert(!passport_fm_ts_finish(&p));
    packet(pkt,0x100,1,true,pes,sizeof(pes)); assert(!passport_fm_ts_feed(&p,pkt,188));
    passport_fm_ts_init(&p,reject,NULL); tables(&p);
    packet(pkt,0x100,0,true,pes,sizeof(pes)); assert(!passport_fm_ts_feed(&p,pkt,188));
    passport_fm_ts_init(&p,collect,NULL); tables(&p);
    packet(pkt,0x100,0,true,pes,sizeof(pes)); pkt[3]|=0x80; assert(!passport_fm_ts_feed(&p,pkt,188));
    /* PSI section fragmentation and CRC failure. */
    uint8_t pat[] = {0,0,0xb0,13,0,1,0xc1,0,0,0,1,0xf0,0,0,0,0,0}; section_crc(pat+1,16);
    passport_fm_ts_init(&p, collect, NULL);
    packet(pkt,0,0,true,pat,7); assert(passport_fm_ts_feed(&p,pkt,188));
    packet(pkt,0,1,false,pat+7,sizeof(pat)-7); assert(passport_fm_ts_feed(&p,pkt,188)); assert(p.pmt_pid == 0x1000);
    passport_fm_ts_init(&p, collect, NULL); pat[12]^=1; packet(pkt,0,0,true,pat,sizeof(pat)); assert(!passport_fm_ts_feed(&p,pkt,188));
}
static void playlists(void)
{
    const char *text = "#EXTM3U\r\n#EXT-X-VERSION:3\n#EXTINF:1.001,\na.ts\n#EXTINF:2.000,\nb.ts\n#EXTINF:3,\nc.ts\n#EXTINF:4,\nd.ts\n#EXT-X-ENDLIST\n";
    passport_fm_playlist_t p; passport_fm_playlist_init(&p,1001,-1);
    for (size_t i=0; i<strlen(text); ++i) assert(passport_fm_playlist_feed(&p,text+i,1));
    assert(passport_fm_playlist_finish(&p)); assert(!strcmp(p.segment.url,"b.ts"));
    assert(p.segment.start_ms==1001 && p.segment.duration_ms==2000 && p.segment.index==1);
    assert(p.next_count==2 && !strcmp(p.next[1].url,"d.ts") && p.total_ms==10001);
    passport_fm_playlist_init(&p,0,2); assert(passport_fm_playlist_feed(&p,text,strlen(text)) && passport_fm_playlist_finish(&p)); assert(p.segment.index==2);
    passport_fm_playlist_init(&p,10001,-1); assert(passport_fm_playlist_feed(&p,text,strlen(text))); assert(!passport_fm_playlist_finish(&p));
    const char *bad[]={"#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n", "#EXTM3U\n#EXT-X-DISCONTINUITY\n", "#EXTM3U\n#EXT-X-BYTERANGE:100\n", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=10\n", "#EXTM3U\n#EXTINF:0,\na.ts\n", "#EXTM3U\na.ts\n"};
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) {passport_fm_playlist_init(&p,0,-1);assert(!passport_fm_playlist_feed(&p,bad[i],strlen(bad[i])));}
    char long_line[600]; memset(long_line,'x',sizeof(long_line));
    passport_fm_playlist_init(&p,0,-1); assert(!passport_fm_playlist_feed(&p,long_line,sizeof(long_line)));
    char url[512]; assert(passport_fm_resolve_url("https://example.com/path/index.m3u8","a.ts",url,sizeof(url))); assert(!strcmp(url,"https://example.com/path/a.ts"));
    assert(passport_fm_resolve_url("https://example.com/path/index.m3u8","/a.ts",url,sizeof(url))); assert(!strcmp(url,"https://example.com/a.ts"));
    assert(!passport_fm_resolve_url("https://example.com/x","../a.ts",url,sizeof(url)));
    assert(passport_fm_resolve_url("http://example.com/path/index.m3u8","a.ts",url,sizeof(url)));
    assert(!strcmp(url,"http://example.com/path/a.ts"));
    assert(!passport_fm_resolve_url("ftp://example.com/path/x","a.ts",url,sizeof(url)));
}
static void actual_ts(const char *path)
{
    FILE *f=fopen(path,"rb"); assert(f); uint8_t chunk[251]; uint64_t bytes=0;
    passport_fm_ts_t p; passport_fm_ts_init(&p,count,&bytes);
    size_t n; while ((n=fread(chunk,1,sizeof(chunk),f))) { if (!passport_fm_ts_feed(&p,chunk,n)) {fprintf(stderr,"TS failure at %ld audio=%llu psi=%zu pes=%zu/%zu remaining=%u\n",ftell(f),(unsigned long long)bytes,p.psi_used,p.pes_used,p.pes_need,p.pes_remaining);abort();}}
    fclose(f); assert(passport_fm_ts_finish(&p)); assert(bytes>900000);
    printf("TS %s: %llu AAC bytes, state %zu bytes\n",path,(unsigned long long)bytes,sizeof(p));
}
static void actual_playlist(const char *path)
{
    FILE *f=fopen(path,"rb"); assert(f); char chunk[37]; size_t n;
    passport_fm_playlist_t p; passport_fm_playlist_init(&p,120000,-1);
    while((n=fread(chunk,1,sizeof(chunk),f))) assert(passport_fm_playlist_feed(&p,chunk,n));
    fclose(f); assert(passport_fm_playlist_finish(&p)); assert(p.count==3583 && p.segment.index==2 && p.next_count==2);
    assert(p.segment.start_ms<=120000 && p.segment.start_ms+p.segment.duration_ms>120000);
    printf("Playlist: %u segments, %llu ms, state %zu bytes\n",p.count,(unsigned long long)p.total_ms,sizeof(p));
}
int main(int argc,char **argv)
{ playlists(); synthetic(); for(int i=1;i<argc;++i) { if(strstr(argv[i],".m3u8"))actual_playlist(argv[i]);else actual_ts(argv[i]); } puts("FM stream tests PASS");return 0; }
