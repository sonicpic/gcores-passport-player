#include "passport_fm_stream.h"
#include <limits.h>
#include <string.h>

static bool duration(const char *s, uint64_t *ms)
{
    uint64_t whole = 0; unsigned fraction = 0, digits = 0;
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        if (whole > 86400) return false;
        whole = whole * 10 + (unsigned)(*s++ - '0');
    }
    if (*s == '.') {
        ++s;
        if (*s < '0' || *s > '9') return false;
        while (*s >= '0' && *s <= '9') {
            if (digits < 3) fraction = fraction * 10 + (unsigned)(*s - '0');
            ++digits; ++s;
        }
    }
    if (*s != ',' || whole > 86400) return false;
    while (digits++ < 3) fraction *= 10;
    *ms = whole * 1000 + fraction;
    return *ms != 0;
}
static bool playlist_line(passport_fm_playlist_t *p)
{
    char *s = p->line;
    if (p->line_used && s[p->line_used - 1] == '\r') --p->line_used;
    s[p->line_used] = 0;
    if (!p->header) { p->header = strcmp(s, "#EXTM3U") == 0; return p->header; }
    if (!*s) return true;
    if (!strncmp(s, "#EXTINF:", 8)) {
        if (p->pending || p->endlist || !duration(s + 8, &p->pending_ms)) return false;
        p->pending = true; return true;
    }
    if (!strcmp(s, "#EXT-X-ENDLIST")) { p->endlist = true; return !p->pending; }
    if (!strncmp(s, "#EXT-X-KEY:", 11) || !strncmp(s, "#EXT-X-MAP:", 11) ||
        !strncmp(s, "#EXT-X-BYTERANGE:", 17) || !strncmp(s, "#EXT-X-DISCONTINUITY", 20) ||
        !strncmp(s, "#EXT-X-STREAM-INF:", 18) || !strncmp(s, "#EXT-X-I-FRAMES-ONLY", 20)) return false;
    if (*s == '#') return true;
    if (!p->pending || p->endlist || p->count == UINT32_MAX ||
        UINT64_MAX - p->total_ms < p->pending_ms) return false;
    for (const unsigned char *c = (const unsigned char *)s; *c; ++c)
        if (*c <= 32 || *c == '\\') return false;
    if (!p->selected && ((p->target_index >= 0 && p->count == (uint32_t)p->target_index) ||
        (p->target_index < 0 && p->target_ms >= p->total_ms &&
         p->target_ms - p->total_ms < p->pending_ms))) {
        memcpy(p->segment.url, s, p->line_used + 1);
        p->segment.start_ms = p->total_ms;
        p->segment.duration_ms = p->pending_ms;
        p->segment.index = p->count; p->selected = true;
    } else if (p->selected && p->next_count < 2) {
        passport_fm_segment_t *next = &p->next[p->next_count++];
        memcpy(next->url, s, p->line_used + 1);
        next->start_ms = p->total_ms; next->duration_ms = p->pending_ms;
        next->index = p->count;
    }
    p->total_ms += p->pending_ms; ++p->count; p->pending = false;
    return true;
}
void passport_fm_playlist_init(passport_fm_playlist_t *p, uint64_t target_ms, int32_t index)
{ memset(p, 0, sizeof(*p)); p->target_ms = target_ms; p->target_index = index; }
bool passport_fm_playlist_feed(passport_fm_playlist_t *p, const void *data, size_t n)
{
    const uint8_t *b = data;
    if (p->failed) return false;
    for (size_t i = 0; i < n; ++i) {
        if (!b[i]) { p->failed = true; return false; }
        if (b[i] == '\n') {
            if (!playlist_line(p)) { p->failed = true; return false; }
            p->line_used = 0;
        } else {
            if (p->line_used == sizeof(p->line) - 1) { p->failed = true; return false; }
            p->line[p->line_used++] = (char)b[i];
        }
    }
    return true;
}
bool passport_fm_playlist_finish(passport_fm_playlist_t *p)
{
    if (!p->failed && p->line_used && !playlist_line(p)) p->failed = true;
    p->line_used = 0;
    return !p->failed && p->header && p->endlist && !p->pending && p->selected;
}
bool passport_fm_resolve_url(const char *base, const char *relative, char *out, size_t cap)
{
    if (!base || !relative || !out || !*relative) return false;
    size_t scheme = !strncmp(base,"https://",8) ? 8 : !strncmp(base,"http://",7) ? 7 : 0;
    if (!scheme) return false;
    size_t prefix = 0;
    if (!strncmp(relative, "https://", 8) || !strncmp(relative,"http://",7)) { prefix = 0; }
    else {
        if (strstr(relative, "://") || strstr(relative, "..") || relative[0] == '\\' ||
            (relative[0] == '/' && relative[1] == '/')) return false;
        const char *host_end = strchr(base + scheme, '/');
        if (!host_end) return false;
        if (*relative == '/') prefix = (size_t)(host_end - base);
        else { const char *last = strrchr(base, '/'); prefix = (size_t)(last - base) + 1; }
    }
    size_t len = strlen(relative);
    if (prefix + len >= cap) return false;
    memcpy(out, base, prefix); memcpy(out + prefix, relative, len + 1); return true;
}
static uint32_t crc(const uint8_t *b, size_t n)
{
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint32_t)b[i] << 24;
        for (unsigned j = 0; j < 8; ++j) c = (c << 1) ^ ((c & 0x80000000U) ? 0x04c11db7U : 0);
    }
    return c;
}
static bool psi_section(passport_fm_ts_t *p)
{
    const uint8_t *b = p->psi; size_t n = p->psi_need;
    if (n < 12 || !(b[1] & 0x80) || crc(b, n) || !(b[5] & 1) || b[6] || b[7]) return false;
    if (p->psi_pid == 0) {
        if (b[0] != 0 || (n - 12) % 4) return false;
        for (size_t i = 8; i + 4 <= n - 4; i += 4) {
            if (b[i] || b[i + 1]) {
                uint16_t pid = ((uint16_t)(b[i + 2] & 31) << 8) | b[i + 3];
                if (p->pmt_pid != 0x1fff && p->pmt_pid != pid) return false;
                p->pmt_pid = pid; return true;
            }
        }
        return false;
    }
    if (b[0] != 2 || n < 16) return false;
    size_t i = 12 + (((size_t)b[10] & 15) << 8) + b[11];
    if (i > n - 4) return false;
    uint16_t found = 0x1fff;
    while (i < n - 4) {
        if (n - 4 - i < 5) return false;
        size_t descriptors = (((size_t)b[i + 3] & 15) << 8) | b[i + 4];
        if (descriptors > n - 4 - i - 5) return false;
        if (b[i] == 0x0f) {
            if (found != 0x1fff) return false;
            found = ((uint16_t)(b[i + 1] & 31) << 8) | b[i + 2];
        }
        i += 5 + descriptors;
    }
    if (found == 0x1fff || (p->audio_pid != 0x1fff && p->audio_pid != found)) return false;
    p->audio_pid = found; return true;
}
static bool psi_bytes(passport_fm_ts_t *p, uint16_t pid, bool start, const uint8_t *b, size_t n)
{
    if (start) {
        if (!n || b[0] >= n) return false;
        size_t pointer = *b++; --n;
        /* Complete a section continued before the next section's pointer. */
        if (p->psi_used && p->psi_pid == pid) {
            for (size_t i = 0; i < pointer && p->psi_used < p->psi_need; ++i) p->psi[p->psi_used++] = b[i];
            if (p->psi_used != p->psi_need || !psi_section(p)) return false;
        }
        b += pointer; n -= pointer; p->psi_used = p->psi_need = 0; p->psi_pid = pid;
    } else if (!p->psi_used || p->psi_pid != pid) return true;
    while (n) {
        if (!p->psi_used && *b == 0xff) return true;
        if (p->psi_used >= sizeof(p->psi)) return false;
        p->psi[p->psi_used++] = *b++; --n;
        if (p->psi_used == 3) {
            p->psi_need = 3 + (((size_t)p->psi[1] & 15) << 8) + p->psi[2];
            if (p->psi_need < 12 || p->psi_need > sizeof(p->psi)) return false;
        }
        if (p->psi_need && p->psi_used == p->psi_need) {
            if (!psi_section(p)) return false;
            p->psi_used = p->psi_need = 0;
        }
    }
    return true;
}
static bool pes_bytes(passport_fm_ts_t *p, bool start, const uint8_t *b, size_t n)
{
    if (start) {
        if (p->pes_active && (p->pes_used < p->pes_need ||
            (!p->pes_unbounded && p->pes_remaining))) return false;
        p->pes_used = 0; p->pes_need = 9; p->pes_active = true; p->pes_unbounded = false;
    }
    if (!p->pes_active) return true;
    while (p->pes_used < p->pes_need && n) {
        p->pes[p->pes_used++] = *b++; --n;
        if (p->pes_used == 9) {
            if (p->pes[0] || p->pes[1] || p->pes[2] != 1 ||
                p->pes[3] < 0xc0 || p->pes[3] > 0xdf || (p->pes[6] & 0xc0) != 0x80 ||
                (p->pes[6] & 0x30)) return false;
            p->pes_need = 9 + p->pes[8];
            unsigned length = ((unsigned)p->pes[4] << 8) | p->pes[5];
            p->pes_unbounded = length == 0;
            if (length && length < 3U + p->pes[8]) return false;
            p->pes_remaining = length ? length - 3U - p->pes[8] : 0;
        }
    }
    if (p->pes_used < p->pes_need) return true;
    if (!p->pes_unbounded && n > p->pes_remaining) n = p->pes_remaining;
    if (n && (!p->sink || !p->sink(p->context, b, n))) return false;
    p->audio_bytes += n;
    if (!p->pes_unbounded) p->pes_remaining -= (uint32_t)n;
    return true;
}
static bool ts_packet(passport_fm_ts_t *p)
{
    const uint8_t *b = p->packet;
    if (b[0] != 0x47 || (b[1] & 0x80) || (b[3] & 0xc0)) return false;
    uint16_t pid = ((uint16_t)(b[1] & 31) << 8) | b[2];
    unsigned control = (b[3] >> 4) & 3; size_t at = 4;
    if (!control) return false;
    if (control & 2) {
        size_t len = b[at++];
        if (len > 188 - at || (len && (b[at] & 0x80))) return false;
        at += len;
    }
    if (!(control & 1) || at == 188) return true;
    int slot = pid == 0 ? 0 : pid == p->pmt_pid ? 1 : pid == p->audio_pid ? 2 : -1;
    if (slot < 0) return true;
    int cc = b[3] & 15, prev = p->counters[slot];
    if (prev == cc) return true; /* Retransmitted payload packet. */
    if (prev >= 0 && cc != ((prev + 1) & 15)) return false;
    p->counters[slot] = (int8_t)cc;
    bool start = (b[1] & 0x40) != 0;
    return slot == 2 ? pes_bytes(p, start, b + at, 188 - at) :
                      psi_bytes(p, pid, start, b + at, 188 - at);
}
void passport_fm_ts_init(passport_fm_ts_t *p, passport_fm_audio_sink_t sink, void *context)
{
    memset(p, 0, sizeof(*p)); p->pmt_pid = p->audio_pid = p->psi_pid = 0x1fff;
    memset(p->counters, -1, sizeof(p->counters)); p->sink = sink; p->context = context;
}
bool passport_fm_ts_feed(passport_fm_ts_t *p, const void *data, size_t n)
{
    const uint8_t *b = data;
    if (p->failed) return false;
    while (n) {
        size_t take = 188 - p->packet_used; if (take > n) take = n;
        memcpy(p->packet + p->packet_used, b, take); p->packet_used += take; b += take; n -= take;
        if (p->packet_used == 188) {
            if (!ts_packet(p)) { p->failed = true; return false; }
            p->packet_used = 0;
        }
    }
    return true;
}
bool passport_fm_ts_finish(passport_fm_ts_t *p)
{
    return !p->failed && !p->packet_used && !p->psi_used && p->audio_bytes &&
           p->pes_used == p->pes_need && (p->pes_unbounded || !p->pes_remaining);
}
