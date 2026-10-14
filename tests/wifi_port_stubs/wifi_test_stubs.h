#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
const char *esp_err_to_name(esp_err_t);
void test_log_warning(const char *, const char *, ...);
#define ESP_LOGW(...) test_log_warning(__VA_ARGS__)
#define ESP_LOGI(...) ((void)0)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_WIFI_NOT_CONNECT 0x300f
#define ESP_ERR_WIFI_STATE 0x3006
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
#define BIT0 (1u << 0)
#define BIT1 (1u << 1)
#define BIT2 (1u << 2)
#define BIT3 (1u << 3)
#define BIT4 (1u << 4)
#define BIT5 (1u << 5)
#define BIT6 (1u << 6)
#define BIT7 (1u << 7)
#define BIT8 (1u << 8)
#define BIT9 (1u << 9)
#define BIT10 (1u << 10)
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY ((unsigned)-1)
typedef struct { bool held; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {false}
#define taskENTER_CRITICAL(p) do {assert(!(p)->held);(p)->held=true;} while(0)
#define taskEXIT_CRITICAL(p) do {assert((p)->held);(p)->held=false;} while(0)
typedef struct { bool held; } test_semaphore_t;
typedef test_semaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
typedef unsigned EventBits_t;
typedef struct { EventBits_t bits; } test_event_group_t;
typedef test_event_group_t *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupGetBits(EventGroupHandle_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t, int, int, unsigned);
typedef const char *esp_event_base_t;
extern const char test_wifi_base[], test_ip_base[];
#define WIFI_EVENT test_wifi_base
#define IP_EVENT test_ip_base
#define IP_EVENT_STA_GOT_IP 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_STA_CONNECTED 3
#define ESP_EVENT_ANY_ID -1
typedef void (*event_handler_t)(void *, esp_event_base_t, int32_t, void *);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t, int32_t, event_handler_t, void *);
typedef struct { unsigned placeholder; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
enum { WIFI_MODE_STA = 1, WIFI_MODE_APSTA, WIFI_IF_STA, WIFI_IF_AP, WIFI_AUTH_WPA2_PSK, WIFI_STORAGE_FLASH, WIFI_PS_MIN_MODEM, WIFI_STORAGE_RAM };
typedef struct { uint8_t ssid[32], password[64]; struct { bool capable; } pmf_cfg; } test_wifi_sta_t;
typedef struct { uint8_t ssid[32], password[64], ssid_len, channel, max_connection; int authmode; } test_wifi_ap_t;
typedef union { test_wifi_sta_t sta; test_wifi_ap_t ap; } wifi_config_t;
typedef struct { uint8_t ssid[33]; } wifi_ap_record_t;
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
void vTaskDelay(unsigned);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t *);
esp_err_t esp_wifi_get_config(int, wifi_config_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_ps(int);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_netif_init(void);
void *esp_netif_create_default_wifi_sta(void);
void *esp_netif_create_default_wifi_ap(void);
uint32_t esp_random(void);
int64_t esp_timer_get_time(void);
typedef void *httpd_handle_t;
typedef struct {
    size_t content_len, offset, recv_chunk;
    const char *body;
    int response;
} httpd_req_t;
typedef struct { unsigned stack_size, max_open_sockets; bool lru_purge_enable; unsigned recv_wait_timeout, send_wait_timeout; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){4096, 7, false, 5, 5})
#define HTTPD_RESP_USE_STRLEN -1
enum { HTTP_GET, HTTP_POST, HTTPD_400_BAD_REQUEST = 400, HTTPD_500_INTERNAL_SERVER_ERROR = 500 };
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, int);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
esp_err_t httpd_resp_send_err(httpd_req_t *, int, const char *);
int httpd_req_recv(httpd_req_t *, char *, size_t);

typedef unsigned nvs_handle_t;
enum { NVS_READONLY, NVS_READWRITE };
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
