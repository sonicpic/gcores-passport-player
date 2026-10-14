#include "passport_audio.h"
#include "passport_mp3_seek.h"
#include "passport_audio_http.h"
#include "bsp_audio.h"
#include "esp_audio_simple_dec.h"
#include "esp_mp3_dec.h"
#include "esp_aac_dec.h"
#include "esp_pcm_dec.h"
#include "esp_m4a_dec.h"
#include "esp_wav_dec.h"
#include "esp_audio_dec_reg.h"
#include "esp_audio_simple_dec_reg.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <inttypes.h>

#define URL_LIMIT 1024
#define STREAM_BYTES (12 * 1024)
#define INPUT_BYTES 3072
#define PCM_BYTES 8192
#define PCM_INITIAL_BYTES (1152 * 2 * sizeof(int16_t))
#define NETWORK_BYTES 1024
#define NET_IDLE (1U << 0)
#define NET_TIMEOUT_MS 3000
#define NET_STOP_WAIT_MS 6500
#define HEADER_SCAN_LIMIT (256 * 1024)

typedef enum { CMD_PLAY, CMD_PAUSE, CMD_STOP, CMD_SEEK, CMD_VOLUME } command_type_t;
typedef struct { command_type_t type; uint32_t value, seek_id; char *url; } command_t;
typedef struct { uint32_t generation; const char *url; uint64_t offset, total_bytes; char validator[128]; } net_request_t;
typedef struct {
    uint32_t generation;
    bool done;
    esp_err_t error;
    uint64_t total_bytes;
    char mime[80];
    char validator[128];
} net_status_t;
typedef struct {
    char mime[80];
    passport_http_response_t headers;
} response_t;

static const char *TAG = "passport_audio";
static QueueHandle_t s_commands, s_net_requests;
static StreamBufferHandle_t s_stream;
static EventGroupHandle_t s_net_events;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation;
static uint32_t s_seek_active_id;
static net_status_t s_net;
static passport_audio_status_t s_status = { .state = PASSPORT_AUDIO_IDLE, .volume = 45 };
static bool s_quiescent = true;
static esp_pm_lock_handle_t s_compute_lock;

static void decoder_unregister(void) {
    // 2.6.2 declares simple_dec_unregister_all but does not export it in the
    // C3 archive. Use the actual per-codec public APIs, including partial init.
    (void)esp_m4a_dec_unregister();
    (void)esp_wav_dec_unregister();
    esp_audio_dec_unregister(ESP_AUDIO_TYPE_MP3);
    esp_audio_dec_unregister(ESP_AUDIO_TYPE_AAC);
    esp_audio_dec_unregister(ESP_AUDIO_TYPE_PCM);
}

/* Maximum CPU frequency only during codec computation. Each return follows
 * release; HTTP, FIFO waits and PCM DMA delivery remain eligible for DFS. */
static esp_audio_err_t decoder_open(esp_audio_simple_dec_cfg_t *cfg,
                                    esp_audio_simple_dec_handle_t *decoder) {
    if (s_compute_lock && esp_pm_lock_acquire(s_compute_lock) != ESP_OK) return ESP_AUDIO_ERR_FAIL;
    esp_audio_err_t r = esp_audio_simple_dec_open(cfg, decoder);
    if (s_compute_lock) esp_pm_lock_release(s_compute_lock);
    return r;
}
static esp_audio_err_t decoder_process(esp_audio_simple_dec_handle_t decoder,
                                       esp_audio_simple_dec_raw_t *raw,
                                       esp_audio_simple_dec_out_t *out) {
    if (s_compute_lock && esp_pm_lock_acquire(s_compute_lock) != ESP_OK) return ESP_AUDIO_ERR_FAIL;
    esp_audio_err_t r = esp_audio_simple_dec_process(decoder, raw, out);
    if (s_compute_lock) esp_pm_lock_release(s_compute_lock);
    return r;
}

static void resource_log(const char *phase, uint32_t decode_us) {
    ESP_LOGI(TAG, "%s heap=%u min=%u largest=%u stack_free=%u decode_us=%lu",
             phase, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned long)decode_us);
}

/* Keep diagnostics numeric: enclosure URLs and network identifiers are private.
 * Log before cleanup, while allocation pressure and codec errors are visible. */
