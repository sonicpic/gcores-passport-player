#include "passport_fm.h"
#include "passport_fm_stream.h"
#include "passport_fm_metadata.h"
#include "nvs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FM_URL_BYTES 768
#define FM_RX_BYTES 1024
#define FM_JSON_BYTES 12288
#define FM_MANIFEST_BYTES (512 * 1024)
#define FM_SEGMENT_BYTES (4 * 1024 * 1024)
#define FM_TIMEOUT_MS 3000
#define FM_CACHED_URI_BYTES 192

static const char *TAG = "passport_fm";
static const char *LATEST_URL =
    "https://www.gcores.com/gapi/v1/radios?filter%5Bis-fm%5D=1&page%5Blimit%5D=1&sort=-published-at";

/* Creator-approved cleartext transport is restricted to public official FM
 * media. API metadata and all other hosts retain verified HTTPS. */
static bool allowed_transport(const char *url) {
    const char *media = "http://alioss.gcores.com/uploads/fm_programs/";
    return url && (!strncmp(url,"https://",8) || !strncmp(url,media,strlen(media)));
}
static void media_transport(char *url) {
    const char *media = "https://alioss.gcores.com/uploads/fm_programs/";
    if (!strncmp(url,media,strlen(media))) memmove(url+4,url+5,strlen(url+5)+1);
}

typedef struct {
    char url[FM_CACHED_URI_BYTES];
    uint64_t start_ms;
    uint32_t index;
} fm_cached_segment_t;

typedef struct {
    uint32_t generation;
    bool (*cancelled)(uint32_t);
    bool (*emit)(const uint8_t *, size_t, void *);
    void *emit_context;
    uint32_t (*elapsed)(void *);
    uint32_t start_s, duration_s, next_at_s, last_position;
    bool names_ready;
    uint8_t *fallback_table;
    size_t fallback_size;
    union { uint8_t rx[FM_RX_BYTES]; char request_url[FM_URL_BYTES]; } scratch;
    char manifest_url[384];
    union { passport_fm_playlist_t playlist; passport_fm_ts_t ts; } parser;
    fm_cached_segment_t segments[3];
    unsigned segment_count;
    uint32_t total_segments;
    uint8_t adts_header[7];
    size_t adts_used, adts_remaining;
    uint64_t skip_us;
    bool forwarding;
} fm_context_t;

/* A single replaceable NVS blob, never one key per programme. */
typedef struct {
    uint32_t magic, identity, fetched_s, table_size;
    char manifest_url[384];
    uint8_t table[];
} fm_name_cache_t;
#define FM_NAMES_MAGIC UINT32_C(0x464d4e31)
static portMUX_TYPE names_lock = portMUX_INITIALIZER_UNLOCKED;
static char names_current[385], names_next[385];

