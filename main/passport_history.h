/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PASSPORT_HISTORY_CAPACITY 32
/* Identity is channel + nonempty UTF-8 GUID, independent of RSS list order.
 * Fixed 32-slot NVS map. Calls perform bounded local I/O, no network; do not
 * call while holding the LVGL lock. init follows nvs_flash_init. */
typedef struct {
    uint32_t position_seconds;
    uint32_t total_seconds;
    bool completed;
} passport_history_progress_t;
esp_err_t passport_history_init(void);
/* Missing/corrupt entries return false without changing out. */
bool passport_history_lookup(int channel, const char *guid, passport_history_progress_t *out);
/* Save/commit one atomic slot. Unchanged progress does not consume Flash writes
 * or advance recency. Oldest changed-save sequence is evicted at capacity. */
esp_err_t passport_history_save(int channel, const char *guid, uint32_t position_seconds,
                                 uint32_t total_seconds, bool completed);
#ifdef __cplusplus
}
#endif
