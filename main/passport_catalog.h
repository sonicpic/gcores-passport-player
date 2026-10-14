/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PASSPORT_CHANNEL_COUNT 5
#define PASSPORT_EPISODES_PER_CHANNEL 100
#define PASSPORT_EPISODE_PAGE_SIZE 4
#define PASSPORT_MAX_CHAPTERS 80

typedef struct {
    char title[385];
    char guid[257];
    char url[1025];
    uint32_t duration_seconds;
    uint64_t enclosure_length;
} passport_episode_t;
typedef struct { uint32_t start_seconds; char title[181]; } passport_chapter_t;
typedef struct {
    uint8_t channel;
    uint8_t episode;
    uint32_t position_seconds;
    uint32_t total_seconds;
    bool completed;
    char guid[257];
    char url[1025];
    char title[385];
} passport_resume_t;
/* load_resume resolves exact GUID+URL; index is UINT8_MAX when absent. Saved
 * URL/title remain playable and can be pinned as the last real episode row. */

/* Call after nvs_flash_init(). Refresh/fetch/save run only in the worker.
 * Episode getters use a four-record page and bounded synchronous local Flash
 * reads on page misses. No network or heap allocation occurs in these getters.
 * A failed refresh keeps existing episodes. TLS uses the system CA bundle.
 * gcores_cache stores transactional A/B snapshots, up to 100 episodes/channel.
 * Legacy five-item NVS blobs are imported without erasing NVS. No SD required.
 */
esp_err_t passport_catalog_init(void);
int passport_catalog_channel_count(void);
const char *passport_catalog_channel_name(int channel);
const char *passport_catalog_channel_short_name(int channel);
const char *passport_catalog_channel_id(int channel);
int passport_catalog_episode_count(int channel);
bool passport_catalog_get_episode(int channel, int episode, passport_episode_t *out);
/* Scan at most 100 local Flash records, without network/allocation.
 * Both nonempty GUID and URL must match; invalid/unavailable identity is -1. */
int passport_catalog_find_episode_by_identity(int channel, const char *guid, const char *url);
/* Pin the actual playback snapshot independently of RSS indexes. When absent
 * from the Flash snapshot it appears as the final episode row (maximum 101).
 * Performs one bounded identity scan; call on source replacement, not render. */
esp_err_t passport_catalog_set_playback(int channel, const passport_episode_t *snapshot);
esp_err_t passport_catalog_refresh(int channel);
size_t passport_catalog_get_chapters(int channel, int episode, passport_chapter_t *out, size_t capacity);
size_t passport_catalog_chapter_count(int channel, int episode);
bool passport_catalog_get_chapter(int channel, int episode, size_t index, passport_chapter_t *out);
/* Query only the already-read packed timeline under a short mutex. No network
 * I/O, heap allocation or expanded vector. out is required; index is optional.
 * No chapter at/before position or malformed data returns false, leaving output
 * unchanged. An evicted/unread timeline must be fetched by the worker first. */
bool passport_catalog_current_chapter(int channel, int episode, uint32_t position_seconds,
                                      passport_chapter_t *out, size_t *index);
/* Load chapter JSON, or refetch the RSS for the selected episode when the
 * timeline has been evicted from RAM. Network operations are blocking. Retain
 * at most 80 whole chapters / 6 KiB of packed data; the tail is omitted on
 * exceeding either budget. Chapter count reports only that retained prefix. */
esp_err_t passport_catalog_fetch_chapters(int channel, int episode);
esp_err_t passport_catalog_fetch_chapters_for_episode(int channel, const passport_episode_t *snapshot);
/* These identity getters query the one retained timeline with no Flash/network
 * I/O or allocation. Refresh, page eviction and aged-out indexes do not evict it. */
size_t passport_catalog_chapter_count_by_identity(int channel, const char *guid, const char *url);
bool passport_catalog_get_chapter_by_identity(int channel, const char *guid, const char *url,
                                               size_t index, passport_chapter_t *out);
bool passport_catalog_current_chapter_by_identity(int channel, const char *guid, const char *url,
    uint32_t position_seconds, passport_chapter_t *out, size_t *index);
bool passport_catalog_load_resume(passport_resume_t *out);
esp_err_t passport_catalog_save_resume(int channel, int episode, uint32_t seconds, bool completed);
/* Snapshot preserves identity even when the playing episode leaves the feed.
 * snapshot.episode may be UINT8_MAX; a real channel and HTTP(S) URL are required. */
esp_err_t passport_catalog_save_playback(const passport_resume_t *snapshot);
/* Loaded identity includes an explicitly empty timeline. Does not fetch. */
bool passport_catalog_timeline_ready(int channel,const char *guid,const char *url);
/* Release retained timeline before station startup; catalog/history stay intact. */
void passport_catalog_release_timeline(void);

#ifdef __cplusplus
}
#endif
