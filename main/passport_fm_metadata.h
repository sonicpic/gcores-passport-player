#ifndef PASSPORT_FM_METADATA_H
#define PASSPORT_FM_METADATA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PASSPORT_FM_TITLE_BYTES 384
#define PASSPORT_FM_METADATA_MAX_TABLE 8192
#define PASSPORT_FM_METADATA_MAX_ENTRIES 128
/* Caller owns parser and table. No allocation survives a feed call. */
typedef struct {
    uint8_t *table;
    size_t capacity, used, object_used;
    uint32_t duration_s, last_at;
    uint16_t count;
    char audio_url[385], published_at[49], object[2048], primitive[64], key[32];
    struct { uint8_t type, state, field; } frames[16];
    uint8_t depth, capture_depth, token, primitive_used, key_used, escape, hex_left;
    uint8_t utf_left, utf_min, utf_max;
    uint16_t unicode;
    bool high_surrogate, low_required, root_started, root_done, failed;
} passport_fm_metadata_t;
typedef struct { uint32_t at_s; char title[385]; } passport_fm_program_t;
void passport_fm_metadata_init(passport_fm_metadata_t *, uint8_t *table, size_t capacity);
bool passport_fm_metadata_feed(passport_fm_metadata_t *, const uint8_t *, size_t);
bool passport_fm_metadata_finish(passport_fm_metadata_t *);
/* Table starts with a little-endian count, then at:u32, length:u16, title bytes. */
bool passport_fm_metadata_lookup(const uint8_t *, size_t, uint32_t position_s,
                                passport_fm_program_t out[3]);
#endif
