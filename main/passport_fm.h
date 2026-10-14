#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Runs in the existing audio network task. Emits only ADTS audio bytes;
 * no TS, API response or manifest bytes reach the decoder. Cancellation must
 * also be checked by emit while waiting for space in the audio FIFO. */
esp_err_t passport_fm_run(uint32_t generation,
                         bool (*cancelled)(uint32_t),
                         bool (*emit)(const uint8_t *, size_t, void *),
                         esp_err_t (*prepare)(void *),
                         uint32_t (*elapsed)(void *),
                         void *ctx);
/* Copies a synchronized bounded snapshot; no LVGL or network access. */
void passport_fm_get_titles(char *current,size_t current_size,char *next,size_t next_size);
