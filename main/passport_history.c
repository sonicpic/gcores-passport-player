/* SPDX-License-Identifier: GPL-3.0-only */
#include "passport_history.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define HISTORY_MAGIC UINT32_C(0x48505047)
#define HISTORY_VERSION 1u
#define HISTORY_CHANNELS 5
/* Native-endian/versioned ESP32 persistent format; CRC covers padding too,
 * which is deterministic because records are zero-initialized before save. */
typedef struct {
    uint64_t sequence;
    uint32_t magic;
    uint32_t version;
    uint8_t identity[32];
    uint32_t position;
    uint32_t total;
    uint32_t completed;
    uint32_t crc;
} history_record_t;
_Static_assert(sizeof(history_record_t) == 64, "History persistent record layout");
static nvs_handle_t s_storage;
static bool s_ready;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;

static uint32_t checksum(const history_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < offsetof(history_record_t, crc); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
    }
    return ~crc;
}
static void slot_key(unsigned slot, char key[5]) { snprintf(key, 5, "h%02u", slot); }
static bool read_slot(unsigned slot, history_record_t *record)
{
    char key[5]; slot_key(slot, key);
    size_t size = sizeof(*record);
    return nvs_get_blob(s_storage, key, record, &size) == ESP_OK
        && size == sizeof(*record) && record->magic == HISTORY_MAGIC
        && record->version == HISTORY_VERSION && record->sequence != 0
        && record->completed <= 1 && (!record->total || record->position <= record->total)
        && record->crc == checksum(record);
}
static bool identity_hash(int channel, const char *guid, uint8_t out[32])
{
    if (channel < 0 || channel >= HISTORY_CHANNELS || !guid || !*guid) return false;
    size_t length = 0;
    while (length <= 256 && guid[length]) ++length;
    if (length > 256) return false;
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    const uint8_t prefix = (uint8_t)channel;
    int err = mbedtls_sha256_starts(&context, 0);
    if (!err) err = mbedtls_sha256_update(&context, &prefix, 1);
    if (!err) err = mbedtls_sha256_update(&context, (const uint8_t *)guid, length);
    if (!err) err = mbedtls_sha256_finish(&context, out);
    mbedtls_sha256_free(&context);
    return err == 0;
}
esp_err_t passport_history_init(void)
{
    if (s_ready) return ESP_OK;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    if (!s_mutex) return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_open("gpp_history", NVS_READWRITE, &s_storage);
    s_ready = err == ESP_OK;
    return err;
}
bool passport_history_lookup(int channel, const char *guid, passport_history_progress_t *out)
{
    uint8_t hash[32];
    if (!s_ready || !out || !identity_hash(channel, guid, hash)) return false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;
    history_record_t record;
    bool found = false;
    for (unsigned i = 0; i < PASSPORT_HISTORY_CAPACITY; ++i) {
        if (read_slot(i, &record) && memcmp(hash, record.identity, 32) == 0) {
            *out = (passport_history_progress_t){record.position, record.total, record.completed != 0};
            found = true; break;
        }
    }
    xSemaphoreGive(s_mutex);
    return found;
}
esp_err_t passport_history_save(int channel, const char *guid, uint32_t position,
                                 uint32_t total, bool completed)
{
    uint8_t hash[32];
    if (!identity_hash(channel, guid, hash) || (total && position > total)) return ESP_ERR_INVALID_ARG;
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    history_record_t record;
    int matching = -1, empty = -1, oldest = -1;
    uint64_t maximum = 0, minimum = UINT64_MAX;
    bool unchanged = false;
    for (unsigned i = 0; i < PASSPORT_HISTORY_CAPACITY; ++i) {
        if (!read_slot(i, &record)) { if (empty < 0) empty = (int)i; continue; }
        if (record.sequence > maximum) maximum = record.sequence;
        if (record.sequence < minimum) { minimum = record.sequence; oldest = (int)i; }
        if (memcmp(hash, record.identity, 32) == 0) {
            matching = (int)i;
            unchanged = record.position == position && record.total == total
                && record.completed == (uint32_t)completed;
        }
    }
    esp_err_t err = ESP_OK;
    if (!unchanged) {
        if (maximum == UINT64_MAX) err = ESP_ERR_INVALID_STATE;
        else {
            const int slot = matching >= 0 ? matching : (empty >= 0 ? empty : oldest);
            memset(&record, 0, sizeof(record));
            record.sequence = maximum + 1; record.magic = HISTORY_MAGIC;
            record.version = HISTORY_VERSION; memcpy(record.identity, hash, 32);
            record.position = position; record.total = total; record.completed = completed;
            record.crc = checksum(&record);
            char key[5]; slot_key((unsigned)slot, key);
            err = nvs_set_blob(s_storage, key, &record, sizeof(record));
            if (err == ESP_OK) err = nvs_commit(s_storage);
        }
    }
    xSemaphoreGive(s_mutex);
    return err;
}