void passport_fm_get_titles(char *current, size_t current_size, char *next, size_t next_size) {
    taskENTER_CRITICAL(&names_lock);
    if (current && current_size) snprintf(current,current_size,"%s",names_current);
    if (next && next_size) snprintf(next,next_size,"%s",names_next);
    taskEXIT_CRITICAL(&names_lock);
}
static void clear_names(void) {
    taskENTER_CRITICAL(&names_lock);
    names_current[0]=names_next[0]=0;
    taskEXIT_CRITICAL(&names_lock);
}
static fm_name_cache_t *read_names(void) {
    nvs_handle_t h;
    if (nvs_open("gpp_fm",NVS_READONLY,&h)!=ESP_OK) return NULL;
    size_t size=0;
    fm_name_cache_t *cache=NULL;
    if (nvs_get_blob(h,"names",NULL,&size)==ESP_OK && size>=sizeof(*cache)+2 &&
        size<=sizeof(*cache)+PASSPORT_FM_METADATA_MAX_TABLE) {
        cache=malloc(size);
        if (cache && (nvs_get_blob(h,"names",cache,&size)!=ESP_OK ||
            cache->magic!=FM_NAMES_MAGIC || cache->table_size!=size-sizeof(*cache) ||
            !memchr(cache->manifest_url,0,sizeof(cache->manifest_url)))) {
            free(cache); cache=NULL;
        }
    }
    nvs_close(h);
    return cache;
}
static void update_names(fm_context_t *fm, uint32_t position) {
    if (!fm->names_ready || (position>=fm->last_position && position<fm->next_at_s)) {
        fm->last_position=position; return;
    }
    fm_name_cache_t *cache=fm->fallback_table?NULL:read_names();
    const uint8_t *table=fm->fallback_table?fm->fallback_table:cache?cache->table:NULL;
    size_t size=fm->fallback_table?fm->fallback_size:cache?cache->table_size:0;
    passport_fm_program_t *window=table?malloc(3*sizeof(*window)):NULL;
    if (!table) fm->names_ready=false;
    if (window && passport_fm_metadata_lookup(table,size,position,window)) {
        taskENTER_CRITICAL(&names_lock);
        memcpy(names_current,window[0].title,sizeof(names_current));
        memcpy(names_next,window[1].title,sizeof(names_next));
        taskEXIT_CRITICAL(&names_lock);
        fm->next_at_s=window[1].at_s>position?window[1].at_s:fm->duration_s;
        fm->last_position=position;
    } else if (window) fm->names_ready=false;
    else if (table) {
        /* Allocation pressure never turns each AAC chunk into a Flash read. */
        fm->last_position=position;
        fm->next_at_s=position+5<fm->duration_s?position+5:fm->duration_s;
    }
    free(window); free(cache);
}
static uint32_t name_identity(const char *id, int64_t published, uint32_t duration) {
    uint32_t hash=2166136261u;
    while (*id) hash=(hash^(uint8_t)*id++)*16777619u;
    return hash^(uint32_t)published^duration;
}
static void memory_log(const char *phase) {
    ESP_LOGI(TAG, "%s heap=%u largest=%u min=%u stack_free=%u", phase,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static bool stopped(const fm_context_t *fm) {
    return fm->cancelled(fm->generation);
}

/* ISO-8601 timestamps supplied by Gcores include an explicit UTC offset.
 * Convert civil dates without changing the process TZ or depending on NTP's
 * local-time configuration. */
static int64_t publication_time(const char *text) {
    int y, m, d, hour, minute, second, tz_hour = 0, tz_minute = 0, used = 0;
    if (!text || sscanf(text, "%d-%d-%dT%d:%d:%d%n", &y, &m, &d,
                        &hour, &minute, &second, &used) != 6 ||
        y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return 0;
    const char *zone = text + used;
    if (*zone == '.') {
        ++zone;
        if (*zone < '0' || *zone > '9') return 0;
        while (*zone >= '0' && *zone <= '9') ++zone;
    }
    int sign = 0;
    if (*zone == 'Z' && zone[1] == 0) {
        sign = 0;
    } else if ((*zone == '+' || *zone == '-') &&
               sscanf(zone + 1, "%d:%d%n", &tz_hour, &tz_minute, &used) == 2 &&
               zone[used + 1] == 0 && tz_hour >= 0 && tz_hour <= 23 &&
               tz_minute >= 0 && tz_minute <= 59) {
        sign = *zone == '+' ? 1 : -1;
    } else return 0;
    y -= m <= 2;
    int era = y / 400;
    unsigned year = (unsigned)(y - era * 400);
    unsigned day = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned days = year * 365 + year / 4 - year / 100 + day;
    return ((int64_t)era * 146097 + days - 719468) * 86400 +
           hour * 3600 + minute * 60 + second - sign * (tz_hour * 3600 + tz_minute * 60);
}

static uint64_t station_position_ms(int64_t now, int64_t published, uint32_t duration) {
    if (!duration || published <= 0 || now < 1700000000 || now < published) return 0;
    return (uint64_t)((now - published) % duration) * 1000;
}

static esp_http_client_handle_t open_resource_once(fm_context_t *fm, const char *url,
                                                  esp_err_t *error) {
    *error = ESP_ERR_INVALID_STATE;
    if (stopped(fm)) return NULL;
    if (!allowed_transport(url)) {
        *error = ESP_ERR_NOT_SUPPORTED;
        return NULL;
    }
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = FM_TIMEOUT_MS,
        .buffer_size = FM_RX_BYTES, .buffer_size_tx = 1024,
        .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "GcoresPassportPlayer/1.0", .keep_alive_enable = true,
    };
    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) { *error = ESP_ERR_NO_MEM; return NULL; }
    esp_http_client_set_header(http, "Accept-Encoding", "identity");
    for (unsigned redirects = 0; redirects <= 3; ++redirects) {
        if (stopped(fm)) break;
        *error = esp_http_client_open(http, 0);
        if (*error != ESP_OK) break;
        if (esp_http_client_fetch_headers(http) < 0) { *error = ESP_FAIL; break; }
        int status = esp_http_client_get_status_code(http);
        if (status == 200) { *error = ESP_OK; return http; }
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
            *error = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        esp_http_client_close(http);
        *error = esp_http_client_set_redirection(http);
        if (*error != ESP_OK) break;
        if (esp_http_client_get_url(http, fm->scratch.request_url, sizeof(fm->scratch.request_url)) != ESP_OK ||
            !allowed_transport(fm->scratch.request_url)) {
            *error = ESP_ERR_NOT_SUPPORTED;
            break;
        }
        *error = ESP_ERR_INVALID_RESPONSE;
    }
    if (stopped(fm)) *error = ESP_ERR_INVALID_STATE;
    esp_http_client_cleanup(http);
    return NULL;
}

static esp_http_client_handle_t open_resource(fm_context_t *fm, const char *url,
                                             esp_err_t *error) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        esp_http_client_handle_t http = open_resource_once(fm, url, error);
        if (http || stopped(fm) || *error == ESP_ERR_NO_MEM ||
            *error == ESP_ERR_NOT_SUPPORTED || *error == ESP_ERR_INVALID_RESPONSE)
            return http;
        vTaskDelay(pdMS_TO_TICKS(150 * (attempt + 1)));
    }
    return NULL;
}

