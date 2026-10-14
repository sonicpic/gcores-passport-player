#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint16_t bytes, main_data_bytes, samples;
    uint32_t sample_rate, bitrate;
    uint8_t version, channels, crc_bytes, side_bytes;
} passport_mp3_frame_t;
bool passport_mp3_header(const uint8_t bytes[4], passport_mp3_frame_t *frame);

/* An estimate, never an exact PCM position. Info denotes CBR; Xing's 8-bit TOC
 * is coarse (one quantization unit is roughly duration / 256). */
typedef struct {
    uint32_t audio_offset, audio_bytes, frames, sample_rate;
    uint32_t proportional_seconds; /* RSS duration: linear estimate without a TOC. */
    uint16_t frame_samples;
    uint8_t toc[100];
    bool valid, cbr, has_toc;
} passport_mp3_seek_t;

typedef struct {
    uint32_t offset, approximate_seconds, preroll_samples;
    bool approximate;
} passport_mp3_seek_plan_t;

/* frame points at the first complete MPEG frame, with its absolute file offset.
 * Require the declared audio byte count to fit the actual HTTP resource. */
bool passport_mp3_seek_parse(passport_mp3_seek_t *seek, const uint8_t *frame,
                             size_t available, uint32_t offset, uint64_t total_bytes);
/* Explicit approximate fallback: validated first MP3 frame + HTTP size + RSS duration.
 * Does not claim CBR; VBR time error can exceed the one-second decoder preroll. */
bool passport_mp3_seek_proportional(passport_mp3_seek_t *seek, const uint8_t *frame,
                                    size_t available, uint32_t offset,
                                    uint64_t total_bytes, uint32_t duration_seconds);
bool passport_mp3_seek_plan(const passport_mp3_seek_t *seek, uint32_t seconds,
                            passport_mp3_seek_plan_t *plan);
/* Confirm three adjacent Layer III frames, allowing bitrate/padding changes.
 * Input need not start at a frame; returns its first confirmed frame offset. */
bool passport_mp3_seek_sync(const uint8_t *bytes, size_t available,
                            uint32_t sample_rate, size_t *offset);

/* Streaming ID3 removal and exact MPEG-frame time accounting. Neither helper
 * allocates or infers a whole file's bitrate from a short prefix. The caller
 * retains incomplete headers/frames in its existing input buffer. */
typedef struct {
    uint32_t offset, remaining;
    bool inspected;
} passport_mp3_prefix_t;
typedef struct {
    uint64_t samples;
    uint32_t sample_rate;
    uint8_t version, channels;
} passport_mp3_scan_t;
enum { PASSPORT_MP3_INVALID=-1, PASSPORT_MP3_NEED_MORE=0,
       PASSPORT_MP3_CONSUMED=1, PASSPORT_MP3_READY=2 };
int passport_mp3_prefix_step(passport_mp3_prefix_t *prefix, const uint8_t *bytes,
                             size_t size, uint64_t total, size_t *consumed);
int passport_mp3_scan_step(passport_mp3_scan_t *scan, const uint8_t *bytes,
                           size_t size, uint32_t seconds, size_t *consumed);
