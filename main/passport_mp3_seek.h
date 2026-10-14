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
bool passport_mp3_seek_plan(const passport_mp3_seek_t *seek, uint32_t seconds,
                            passport_mp3_seek_plan_t *plan);
/* Confirm three adjacent Layer III frames, allowing bitrate/padding changes.
 * Input need not start at a frame; returns its first confirmed frame offset. */
bool passport_mp3_seek_sync(const uint8_t *bytes, size_t available,
                            uint32_t sample_rate, size_t *offset);