static void failure_log(const char *stage, int error) {
    ESP_LOGE(TAG, "failure stage=%s error=%d heap=%u largest=%u", stage, error,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

/* A stereo MPEG frame needs at most 1152 * 2 * 2 bytes. Retain the previous
 * 8 KiB ceiling for container/PCM outputs that request more, without charging
 * every MP3 session for it. Failed realloc leaves the live buffer untouched. */
static esp_err_t pcm_grow(uint8_t **pcm, size_t *bytes, uint32_t needed) {
    if (needed <= *bytes || needed > PCM_BYTES) return ESP_ERR_INVALID_SIZE;
    uint8_t *grown = realloc(*pcm, PCM_BYTES);
    if (!grown) return ESP_ERR_NO_MEM;
    *pcm = grown;
    *bytes = PCM_BYTES;
    return ESP_OK;
}

static uint32_t generation_get(void) {
    taskENTER_CRITICAL(&s_lock);
    uint32_t g = s_generation;
    taskEXIT_CRITICAL(&s_lock);
    return g;
}
static uint32_t generation_next(void) {
    taskENTER_CRITICAL(&s_lock);
    uint32_t g = ++s_generation;
    taskEXIT_CRITICAL(&s_lock);
    return g;
}
static net_status_t network_status(void) {
    taskENTER_CRITICAL(&s_lock);
    net_status_t n = s_net;
    taskEXIT_CRITICAL(&s_lock);
    return n;
}
static void publish(passport_audio_state_t state, esp_err_t error) {
    taskENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.last_error = error;
    if ((state==PASSPORT_AUDIO_PLAYING && s_seek_active_id==s_status.seek_request_id) ||
        state==PASSPORT_AUDIO_ERROR || state==PASSPORT_AUDIO_IDLE || state==PASSPORT_AUDIO_ENDED) {
        s_status.seeking=false;
        s_status.seek_complete_id=s_status.seek_request_id;
    }
    taskEXIT_CRITICAL(&s_lock);
}
static void quiescent_set(bool q) {
    taskENTER_CRITICAL(&s_lock);
    s_quiescent = q;
    taskEXIT_CRITICAL(&s_lock);
}
void passport_audio_get_status(passport_audio_status_t *status) {
    if (!status) return;
    taskENTER_CRITICAL(&s_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_lock);
}
bool passport_audio_is_quiescent(void) {
    taskENTER_CRITICAL(&s_lock);
    bool q = s_quiescent;
    taskEXIT_CRITICAL(&s_lock);
    return q && (!s_commands || uxQueueMessagesWaiting(s_commands) == 0);
}

static esp_err_t http_event(esp_http_client_event_t *event) {
    response_t *response=event->user_data;
    if (event->event_id!=HTTP_EVENT_ON_HEADER || !response) return ESP_OK;
    if (!strcasecmp(event->header_key,"Content-Type"))
        snprintf(response->mime,sizeof(response->mime),"%s",event->header_value);
    passport_http_header(&response->headers,event->header_key,event->header_value);
    return ESP_OK;
}

/* Manual redirects preserve TLS verification and reject HTTPS downgrades.
 * A retry resumes at the number of encoded bytes actually put in the FIFO,
 * retaining parser state and MP3 bit reservoir; no byte/time approximation. */
static esp_err_t http_open(esp_http_client_handle_t http, response_t *response,
                           const char *url, uint64_t offset, uint32_t generation,
                           const char *validator, uint64_t expected_total) {
    esp_err_t err = esp_http_client_set_url(http, url);
    if (err != ESP_OK) return err;
    char range[48];
    esp_http_client_set_header(http,"Accept-Encoding","identity");
    if (offset) {
        snprintf(range, sizeof(range), "bytes=%" PRIu64 "-", offset);
        esp_http_client_set_header(http, "Range", range);
        esp_http_client_set_header(http, "If-Range", validator);
    } else {
        esp_http_client_delete_header(http, "Range");
        esp_http_client_delete_header(http, "If-Range");
    }
    bool secure = !strncasecmp(url, "https://", 8);
    for (unsigned redirect = 0; redirect <= 5; ++redirect) {
        if (generation != generation_get()) return ESP_ERR_INVALID_STATE;
        memset(response, 0, sizeof(*response));
        err = esp_http_client_open(http, 0);
        if (err != ESP_OK) return err;
        if (esp_http_client_fetch_headers(http) < 0) {
            failure_log("http_headers", esp_http_client_get_errno(http));
            return ESP_FAIL;
        }
        int status = esp_http_client_get_status_code(http);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (redirect == 5) return ESP_ERR_INVALID_RESPONSE;
            esp_http_client_close(http);
            err = esp_http_client_set_redirection(http);
            if (err != ESP_OK) return err;
            char next_url[URL_LIMIT];
            if (esp_http_client_get_url(http, next_url, sizeof(next_url)) != ESP_OK)
                return ESP_ERR_INVALID_SIZE;
            if ((secure && strncasecmp(next_url, "https://", 8)) ||
                (strncasecmp(next_url, "https://", 8) && strncasecmp(next_url, "http://", 7)))
                return ESP_ERR_INVALID_RESPONSE;
            secure |= !strncasecmp(next_url, "https://", 8);
            continue;
        }
        uint64_t full_total;
        passport_http_decision_t decision=passport_http_validate(&response->headers,status,
            esp_http_client_is_chunked_response(http) ? -1 : esp_http_client_get_content_length(http),
            offset,expected_total,validator,&full_total);
        if (decision!=PASSPORT_HTTP_ACCEPT) {
            failure_log("http_identity",status);
            return offset ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_RESPONSE;
        }
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static void network_worker(void *arg) {
    (void)arg;
    net_request_t request;
    for (;;) {
        xQueueReceive(s_net_requests, &request, portMAX_DELAY);
        uint8_t *buffer = malloc(NETWORK_BYTES);
        esp_err_t result = buffer ? ESP_OK : ESP_ERR_NO_MEM;
        uint64_t delivered = 0;
        response_t response = {0};
        esp_http_client_config_t config = {
            .url = request.url, .timeout_ms = NET_TIMEOUT_MS,
            .buffer_size = NETWORK_BYTES, .buffer_size_tx = 1024,
            .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = http_event, .user_data = &response,
            .user_agent = "GcoresPassportPlayer/1.0", .keep_alive_enable = true,
        };
        esp_http_client_handle_t http = buffer ? esp_http_client_init(&config) : NULL;
        if (!http) result = ESP_ERR_NO_MEM;
        if (result == ESP_OK) result = http_open(http, &response, request.url, request.offset, request.generation,
                                                request.validator, request.total_bytes);
        if (result != ESP_OK && request.generation == generation_get())
            failure_log(http ? "http_open" : "http_alloc", result);
        uint64_t full_total=request.total_bytes;
        if (result == ESP_OK) {
            int64_t length = esp_http_client_is_chunked_response(http) ? -1 : esp_http_client_get_content_length(http);
            full_total=request.offset ? response.headers.range_total : length>0 ? (uint64_t)length : 0;
            taskENTER_CRITICAL(&s_lock);
            if (s_net.generation==request.generation && s_generation==request.generation) {
                s_net.total_bytes = full_total;
                memcpy(s_net.mime, response.mime, sizeof(s_net.mime));
                memcpy(s_net.validator, request.offset ? request.validator : response.headers.validator, sizeof(s_net.validator));
            }
            taskEXIT_CRITICAL(&s_lock);
        }
        char validator[128];
        memcpy(validator, request.offset ? request.validator : response.headers.validator, sizeof(validator));
        unsigned retries = 0;
        while (result == ESP_OK && request.generation == generation_get()) {
            int count = esp_http_client_read(http, (char *)buffer, NETWORK_BYTES);
            if (count <= 0) {
                if (count == 0 && esp_http_client_is_complete_data_received(http)) {
                    if (full_total && request.offset+delivered!=full_total) result=ESP_ERR_NOT_SUPPORTED;
                    break;
                }
                if (++retries > 2) {
                    failure_log("http_read", esp_http_client_get_errno(http));
                    result = ESP_FAIL; break;
                }
                // A changed resource must never be spliced into decoder state.
                // If-Range forces a 200 on validator mismatch; http_open rejects it.
                if (request.offset+delivered && !validator[0]) {
                    failure_log("http_resume_validator", ESP_ERR_NOT_SUPPORTED);
                    result = ESP_ERR_NOT_SUPPORTED; break;
                }
                esp_http_client_close(http);
                vTaskDelay(pdMS_TO_TICKS(200 * retries));
                result = http_open(http, &response, request.url, request.offset+delivered, request.generation,
                                   validator, full_total);
                if (result != ESP_OK && request.generation == generation_get())
                    failure_log("http_resume", result);
                continue;
            }
            size_t sent = 0;
            while (sent < (size_t)count && request.generation == generation_get()) {
                size_t n = xStreamBufferSend(s_stream, buffer + sent, count - sent, pdMS_TO_TICKS(50));
                sent += n;
                delivered += n;
            }
        }
        if (http) esp_http_client_cleanup(http);
        free(buffer);
        // One low-frequency snapshot per completed/cancelled HTTP request.
        // TLS/redirect/retry high-water mark belongs to this 8 KiB task, unlike
        // playback's existing resource logs. Keep scratch on heap until measured.
        resource_log("http_idle", 0);
        taskENTER_CRITICAL(&s_lock);
        if (s_net.generation == request.generation && s_generation == request.generation) {
            s_net.error = result;
            s_net.done = true;
        }
        taskEXIT_CRITICAL(&s_lock);
        xEventGroupSetBits(s_net_events, NET_IDLE);
    }
}

static esp_audio_simple_dec_type_t detect_type(const uint8_t *bytes, size_t length,
                                              const char *mime, const char *url) {
    if (length >= 12 && !memcmp(bytes, "RIFF", 4) && !memcmp(bytes + 8, "WAVE", 4))
        return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    if (length >= 8 && !memcmp(bytes + 4, "ftyp", 4)) return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    if (length >= 3 && !memcmp(bytes, "ID3", 3)) return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    if (length >= 2 && bytes[0] == 0xff && (bytes[1] & 0xf6) == 0xf0)
        return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    if (strstr(mime, "mp4") || strstr(mime, "m4a") || strstr(url, ".m4a") || strstr(url, ".mp4"))
        return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    if (strstr(mime, "aac") || strstr(url, ".aac")) return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    if (strstr(mime, "wav") || strstr(url, ".wav")) return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
}

static esp_err_t network_cancel(void) {
    generation_next();
    EventBits_t bits = xEventGroupWaitBits(s_net_events, NET_IDLE, pdFALSE, pdTRUE,
                                         pdMS_TO_TICKS(NET_STOP_WAIT_MS));
    return (bits & NET_IDLE) ? ESP_OK : ESP_ERR_TIMEOUT;
}
static esp_err_t network_start_at(const char *url, uint64_t offset, const char *validator, uint64_t total) {
    if (xStreamBufferReset(s_stream) != pdPASS) return ESP_ERR_INVALID_STATE;
    uint32_t generation = generation_next();
    taskENTER_CRITICAL(&s_lock);
    s_net = (net_status_t){ .generation = generation };
    taskEXIT_CRITICAL(&s_lock);
    xEventGroupClearBits(s_net_events, NET_IDLE);
    net_request_t request = { .generation = generation, .url = url, .offset=offset, .total_bytes=total };
    if (offset) snprintf(request.validator,sizeof(request.validator),"%s",validator);
    if (xQueueSend(s_net_requests, &request, 0) != pdPASS) {
        xEventGroupSetBits(s_net_events, NET_IDLE);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
static esp_err_t network_start(const char *url) { return network_start_at(url,0,"",0); }

static void playback_worker(void *arg) {
    (void)arg;
    char *url = NULL;
    uint8_t *input = NULL, *pcm = NULL;
    esp_audio_simple_dec_handle_t decoder = NULL;
    size_t input_length = 0, input_offset = 0;
    size_t pcm_bytes = 0;
    uint64_t decoded_samples = 0, discard_samples = 0, scanned = 0;
    uint32_t target_seconds = 0, format_hz = 0;
    passport_mp3_seek_t mp3_seek={0};
    passport_mp3_seek_plan_t seek_plan={0};
    uint64_t position_base_samples=0;
    uint32_t range_scan_bytes=0, warm_frames=0, warm_bytes=0;
    bool range_mode=false, range_synced=false;
    int64_t discard_yield_at = 0;
    int64_t seek_started_us=0;
    bool seek_output_pending=false;
    bool running = false, paused = false, codec_awake = false, have_output = false, eos_flushed = false;
    // Keep config alive: the parser can defer frame-decoder creation until PCM.
    // AAC-LC only; explicitly disable SBR/HE-AAC in both supported containers.
    esp_aac_dec_cfg_t aac_cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
    esp_m4a_dec_cfg_t m4a_cfg = { .aac_plus_enable = false };
    command_t cmd;
    for (;;) {
        if (xQueueReceive(s_commands, &cmd, running && !paused ? 0 : portMAX_DELAY) == pdPASS) {
            if (cmd.type==CMD_SEEK) s_seek_active_id=cmd.seek_id;
            else if (cmd.type==CMD_PLAY) {
                s_seek_active_id=cmd.seek_id;
                taskENTER_CRITICAL(&s_lock);
                if (cmd.seek_id==s_status.seek_request_id) s_status.seeking=false;
                taskEXIT_CRITICAL(&s_lock);
            }
            if (cmd.type==CMD_SEEK && !url) {
                taskENTER_CRITICAL(&s_lock);
                if (cmd.seek_id==s_status.seek_request_id) { s_status.seeking=false; s_status.seek_complete_id=cmd.seek_id; }
                taskEXIT_CRITICAL(&s_lock);
            }
            if (cmd.type==CMD_SEEK && url) { seek_started_us=esp_timer_get_time(); seek_output_pending=true; }
            else if (cmd.type==CMD_STOP || cmd.type==CMD_PLAY) seek_output_pending=false;
            if (cmd.type == CMD_VOLUME) {
                bsp_audio_set_volume(cmd.value);
                taskENTER_CRITICAL(&s_lock);
                s_status.volume = cmd.value;
                taskEXIT_CRITICAL(&s_lock);
            } else if (cmd.type == CMD_PAUSE && running) {
                paused = cmd.value != 0;
                if (paused) {
                    esp_err_t e = bsp_audio_sleep();
                    codec_awake = false;
                    if (e != ESP_OK) { failure_log("pause_sleep", e); publish(PASSPORT_AUDIO_ERROR, e); running = false; goto failed; }
                    else publish(PASSPORT_AUDIO_PAUSED, ESP_OK);
                } else {
                    publish(PASSPORT_AUDIO_BUFFERING, ESP_OK);
                }
            } else if (cmd.type == CMD_SEEK && url && running && decoder && have_output && format_hz &&
                       (uint64_t)cmd.value * format_hz >= position_base_samples+decoded_samples &&
                       (!mp3_seek.valid || !network_status().validator[0] ||
                        (uint64_t)cmd.value*format_hz<=position_base_samples+decoded_samples+3U*format_hz) &&
                       network_status().error == ESP_OK) {
                // Keep the existing parser, MP3 reservoir, FIFO and HTTP byte
                // cursor. Sample accounting gives an exact forward target for
                // every supported format, including VBR and faststart M4A.
                esp_err_t e = bsp_audio_sleep();
                codec_awake = false;
                if (e != ESP_OK) { failure_log("seek_sleep", e); publish(PASSPORT_AUDIO_ERROR, e); running = false; goto failed; }
                target_seconds = cmd.value;
                discard_samples = (uint64_t)target_seconds * format_hz - position_base_samples - decoded_samples;
                discard_yield_at = esp_timer_get_time() + 20000;
                taskENTER_CRITICAL(&s_lock);
                s_status.position_seconds = target_seconds;
                taskEXIT_CRITICAL(&s_lock);
                publish(paused ? PASSPORT_AUDIO_PAUSED : PASSPORT_AUDIO_BUFFERING, ESP_OK);
                resource_log("seek_forward", 0);
            } else if (cmd.type == CMD_STOP || cmd.type == CMD_PLAY || (cmd.type == CMD_SEEK && url)) {
                // A seek changes position, not the user's playback intent.
                // Keep a paused stream silent until an explicit resume command.
                bool seek_paused = cmd.type == CMD_SEEK && paused;
                net_status_t previous_net=network_status();
                bool fast_seek=cmd.type==CMD_SEEK && mp3_seek.valid && previous_net.validator[0] &&
                               previous_net.total_bytes && passport_mp3_seek_plan(&mp3_seek,cmd.value,&seek_plan) &&
                               seek_plan.offset>mp3_seek.audio_offset;
                running = false;
                esp_err_t e = network_cancel();
                if (decoder) { esp_audio_simple_dec_close(decoder); decoder = NULL; }
                esp_err_t sleep_error = bsp_audio_sleep();
                codec_awake = false;
                if (e != ESP_OK) {
                    /* Retain URL: the network task may still read it. Reject
                     * replacement instead of freeing live resources. */
                    free(cmd.url);
                    failure_log("network_cancel", e);
                    publish(PASSPORT_AUDIO_ERROR, e);
                    continue;
                }
                if (cmd.type == CMD_PLAY) { free(url); url = cmd.url; cmd.url = NULL; mp3_seek.valid=false; }
                target_seconds = cmd.type == CMD_STOP ? 0 : cmd.value;
                if (cmd.type == CMD_STOP) {
                    mp3_seek.valid=false; range_mode=false;
                    free(url); url = NULL;
                    free(input); input = NULL; free(pcm); pcm = NULL;
                    pcm_bytes = 0;
                    publish(sleep_error == ESP_OK ? PASSPORT_AUDIO_IDLE : PASSPORT_AUDIO_ERROR, sleep_error);
                    quiescent_set(sleep_error == ESP_OK);
                    resource_log("stopped", 0);
                } else {
                    if (!input) input = malloc(INPUT_BYTES);
                    // Replacing/restarting a large-output stream must release
                    // its 8 KiB allocation before the next MP3 admission check.
                    free(pcm);
                    pcm = malloc(PCM_INITIAL_BYTES);
                    pcm_bytes = pcm ? PCM_INITIAL_BYTES : 0;
                    range_mode=fast_seek; range_synced=false; range_scan_bytes=warm_frames=warm_bytes=0;
                    e = input && pcm ? (fast_seek ? network_start_at(url,seek_plan.offset,previous_net.validator,
                                                                     previous_net.total_bytes) : network_start(url)) : ESP_ERR_NO_MEM;
                    input_length = input_offset = 0;
                    decoded_samples = discard_samples = scanned = 0;
                    position_base_samples=fast_seek ? (uint64_t)seek_plan.approximate_seconds*mp3_seek.sample_rate-seek_plan.preroll_samples : 0;
                    if (fast_seek) {
                        target_seconds=seek_plan.approximate_seconds;
                        discard_samples=seek_plan.preroll_samples;
                        ESP_LOGI(TAG,"seek_range target=%lu offset=%lu approximate=1",(unsigned long)target_seconds,(unsigned long)seek_plan.offset);
                    }
                    discard_yield_at = esp_timer_get_time() + 20000;
                    format_hz = 0; have_output = false; paused = seek_paused; eos_flushed = false;
                    running = e == ESP_OK;
                    taskENTER_CRITICAL(&s_lock);
                    s_status.position_seconds = target_seconds;
                    s_status.duration_seconds = fast_seek ? (uint64_t)mp3_seek.frames*mp3_seek.frame_samples/mp3_seek.sample_rate : 0;
                    s_status.sample_rate = 0;
                    s_status.seek_supported = true;
                    s_status.position_approximate = fast_seek;
                    taskEXIT_CRITICAL(&s_lock);
                    quiescent_set(false);
                    publish(running ? (paused ? PASSPORT_AUDIO_PAUSED : PASSPORT_AUDIO_BUFFERING) :
                            PASSPORT_AUDIO_ERROR, e);
                    if (!running) {
                        failure_log(input && pcm ? "network_start" : "playback_alloc", e);
                        free(input); input = NULL; free(pcm); pcm = NULL;
                        pcm_bytes = 0;
                        quiescent_set(sleep_error == ESP_OK);
                    }
                }
            }
            free(cmd.url);
            continue;
        }
        if (!running || paused) continue;
        if (!decoder && !input_length && xStreamBufferBytesAvailable(s_stream) < 8192 &&
            !network_status().done) {
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }
        if (input_offset == input_length) {
            input_length = xStreamBufferReceive(s_stream, input, INPUT_BYTES, pdMS_TO_TICKS(50));
            input_offset = 0;
            if (!input_length) {
                net_status_t net = network_status();
                if (!net.done) { publish(PASSPORT_AUDIO_BUFFERING, ESP_OK); continue; }
                if (decoder && !range_mode && !eos_flushed && net.error == ESP_OK) {
                    // Decode any cached final frame through the normal format,
                    // mixing, skip, error and position accounting path below.
                    eos_flushed = true;
                    goto decode_input;
                }
                if (range_mode && net.error!=ESP_OK) goto range_restart_zero;
                if (decoder) { esp_audio_simple_dec_close(decoder); decoder = NULL; }
                esp_err_t sleep_result = bsp_audio_sleep();
                esp_err_t e = sleep_result;
                codec_awake = false; running = false;
                if (net.error != ESP_OK) e = net.error;
                if (!have_output && e == ESP_OK) e = ESP_ERR_NOT_SUPPORTED;
                if (e != ESP_OK) failure_log(net.error != ESP_OK ? "network_end" :
                                           (!have_output ? "empty_audio" : "end_sleep"), e);
                taskENTER_CRITICAL(&s_lock);
                if (s_status.sample_rate) s_status.position_seconds = (position_base_samples+decoded_samples) / s_status.sample_rate;
                taskEXIT_CRITICAL(&s_lock);
                free(input); input = NULL; free(pcm); pcm = NULL;
                pcm_bytes = 0;
                input_length = input_offset = 0;
                publish(e == ESP_OK ? PASSPORT_AUDIO_ENDED : PASSPORT_AUDIO_ERROR, e);
                quiescent_set(sleep_result == ESP_OK);
                resource_log("finished", 0);
                continue;
            }
        }
        if (range_mode) {
            /* Keep incomplete frame tails in the existing 3 KiB input buffer.
             * A Range estimate can begin inside a frame: require three headers
             * at their computed boundaries, then feed one complete frame. */
            size_t remaining=input_length-input_offset;
            if (input_offset) { memmove(input,input+input_offset,remaining); input_length=remaining; input_offset=0; }
            if (!range_synced) {
                input_length+=xStreamBufferReceive(s_stream,input+input_length,INPUT_BYTES-input_length,pdMS_TO_TICKS(50));
                size_t sync;
                if (!passport_mp3_seek_sync(input,input_length,mp3_seek.sample_rate,&sync)) {
                    net_status_t net=network_status();
                    if (net.done && xStreamBufferBytesAvailable(s_stream)==0) goto range_restart_zero;
                    if (input_length==INPUT_BYTES) {
                        memmove(input,input+512,INPUT_BYTES-512); input_length-=512; range_scan_bytes+=512;
                        if (range_scan_bytes>8192) goto range_restart_zero;
                    }
                    continue;
                }
                memmove(input,input+sync,input_length-sync); input_length-=sync;
                range_synced=true;
                ESP_LOGI(TAG,"seek_sync skipped=%u",(unsigned)(range_scan_bytes+sync));
            }
            if (input_length<4) {
                input_length+=xStreamBufferReceive(s_stream,input+input_length,INPUT_BYTES-input_length,pdMS_TO_TICKS(50));
                if (input_length<4) { if (network_status().done) goto range_restart_zero; continue; }
            }
            passport_mp3_frame_t frame;
            if (!passport_mp3_header(input,&frame) || frame.sample_rate!=mp3_seek.sample_rate) {
                /* Optional terminal ID3v1 is not an MPEG frame. */
                if (input_length<=128 && !memcmp(input,"TAG",3) && network_status().done) {
                    input_offset=input_length; continue;
                }
                goto range_restart_zero;
            }
            if (input_length<frame.bytes) {
                input_length+=xStreamBufferReceive(s_stream,input+input_length,INPUT_BYTES-input_length,pdMS_TO_TICKS(50));
                if (input_length<frame.bytes) { if (network_status().done && !xStreamBufferBytesAvailable(s_stream)) goto range_restart_zero; continue; }
            }
        }
        if (!decoder) {
            if (input_length < 12 && !network_status().done) {
                input_length += xStreamBufferReceive(s_stream, input + input_length,
                                  INPUT_BYTES - input_length, pdMS_TO_TICKS(50));
                if (input_length < 12) continue;
            }
            net_status_t net = network_status();
            esp_audio_simple_dec_type_t type = detect_type(input, input_length, net.mime, url);
            if (range_mode) type=ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
            else if (type==ESP_AUDIO_SIMPLE_DEC_TYPE_MP3 && !mp3_seek.valid && !scanned) {
                size_t first=0;
                if (input_length>=10 && !memcmp(input,"ID3",3) &&
                    !((input[6]|input[7]|input[8]|input[9])&0x80)) {
                    first=10U+((uint32_t)input[6]<<21)+((uint32_t)input[7]<<14)+((uint32_t)input[8]<<7)+input[9];
                    if (input[3]==4 && (input[5]&0x10)) first+=10;
                }
                if (first+4<=input_length) {
                    passport_mp3_frame_t frame;
                    if (passport_mp3_header(input+first,&frame) && first+frame.bytes>input_length && first+frame.bytes<=INPUT_BYTES)
                        input_length+=xStreamBufferReceive(s_stream,input+input_length,INPUT_BYTES-input_length,pdMS_TO_TICKS(50));
                    if (passport_mp3_seek_parse(&mp3_seek,input+first,input_length-first,first,net.total_bytes))
                        ESP_LOGI(TAG,"seek_metadata cbr=%d toc=%d duration=%lu",mp3_seek.cbr,mp3_seek.has_toc,
                                 (unsigned long)((uint64_t)mp3_seek.frames*mp3_seek.frame_samples/mp3_seek.sample_rate));
                }
            }
            size_t needed_heap = type == ESP_AUDIO_SIMPLE_DEC_TYPE_MP3 ? 64 * 1024 : 80 * 1024;
            if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < needed_heap) {
                ESP_LOGE(TAG, "admission codec=%d required=%u", (int)type, (unsigned)needed_heap);
                failure_log("decoder_admission", ESP_ERR_NO_MEM);
                publish(PASSPORT_AUDIO_ERROR, ESP_ERR_NO_MEM); running = false;
            } else {
                esp_audio_simple_dec_cfg_t cfg = {
                    .dec_type = type, .use_frame_dec = range_mode,
                };
                if (type == ESP_AUDIO_SIMPLE_DEC_TYPE_AAC) {
                    cfg.dec_cfg = &aac_cfg; cfg.cfg_size = sizeof(aac_cfg);
                } else if (type == ESP_AUDIO_SIMPLE_DEC_TYPE_M4A) {
                    cfg.dec_cfg = &m4a_cfg; cfg.cfg_size = sizeof(m4a_cfg);
                }
                esp_audio_err_t opened = decoder_open(&cfg, &decoder);
                if (opened != ESP_AUDIO_ERR_OK) {
                    failure_log("decoder_open", opened);
                    publish(PASSPORT_AUDIO_ERROR, opened == ESP_AUDIO_ERR_MEM_LACK ?
                            ESP_ERR_NO_MEM : ESP_ERR_NOT_SUPPORTED);
                    running = false;
                }
            }
            if (!running) goto failed;
        }
decode_input:
        ; // A label must precede a statement before C declarations.
        esp_audio_simple_dec_raw_t raw = {
            .buffer = input + input_offset, .len = input_length - input_offset,
            .eos = eos_flushed && input_length == 0,
        };
        passport_mp3_frame_t range_frame={0};
        if (range_mode && !raw.eos) {
            if (!passport_mp3_header(raw.buffer,&range_frame) || raw.len<range_frame.bytes) goto range_restart_zero;
            raw.len=range_frame.bytes;
        }
        esp_audio_simple_dec_out_t out = { .buffer = pcm, .len = pcm_bytes };
        int64_t frame_start = esp_timer_get_time();
        esp_audio_err_t r = decoder_process(decoder, &raw, &out);
        uint32_t decode_us = esp_timer_get_time() - frame_start;
        if (range_mode && !raw.eos && (r==ESP_AUDIO_ERR_FAIL || r==ESP_AUDIO_ERR_DATA_LACK) &&
            warm_frames<12 && warm_bytes<511 && !out.decoded_size && raw.consumed<=raw.len) {
            /* PacketVideo may reject initial reservoir references while still
             * accumulating their main data. Never make those frames audible. */
            input_offset+=range_frame.bytes; scanned+=range_frame.bytes;
            warm_bytes+=range_frame.main_data_bytes; ++warm_frames;
            uint32_t skipped=range_frame.samples;
            decoded_samples+=skipped;
            discard_samples=discard_samples>skipped ? discard_samples-skipped : 0;
            continue;
        }
        if (range_mode && !raw.eos && r!=ESP_AUDIO_ERR_OK && r!=ESP_AUDIO_ERR_BUFF_NOT_ENOUGH)
            goto range_restart_zero;
        bool empty_end = raw.eos && !raw.len && !out.decoded_size &&
                         (r == ESP_AUDIO_ERR_OK || r == ESP_AUDIO_ERR_DATA_LACK);
        if (!raw.consumed && !out.decoded_size && r == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            esp_err_t e = pcm_grow(&pcm, &pcm_bytes, out.needed_size);
            if (e != ESP_OK) {
                failure_log("pcm_capacity", e);
                publish(PASSPORT_AUDIO_ERROR, e); running = false; goto failed;
            }
            // The pinned simple-decoder parser's buffer-shortage path sets
            // consumed=0. Its official example enlarges output and retries
            // without advancing input; only accept that unambiguous case.
            goto decode_input;
        }
        if (raw.consumed > raw.len || out.decoded_size > pcm_bytes ||
            (!empty_end && r != ESP_AUDIO_ERR_OK && r != ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) ||
            r == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH || (!raw.consumed && !out.decoded_size && !raw.eos)) {
            ESP_LOGE(TAG, "process result=%d input=%u consumed=%u output=%u needed=%u eos=%d",
                     (int)r, (unsigned)raw.len, (unsigned)raw.consumed,
                     (unsigned)out.decoded_size, (unsigned)out.needed_size, (int)raw.eos);
            failure_log("decoder_process", r);
            publish(PASSPORT_AUDIO_ERROR, ESP_ERR_NOT_SUPPORTED); running = false; goto failed;
        }
        input_offset += raw.consumed;
        scanned += raw.consumed;
        if (!out.decoded_size) {
            if (!have_output && scanned > HEADER_SCAN_LIMIT) {
                failure_log("header_scan_limit", ESP_ERR_NOT_SUPPORTED);
                publish(PASSPORT_AUDIO_ERROR, ESP_ERR_NOT_SUPPORTED); running = false; goto failed;
            }
            continue;
        }
        esp_audio_simple_dec_info_t info = {0};
        esp_audio_err_t info_result = esp_audio_simple_dec_get_info(decoder, &info);
        if (info_result != ESP_AUDIO_ERR_OK ||
            info.bits_per_sample != 16 || info.channel < 1 || info.channel > 2 ||
            info.sample_rate < 8000 || info.sample_rate > 48000 ||
            out.decoded_size % (2 * info.channel)) {
            ESP_LOGE(TAG, "format result=%d bits=%u channels=%u rate=%u output=%u",
                     (int)info_result, (unsigned)info.bits_per_sample, (unsigned)info.channel,
                     (unsigned)info.sample_rate, (unsigned)out.decoded_size);
            failure_log("decoder_format", info_result);
            publish(PASSPORT_AUDIO_ERROR, ESP_ERR_NOT_SUPPORTED); running = false; goto failed;
        }
        if (!have_output) {
            if (!range_mode) discard_samples = (uint64_t)target_seconds * info.sample_rate;
            resource_log("first_pcm", decode_us);
            if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 20 * 1024)
                ESP_LOGW(TAG, "Internal heap headroom below 20 KiB; avoid overlapping TLS/catalog work");
        }
        have_output = true;
        if (format_hz && format_hz != info.sample_rate) {
            failure_log("sample_rate_changed", ESP_ERR_NOT_SUPPORTED);
            publish(PASSPORT_AUDIO_ERROR, ESP_ERR_NOT_SUPPORTED); running = false; goto failed;
        }
        format_hz = info.sample_rate; // Confirm the continuous decoded timebase even while discarding.
        size_t samples = out.decoded_size / (2 * info.channel);
        int16_t *p = (int16_t *)pcm;
        if (info.channel == 2) {
            /* One physical speaker: preserve both channels with clipping-safe
             * arithmetic averaging, halve DMA traffic, no resampling/pitch shift. */
            for (size_t i = 0; i < samples; ++i) p[i] = ((int32_t)p[2*i] + p[2*i+1]) / 2;
        }
        size_t skip = discard_samples < samples ? discard_samples : samples;
        if (range_mode && (warm_frames<3 || warm_bytes<511) && skip<samples) skip=samples;
        if (skip>discard_samples) discard_samples=0;
        else discard_samples -= skip;
        if (range_mode) { ++warm_frames; warm_bytes+=range_frame.main_data_bytes; }
        decoded_samples += samples;
        if (samples > skip) {
            if (!codec_awake) {
                esp_err_t e = bsp_audio_set_playback_format(info.sample_rate, 16, 1);
                if (e != ESP_OK) { failure_log("dac_format", e); publish(PASSPORT_AUDIO_ERROR, e); running = false; goto failed; }
                codec_awake = true;
            }
            /* Feed bounded chunks; commands run between decoded frames.
             * No register/codec operation races PCM writes. */
            for (size_t i = skip; i < samples; ) {
                size_t n = samples - i;
                if (n > 480) n = 480;
                esp_err_t e = bsp_audio_write(p + i, n * 2);
                if (e != ESP_OK) { failure_log("dac_write", e); publish(PASSPORT_AUDIO_ERROR, e); running = false; break; }
                i += n;
            }
            if (!running) goto failed;
            if (seek_output_pending) {
                ESP_LOGI(TAG,"seek_output target=%lu approximate=%d elapsed_us=%lld",(unsigned long)target_seconds,
                         range_mode,(long long)(esp_timer_get_time()-seek_started_us));
                seek_output_pending=false;
            }
            publish(PASSPORT_AUDIO_PLAYING, ESP_OK);
        } else {
            /* Each frame still checks commands and releases the PM lock. Bound
             * compute batches to 20 ms before one blocking scheduler tick so
             * lower-priority HTTP/UI and idle tasks run during long discards.
             * Avoid the old mandatory 10 ms delay after every ~26 ms MP3 frame. */
            if (esp_timer_get_time() >= discard_yield_at) {
                vTaskDelay(1);
                discard_yield_at = esp_timer_get_time() + 20000;
            }
        }
        net_status_t net = network_status();
        taskENTER_CRITICAL(&s_lock);
        if (!discard_samples) s_status.position_seconds = (position_base_samples+decoded_samples) / info.sample_rate;
        s_status.sample_rate = info.sample_rate;
        if (mp3_seek.valid) {
            s_status.duration_seconds=(uint64_t)mp3_seek.frames*mp3_seek.frame_samples/mp3_seek.sample_rate;
        } else if (info.bitrate && net.total_bytes) {
            uint64_t duration = net.total_bytes * 8 / info.bitrate;
            s_status.duration_seconds = duration > UINT32_MAX ? UINT32_MAX : duration;
        }
        taskEXIT_CRITICAL(&s_lock);
        continue;
range_restart_zero:
        {
            esp_err_t e=network_cancel();
            if (e!=ESP_OK) { publish(PASSPORT_AUDIO_ERROR,e); running=false; goto failed; }
            if (decoder) { esp_audio_simple_dec_close(decoder); decoder=NULL; }
            (void)bsp_audio_sleep(); codec_awake=false;
            range_mode=range_synced=false; mp3_seek.valid=false;
            decoded_samples=discard_samples=scanned=position_base_samples=0;
            input_length=input_offset=0; format_hz=0; have_output=eos_flushed=false;
            taskENTER_CRITICAL(&s_lock); s_status.position_approximate=false; taskEXIT_CRITICAL(&s_lock);
            e=network_start(url);
            ESP_LOGW(TAG,"seek_range_fallback target=%lu",(unsigned long)target_seconds);
            if (e!=ESP_OK) { publish(PASSPORT_AUDIO_ERROR,e); running=false; goto failed; }
            publish(paused ? PASSPORT_AUDIO_PAUSED : PASSPORT_AUDIO_BUFFERING,ESP_OK);
            continue;
        }
failed:
        {
        esp_err_t stopped = network_cancel();
        if (decoder) { esp_audio_simple_dec_close(decoder); decoder = NULL; }
        esp_err_t asleep = bsp_audio_sleep();
        codec_awake = false;
        free(input); input = NULL; free(pcm); pcm = NULL;
        pcm_bytes = 0;
        input_length = input_offset = 0;
        quiescent_set(stopped == ESP_OK && asleep == ESP_OK);
        resource_log("failed", 0);
        }
    }
}

static esp_err_t enqueue(command_t command) {
    if (!s_commands) { free(command.url); return ESP_ERR_INVALID_STATE; }
    // Mark producers before enqueue can wake the higher-priority worker. A
    // rejected play conservatively requires STOP to acknowledge quiescence.
    if (command.type == CMD_PLAY || command.type == CMD_SEEK) quiescent_set(false);
    bool was_seeking=false;
    if (command.type==CMD_PLAY) {
        taskENTER_CRITICAL(&s_lock); command.seek_id=s_status.seek_request_id; taskEXIT_CRITICAL(&s_lock);
    }
    if (command.type==CMD_SEEK) {
        taskENTER_CRITICAL(&s_lock);
        was_seeking=s_status.seeking; s_status.seeking=true;
        command.seek_id=++s_status.seek_request_id;
        taskEXIT_CRITICAL(&s_lock);
    }
    if (xQueueSend(s_commands, &command, 0) != pdPASS) {
        if (command.type==CMD_SEEK) {
            taskENTER_CRITICAL(&s_lock);
            if (s_status.seek_request_id==command.seek_id) { --s_status.seek_request_id; s_status.seeking=was_seeking; }
            taskEXIT_CRITICAL(&s_lock);
        }
        free(command.url); return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
esp_err_t passport_audio_play(const char *url, uint32_t resume_seconds) {
    if (!url || strnlen(url, URL_LIMIT) >= URL_LIMIT ||
        (strncasecmp(url, "https://", 8) && strncasecmp(url, "http://", 7))) return ESP_ERR_INVALID_ARG;
    char *copy = strdup(url);
    if (!copy) return ESP_ERR_NO_MEM;
    return enqueue((command_t){ .type = CMD_PLAY, .value = resume_seconds, .url = copy });
}
esp_err_t passport_audio_pause(bool paused) { return enqueue((command_t){ .type = CMD_PAUSE, .value = paused }); }
esp_err_t passport_audio_stop(void) { return enqueue((command_t){ .type = CMD_STOP }); }
esp_err_t passport_audio_seek(uint32_t seconds) { return enqueue((command_t){ .type = CMD_SEEK, .value = seconds }); }
esp_err_t passport_audio_set_volume(uint8_t percent) {
    return enqueue((command_t){ .type = CMD_VOLUME, .value = percent > 100 ? 100 : percent });
}

esp_err_t passport_audio_init(void) {
    if (s_commands) return ESP_OK;
    esp_err_t e = bsp_audio_init_playback();
    if (e != ESP_OK) return e;
    bsp_audio_set_volume(45);
    e = bsp_audio_sleep();
    if (e != ESP_OK) return e;
    if (esp_mp3_dec_register() != ESP_AUDIO_ERR_OK || esp_aac_dec_register() != ESP_AUDIO_ERR_OK ||
        esp_pcm_dec_register() != ESP_AUDIO_ERR_OK || esp_m4a_dec_register() != ESP_AUDIO_ERR_OK ||
        esp_wav_dec_register() != ESP_AUDIO_ERR_OK) {
        decoder_unregister();
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_PM_ENABLE
    e = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "audio_compute", &s_compute_lock);
    if (e != ESP_OK) {
        decoder_unregister();
        return e;
    }
#endif
    s_net_requests = xQueueCreate(1, sizeof(net_request_t));
    s_net_events = xEventGroupCreate();
    s_stream = xStreamBufferCreate(STREAM_BYTES, 1);
    QueueHandle_t commands = xQueueCreate(8, sizeof(command_t));
    if (!s_net_requests || !s_net_events || !s_stream || !commands) {
        if (s_net_requests) vQueueDelete(s_net_requests);
        if (s_net_events) vEventGroupDelete(s_net_events);
        if (s_stream) vStreamBufferDelete(s_stream);
        if (commands) vQueueDelete(commands);
        s_net_requests = NULL; s_net_events = NULL; s_stream = NULL;
        decoder_unregister();
        if (s_compute_lock) { esp_pm_lock_delete(s_compute_lock); s_compute_lock = NULL; }
        return ESP_ERR_NO_MEM;
    }
    xEventGroupSetBits(s_net_events, NET_IDLE);
    TaskHandle_t net_task = NULL;
    if (xTaskCreate(network_worker, "audio_http", 8192, NULL, 4, &net_task) != pdPASS) {
        e = ESP_ERR_NO_MEM; goto init_fail;
    }
    s_commands = commands;
    /* C3 archive audit: MP3 largest frame 1200 B, AAC-LC core <=160 B;
     * 12 KiB includes nested calls, BSP and logs. Other codecs are disabled.
     * Device stack watermarks still required; see gcores-audio documentation. */
    if (xTaskCreate(playback_worker, "audio_decode", 12288, NULL, 5, NULL) != pdPASS) {
        vTaskDelete(net_task); s_commands = NULL; e = ESP_ERR_NO_MEM; goto init_fail;
    }
    ESP_LOGI(TAG, "C3 stream ready: FIFO 12 KiB, PCM 4608 B (max 8 KiB), HTTP RX/scratch 1 KiB, decode stack 12 KiB");
    return ESP_OK;
init_fail:
    vQueueDelete(commands); vQueueDelete(s_net_requests); vEventGroupDelete(s_net_events);
    vStreamBufferDelete(s_stream);
    s_net_requests = NULL; s_net_events = NULL; s_stream = NULL;
    decoder_unregister();
    if (s_compute_lock) { esp_pm_lock_delete(s_compute_lock); s_compute_lock = NULL; }
    return e;
}