static cJSON *fetch_json(fm_context_t *fm, const char *url, esp_err_t *error) {
    esp_http_client_handle_t http = open_resource(fm, url, error);
    if (!http) return NULL;
    int64_t declared = esp_http_client_is_chunked_response(http) ? -1 : esp_http_client_get_content_length(http);
    size_t capacity = declared > 0 && declared <= FM_JSON_BYTES ? (size_t)declared : FM_JSON_BYTES;
    char *body = declared > FM_JSON_BYTES ? NULL : malloc(capacity + 1);
    cJSON *json = NULL;
    bool complete = false;
    if (!body) { *error = declared > FM_JSON_BYTES ? ESP_ERR_INVALID_SIZE : ESP_ERR_NO_MEM; goto done; }
    size_t used = 0;
    while (!stopped(fm)) {
        int n = esp_http_client_read(http, (char *)fm->scratch.rx, sizeof(fm->scratch.rx));
        if (n <= 0) {
            if (n == 0 && esp_http_client_is_complete_data_received(http)) {
                body[used] = 0;
                complete = true;
                *error = ESP_OK;
            } else *error = ESP_FAIL;
            break;
        }
        if ((size_t)n > capacity - used) { *error = ESP_ERR_INVALID_SIZE; break; }
        memcpy(body + used, fm->scratch.rx, (size_t)n);
        used += (size_t)n;
    }
    if (stopped(fm)) *error = ESP_ERR_INVALID_STATE;
done:
    /* TLS is gone before allocating the JSON tree: API metadata must not
     * overlap two TLS sessions or the AAC decoder's large allocations. */
    esp_http_client_cleanup(http);
    if (complete && !stopped(fm)) {
        json = cJSON_Parse(body);
        *error = json ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    free(body);
    return json;
}

static const cJSON *field(const cJSON *object, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static bool fetch_names(fm_context_t *fm, uint32_t identity) {
    fm_name_cache_t *cache=calloc(1,sizeof(*cache)+PASSPORT_FM_METADATA_MAX_TABLE);
    passport_fm_metadata_t *parser=malloc(sizeof(*parser));
    if (!cache || !parser) { free(cache); free(parser); return false; }
    passport_fm_metadata_init(parser,cache->table,PASSPORT_FM_METADATA_MAX_TABLE);
    esp_err_t error;
    esp_http_client_handle_t http=open_resource(fm,fm->scratch.request_url,&error);
    bool valid=false;
    size_t total=0;
    while (http && !stopped(fm)) {
        int n=esp_http_client_read(http,(char *)fm->scratch.rx,sizeof(fm->scratch.rx));
        if (n<=0) {
            valid=n==0 && esp_http_client_is_complete_data_received(http) && passport_fm_metadata_finish(parser);
            break;
        }
        total+=(size_t)n;
        if (total>65536 || !passport_fm_metadata_feed(parser,fm->scratch.rx,(size_t)n)) break;
    }
    if (http) esp_http_client_cleanup(http);
    valid=valid && !stopped(fm) && !strncmp(parser->audio_url,"https://",8) &&
          strlen(parser->audio_url)<sizeof(fm->manifest_url);
    if (valid) {
        strcpy(fm->manifest_url,parser->audio_url);
        media_transport(fm->manifest_url);
        if (parser->count) {
            cache->magic=FM_NAMES_MAGIC; cache->identity=identity;
            cache->fetched_s=(uint32_t)time(NULL); cache->table_size=parser->used;
            strcpy(cache->manifest_url,fm->manifest_url);
            nvs_handle_t h;
            bool saved=false;
            if (nvs_open("gpp_fm",NVS_READWRITE,&h)==ESP_OK) {
                saved=nvs_set_blob(h,"names",cache,sizeof(*cache)+cache->table_size)==ESP_OK && nvs_commit(h)==ESP_OK;
                nvs_close(h);
            }
            fm->names_ready=true;
            if (!saved) {
                fm->fallback_table=malloc(parser->used);
                if (fm->fallback_table) { memcpy(fm->fallback_table,cache->table,parser->used); fm->fallback_size=parser->used; }
                else fm->names_ready=false;
            }
            ESP_LOGI(TAG,"programme_names count=%u table=%u flash=%d parser=%u",parser->count,(unsigned)parser->used,saved,(unsigned)sizeof(*parser));
        }
    }
    free(parser); free(cache);
    return valid;
}

static esp_err_t station(fm_context_t *fm, int64_t *published, uint32_t *duration) {
    esp_err_t err;
    cJSON *json = fetch_json(fm, LATEST_URL, &err);
    if (!json) return err;
    const cJSON *radio = cJSON_GetArrayItem(field(json, "data"), 0);
    const cJSON *id = field(radio, "id"), *attrs = field(radio, "attributes");
    const cJSON *date = field(attrs, "published-at"), *seconds = field(attrs, "duration");
    bool valid = cJSON_IsString(id) && strlen(id->valuestring) > 0 && strlen(id->valuestring) <= 16 &&
                 cJSON_IsString(date) && cJSON_IsNumber(seconds) &&
                 seconds->valuedouble > 0 && seconds->valuedouble <= 7 * 86400;
    if (valid) {
        for (const char *p = id->valuestring; *p; ++p) if (*p < '0' || *p > '9') valid = false;
        *published = publication_time(date->valuestring);
        *duration = (uint32_t)seconds->valuedouble;
        valid = valid && *published > 0;
    }
    char radio_id[17]={0};
    uint32_t identity=0;
    if (valid) { strcpy(radio_id,id->valuestring); identity=name_identity(radio_id,*published,*duration); }
    cJSON_Delete(json);
    if (!valid) return ESP_ERR_INVALID_RESPONSE;
    fm->duration_s=*duration;
    fm_name_cache_t *cache=read_names();
    int64_t now=time(NULL);
    if (cache && cache->identity==identity && allowed_transport(cache->manifest_url) &&
        now>=1700000000 && now>=cache->fetched_s && now-cache->fetched_s<86400) {
        strcpy(fm->manifest_url,cache->manifest_url); fm->names_ready=true;
        free(cache); memory_log("names_cache_hit"); return ESP_OK;
    }
    free(cache);
    snprintf(fm->scratch.request_url,sizeof(fm->scratch.request_url),
        "https://www.gcores.com/gapi/v1/radios/%s?include=media.timelines&fields%%5Bradios%%5D=published-at,duration,media&fields%%5Bmedias%%5D=audio,duration&fields%%5Btimelines%%5D=at,title",radio_id);
    if (fetch_names(fm,identity)) { memory_log("names_api_release"); return ESP_OK; }
    if (stopped(fm)) return ESP_ERR_INVALID_STATE;
    /* Programme metadata is optional; its failure must not prevent audio. */
    snprintf(fm->scratch.request_url,sizeof(fm->scratch.request_url),
        "https://www.gcores.com/gapi/v1/radios/%s?include=media",radio_id);
    json = fetch_json(fm, fm->scratch.request_url, &err);
    if (!json) return err;
    const cJSON *included = field(json, "included"), *entry;
    const cJSON *relationship = field(field(field(field(json, "data"), "relationships"), "media"), "data");
    const cJSON *media_id = field(relationship, "id");
    valid = false;
    cJSON_ArrayForEach(entry, included) {
        const cJSON *type = field(entry, "type"), *entry_id = field(entry, "id");
        if (!cJSON_IsString(type) || strcmp(type->valuestring, "medias") ||
            !cJSON_IsString(media_id) || !cJSON_IsString(entry_id) ||
            strcmp(entry_id->valuestring, media_id->valuestring)) continue;
        const cJSON *audio = field(field(entry, "attributes"), "audio");
        if (cJSON_IsString(audio) && !strncmp(audio->valuestring, "https://", 8) &&
            strlen(audio->valuestring) < sizeof(fm->manifest_url)) {
            memcpy(fm->manifest_url, audio->valuestring, strlen(audio->valuestring) + 1);
            media_transport(fm->manifest_url);
            valid = true;
        }
        break;
    }
    cJSON_Delete(json);
    memory_log("api_release");
    return valid ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t manifest(fm_context_t *fm, uint64_t target_ms, int32_t index) {
    esp_err_t err;
    esp_http_client_handle_t http = open_resource(fm, fm->manifest_url, &err);
    if (!http) return err;
    passport_fm_playlist_init(&fm->parser.playlist, target_ms, index);
    size_t bytes = 0;
    while (!stopped(fm)) {
        int n = esp_http_client_read(http, (char *)fm->scratch.rx, sizeof(fm->scratch.rx));
        if (n <= 0) {
            err = n == 0 && esp_http_client_is_complete_data_received(http) &&
                  passport_fm_playlist_finish(&fm->parser.playlist) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
            break;
        }
        bytes += (size_t)n;
        if (bytes > FM_MANIFEST_BYTES ||
            !passport_fm_playlist_feed(&fm->parser.playlist, fm->scratch.rx, (size_t)n)) {
            err = ESP_ERR_NOT_SUPPORTED; break;
        }
    }
    if (stopped(fm)) err = ESP_ERR_INVALID_STATE;
    esp_http_client_cleanup(http);
    if (err == ESP_OK) {
        const passport_fm_playlist_t *p = &fm->parser.playlist;
        fm->segment_count = 0;
        fm->total_segments = p->count;
        for (unsigned i = 0; i <= p->next_count; ++i) {
            const passport_fm_segment_t *s = i ? &p->next[i - 1] : &p->segment;
            if (strlen(s->url) >= FM_CACHED_URI_BYTES) return ESP_ERR_INVALID_SIZE;
            fm_cached_segment_t *cached = &fm->segments[fm->segment_count++];
            memcpy(cached->url, s->url, strlen(s->url) + 1);
            cached->start_ms = s->start_ms;
            cached->index = s->index;
        }
    }
    return err;
}

/* Only the first segment needs intra-segment synchronization. Inspect ADTS
 * headers while discarding compressed AAC frames, never allocate a segment or
 * decode a minute of discarded PCM. Once positioned, forward bytes directly. */
static bool audio_sink(void *context, const uint8_t *data, size_t n) {
    static const uint32_t rates[] = {96000, 88200, 64000, 48000, 44100, 32000,
                                    24000, 22050, 16000, 12000, 11025, 8000, 7350};
    fm_context_t *fm = context;
    while (n && !stopped(fm)) {
        if (fm->forwarding) {
            if (fm->elapsed && fm->duration_s) update_names(fm,(fm->start_s+fm->elapsed(fm->emit_context))%fm->duration_s);
            return fm->emit(data, n, fm->emit_context);
        }
        if (fm->adts_remaining) {
            size_t consume = n < fm->adts_remaining ? n : fm->adts_remaining;
            fm->adts_remaining -= consume;
            data += consume; n -= consume;
            continue;
        }
        size_t take = 7 - fm->adts_used;
        if (take > n) take = n;
        memcpy(fm->adts_header + fm->adts_used, data, take);
        fm->adts_used += take; data += take; n -= take;
        if (fm->adts_used != 7) continue;
        const uint8_t *h = fm->adts_header;
        unsigned rate = (h[2] >> 2) & 15;
        size_t length = ((size_t)(h[3] & 3) << 11) | ((size_t)h[4] << 3) | (h[5] >> 5);
        if (h[0] != 0xff || (h[1] & 0xf6) != 0xf0 || rate >= 13 ||
            length < ((h[1] & 1) ? 7u : 9u)) return false;
        uint64_t frame_us = UINT64_C(1024000000) * (1 + (h[6] & 3)) / rates[rate];
        fm->adts_used = 0;
        if (fm->skip_us < frame_us) {
            fm->forwarding = true;
            memory_log("adts_prefill");
            ESP_LOGI(TAG, "synchronized_adts");
            if (!fm->emit(h, 7, fm->emit_context)) return false;
        } else {
            fm->skip_us -= frame_us;
            fm->adts_remaining = length - 7;
        }
    }
    return !stopped(fm);
}

static esp_err_t segment(fm_context_t *fm, const fm_cached_segment_t *selected) {
    if (!passport_fm_resolve_url(fm->manifest_url, selected->url,
                                fm->scratch.request_url, sizeof(fm->scratch.request_url))) return ESP_ERR_NOT_SUPPORTED;
    media_transport(fm->scratch.request_url);
    esp_err_t err;
    esp_http_client_handle_t http = open_resource(fm, fm->scratch.request_url, &err);
    if (!http) return err;
    memory_log("segment_http_open");
    passport_fm_ts_init(&fm->parser.ts, audio_sink, fm);
    size_t bytes = 0;
    while (!stopped(fm)) {
        int n = esp_http_client_read(http, (char *)fm->scratch.rx, sizeof(fm->scratch.rx));
        if (n <= 0) {
            err = n == 0 && esp_http_client_is_complete_data_received(http) &&
                  passport_fm_ts_finish(&fm->parser.ts) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
            break;
        }
        bytes += (size_t)n;
        if (bytes > FM_SEGMENT_BYTES || !passport_fm_ts_feed(&fm->parser.ts, fm->scratch.rx, (size_t)n)) {
            err = ESP_ERR_INVALID_RESPONSE; break;
        }
    }
    if (stopped(fm)) err = ESP_ERR_INVALID_STATE;
    esp_http_client_cleanup(http);
    return err;
}

esp_err_t passport_fm_run(uint32_t generation, bool (*cancelled)(uint32_t),
                         bool (*emit)(const uint8_t *, size_t, void *),
                         esp_err_t (*prepare)(void *), uint32_t (*elapsed)(void *), void *ctx) {
    if (!cancelled || !emit || !prepare) return ESP_ERR_INVALID_ARG;
    fm_context_t *fm = calloc(1, sizeof(*fm));
    if (!fm) return ESP_ERR_NO_MEM;
    fm->generation = generation; fm->cancelled = cancelled;
    clear_names();
    fm->emit = emit; fm->emit_context = ctx; fm->elapsed=elapsed;
    int64_t published = 0;
    uint32_t duration = 0;
    esp_err_t err = station(fm, &published, &duration);
    /* A short NTP grace period is bounded and cancellation-aware. Without a
     * trustworthy wall clock, play the beginning rather than invent a date. */
    for (unsigned wait = 0; err == ESP_OK && time(NULL) < 1700000000 && wait < 30 && !stopped(fm); ++wait)
        vTaskDelay(pdMS_TO_TICKS(100));
    int64_t now = time(NULL);
    uint64_t target_ms = err == ESP_OK ? station_position_ms(now, published, duration) : 0;
    fm->start_s=(uint32_t)(target_ms/1000);
    fm->next_at_s=0;
    if (err==ESP_OK) update_names(fm,fm->start_s);
    if (err == ESP_OK) err = manifest(fm, target_ms, -1);
    if (err == ESP_OK && !stopped(fm)) {
        memory_log("decoder_prepare");
        err = prepare(ctx);
    }
    while (err == ESP_OK && !stopped(fm)) {
        const fm_cached_segment_t *selected = &fm->segments[0];
        fm->skip_us = target_ms > selected->start_ms ?
                      (target_ms - selected->start_ms) * 1000 : 0;
        fm->forwarding = fm->skip_us == 0;
        fm->adts_used = fm->adts_remaining = 0;
        err = segment(fm, selected);
        if (err != ESP_OK) {
            /* Never splice a truncated PES/ADTS frame into the decoder. A
             * segment failure ends the session; caller can reopen at FM time. */
            break;
        }
        target_ms = 0;
        uint32_t next_index = selected->index + 1;
        if (--fm->segment_count) {
            memmove(fm->segments, fm->segments + 1, fm->segment_count * sizeof(fm->segments[0]));
        } else if (next_index < fm->total_segments) {
            err = manifest(fm, 0, (int32_t)next_index);
        } else {
            /* The official station loops this finite curated playlist. */
            err = manifest(fm, 0, 0);
        }
    }
    if (stopped(fm)) err = ESP_ERR_INVALID_STATE;
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "stream_error=%d", err);
    free(fm->fallback_table);
    free(fm);
    return err;
}
