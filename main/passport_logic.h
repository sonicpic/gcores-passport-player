#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { PASSPORT_POWER_ACTIVE, PASSPORT_POWER_DIM, PASSPORT_POWER_DARK, PASSPORT_POWER_SLEEP } passport_power_stage_t;
int passport_clamp_selection(int selection, int direction, int count);
/* UTC seconds mapped to the user's Beijing calendar day; 0 means unsynchronized. */
int64_t passport_catalog_day(int64_t utc_seconds);
bool passport_catalog_update_due(int64_t utc_seconds, int64_t last_success, int64_t attempted_day);
uint32_t passport_seek_target(uint32_t position, int delta, uint32_t duration);
passport_power_stage_t passport_power_stage(uint64_t idle_ms, bool active_audio, bool busy, bool provisioning);
passport_power_stage_t passport_power_stage_configured(uint64_t idle_ms, bool active_audio, bool busy, bool provisioning, uint32_t screen_timeout_ms);
typedef struct {
    bool active;
    int direction;
    uint32_t target, duration;
    uint64_t started_ms, next_ms;
} passport_scrub_t;
void passport_scrub_begin(passport_scrub_t *s, uint32_t position, uint32_t duration, int direction, uint64_t now_ms);
bool passport_scrub_tick(passport_scrub_t *s, uint64_t now_ms);
bool passport_scrub_commit(passport_scrub_t *s, uint32_t *target);
typedef struct { bool swallow[3], held[3]; uint64_t until_us; } passport_screen_guard_t;
bool passport_screen_guard_consume(passport_screen_guard_t *s, unsigned key, bool press, bool release, bool dark, uint64_t now_us);
