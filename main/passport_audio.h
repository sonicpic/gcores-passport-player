#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    PASSPORT_AUDIO_IDLE, PASSPORT_AUDIO_BUFFERING, PASSPORT_AUDIO_PLAYING,
    PASSPORT_AUDIO_PAUSED, PASSPORT_AUDIO_ENDED, PASSPORT_AUDIO_ERROR,
} passport_audio_state_t;

typedef struct {
    passport_audio_state_t state;
    uint32_t position_seconds;
    uint32_t duration_seconds; /* MP3 metadata duration, else size/bitrate estimate; zero unknown. */
    uint32_t sample_rate;
    uint8_t volume;
    bool fm; /* Synchronized station, no episode seeking/history. */
    bool seek_supported; /* Fast approximate MP3 Range when available; sequential fallback. */
    bool position_approximate; /* Metadata or RSS/size mapping after a fast MP3 seek. */
    bool seeking; /* Accepted seek still waiting for target PCM; paused seeks await resume. */
    uint32_t seek_request_id, seek_complete_id;
    esp_err_t last_error;
} passport_audio_status_t;

/* Initialize once from the lifecycle task, before producers start.
 * Commands copy/own their inputs and return immediately, or ESP_ERR_TIMEOUT if
 * the bounded queue is full. Only the playback worker accesses the audio BSP.
 * The caller polls snapshots; workers never retain UI objects or callbacks.
 * URLs are limited to 1023 bytes. No URL/credentials are written to logs.
 */
esp_err_t passport_audio_init(void);
/* duration_seconds is the RSS duration (zero if unknown). Saved MP3 playback
 * probes its first frame and reuses the manual Range path; absent Info/Xing,
 * known duration and HTTP size allow a proportional, approximate position.
 * Without a usable plan it scans compressed frames;
 * formats without this path decode/discard from zero. */
esp_err_t passport_audio_play(const char *url, uint32_t resume_seconds, uint32_t duration_seconds);
/* Fetch the latest station and join its network-time position. */
esp_err_t passport_audio_fm(void);
esp_err_t passport_audio_pause(bool paused);
esp_err_t passport_audio_stop(void);
/* Commit a final target once after the gesture ends; UI previews must not
 * enqueue seeks. Preserves pause state; decode/discard waits for resume when
 * paused. MP3 with validated Info/Xing or RSS duration/size and strong ETag uses approximate Range
 * plus frame sync/reservoir preroll. position_approximate distinguishes its time
 * estimate. ID3 tags up to 256 KiB are skipped within the existing input buffer
 * before metadata discovery. MP3 without a usable Range plan scans compressed
 * frame headers from zero and decodes only the final preroll; the network must
 * still read the skipped prefix. Formats without this path decode sequentially.
 * Nearby forward targets reuse the live cursor; backward/ended/unknown streams
 * without a usable fast path restart from zero and may take time.
 * seeking/request/complete IDs report completion of this accepted request. */
esp_err_t passport_audio_seek(uint32_t seconds);
esp_err_t passport_audio_set_volume(uint8_t percent);
void passport_audio_get_status(passport_audio_status_t *status);
/* Poll after stop before system sleep. True only after codec and network stop.
 * No external bsp_audio_sleep/wake/set_format call may race this subsystem. */
bool passport_audio_is_quiescent(void);
