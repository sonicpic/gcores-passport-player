#ifndef PASSPORT_FM_STREAM_H
#define PASSPORT_FM_STREAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded, allocation-free parsers. False is terminal malformed/unsupported
 * input (or a sink failure); initialize again before retrying a resource. */
typedef struct {
    char url[512];
    uint64_t start_ms, duration_ms;
    uint32_t index;
} passport_fm_segment_t;
typedef struct {
    char line[512];
    size_t line_used;
    uint64_t target_ms, total_ms, pending_ms;
    int32_t target_index;
    uint32_t count;
    bool header, endlist, pending, selected, failed;
    passport_fm_segment_t segment;
    passport_fm_segment_t next[2];
    unsigned next_count;
} passport_fm_playlist_t;
void passport_fm_playlist_init(passport_fm_playlist_t *p, uint64_t target_ms,
                              int32_t target_index);
bool passport_fm_playlist_feed(passport_fm_playlist_t *p, const void *data, size_t n);
bool passport_fm_playlist_finish(passport_fm_playlist_t *p);
bool passport_fm_resolve_url(const char *base, const char *relative,
                             char *out, size_t capacity);

typedef bool (*passport_fm_audio_sink_t)(void *context, const uint8_t *data, size_t n);
typedef struct {
    uint8_t packet[188], psi[1024], pes[264];
    size_t packet_used, psi_used, psi_need, pes_used, pes_need;
    uint16_t pmt_pid, audio_pid, psi_pid;
    int8_t counters[3];
    uint32_t pes_remaining;
    bool failed, pes_active, pes_unbounded;
    passport_fm_audio_sink_t sink;
    void *context;
    uint64_t audio_bytes;
} passport_fm_ts_t;
void passport_fm_ts_init(passport_fm_ts_t *p, passport_fm_audio_sink_t sink, void *context);
bool passport_fm_ts_feed(passport_fm_ts_t *p, const void *data, size_t n);
bool passport_fm_ts_finish(passport_fm_ts_t *p);
#endif
