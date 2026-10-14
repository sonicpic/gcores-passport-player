#include "passport_wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CONNECTED BIT0
#define PORTAL BIT1
#define SUBMITTED BIT2
#define RECONFIGURING BIT3
#define DISCONNECTED BIT4
#define CLOSE_REQUESTED BIT5
#define CLOSING BIT6
#define CONFIGURED BIT7
#define STOPPING BIT8
static EventGroupHandle_t events;
/* Only application worker APIs take this lock. HTTP/event callbacks must never
 * take it: httpd_stop() waits for any running HTTP handler to return. */
static SemaphoreHandle_t lifecycle;
static httpd_handle_t server;
static char ap_password[13];
static unsigned retries;
static esp_err_t end_provisioning_locked(void);
static const char page[] = "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>GCORES Passport</title><style>body{background:#101216;color:#eee;font:16px system-ui;max-width:420px;margin:64px auto;padding:24px}h1{border-left:5px solid #ef3b45;padding-left:16px}input,button{box-sizing:border-box;width:100%;padding:14px;margin:8px 0;background:#20242a;color:white;border:1px solid #444;border-radius:6px}button{background:#e73541}</style><h1>GCORES / 机核</h1><p>连接 2.4 GHz Wi-Fi</p><form method=post action=/connect><input name=ssid maxlength=32 placeholder='网络名称 SSID' required><input name=password type=password maxlength=63 placeholder='密码（开放网络留空）'><button>保存并连接</button></form>";
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        EventBits_t bits=xEventGroupGetBits(events);
        if (!(bits&(RECONFIGURING|STOPPING))) {
            retries = 0;
            xEventGroupSetBits(events, CONNECTED|((bits&PORTAL)?CLOSE_REQUESTED:0));
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, CONNECTED);
        xEventGroupSetBits(events,DISCONNECTED);
        EventBits_t bits=xEventGroupGetBits(events);
        if ((bits&CONFIGURED) && !(bits&(RECONFIGURING|STOPPING)) && ++retries <= 5) esp_wifi_connect();
    }
}
static esp_err_t get_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}
static int unhex(char c) {
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}
static bool decode(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    while (*src) {
        unsigned char c = (unsigned char)*src++;
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (!src[0] || !src[1] || unhex(src[0]) < 0 || unhex(src[1]) < 0) return false;
            c = (unsigned char)((unhex(src[0])<<4)|unhex(src[1])); src += 2;
        }
        if (!c || n+1 >= cap) return false;
        dst[n++] = (char)c;
    }
    dst[n] = 0; return true;
}
static esp_err_t connect_post(httpd_req_t *req) {
    if (xEventGroupGetBits(events)&CLOSING) {
        httpd_resp_set_status(req,"503 Service Unavailable");
        return httpd_resp_sendstr(req,"Provisioning is closing");
    }
    char body[320];
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form size");
    size_t used = 0;
    int64_t deadline=esp_timer_get_time()+5000000;
    while (used < req->content_len) {
        /* Socket timeouts apply per recv, so a slow byte stream otherwise keeps
         * the worker blocked in httpd_stop indefinitely. Abort on cancellation
         * or a total form deadline; ESP_FAIL closes without draining the body. */
        if ((xEventGroupGetBits(events)&CLOSING) || esp_timer_get_time()>=deadline) return ESP_FAIL;
        int n = httpd_req_recv(req, body+used, req->content_len-used);
        if (n <= 0) return ESP_FAIL;
        used += n;
    }
    if ((xEventGroupGetBits(events)&CLOSING) || esp_timer_get_time()>=deadline) return ESP_FAIL;
    body[used] = 0;
    char ssid[33] = {0}, password[64] = {0};
    for (char *token = strtok(body,"&"); token; token = strtok(NULL,"&")) {
        char *equal = strchr(token,'='); if (!equal) continue; *equal++ = 0;
        if (!strcmp(token,"ssid") && !decode(ssid,sizeof(ssid),equal)) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid SSID");
        if (!strcmp(token,"password") && !decode(password,sizeof(password),equal)) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid password");
    }
    size_t sl = strlen(ssid), pl = strlen(password);
    if (!sl || sl > 32 || (pl && pl < 8)) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Check SSID and WPA password length");
    wifi_config_t config = {0};
    memcpy(config.sta.ssid,ssid,sl); memcpy(config.sta.password,password,pl);
    config.sta.pmf_cfg.capable = true;
    xEventGroupSetBits(events,RECONFIGURING);
    xEventGroupClearBits(events,SUBMITTED|DISCONNECTED|CONNECTED|CLOSE_REQUESTED);
    retries = 0;
    /* disconnect() may return ESP_OK without emitting DISCONNECTED when STA
     * has never associated (first setup), or is already offline. GOT_IP alone
     * also misses an associated station still waiting for DHCP: ask the driver.
     * Keep RECONFIGURING set throughout cancellation and configuration so old
     * queued events cannot reconnect the old network or close this portal. */
    wifi_ap_record_t old_ap;
    const char *stage="association_check";
    esp_err_t associated=esp_wifi_sta_get_ap_info(&old_ap);
    esp_err_t err=associated;
    if (associated==ESP_OK || associated==ESP_ERR_WIFI_NOT_CONNECT) {
        stage="disconnect"; err=esp_wifi_disconnect();
    }
    if (err==ESP_OK && associated==ESP_OK &&
        !(xEventGroupWaitBits(events,DISCONNECTED,pdFALSE,pdTRUE,pdMS_TO_TICKS(2000))&DISCONNECTED) &&
        esp_wifi_sta_get_ap_info(&old_ap)!=ESP_ERR_WIFI_NOT_CONNECT) {
        stage="disconnect_wait"; err=ESP_ERR_TIMEOUT;
    }
    if (err==ESP_ERR_WIFI_NOT_CONNECT) err=ESP_OK;
    if (err==ESP_OK) {
        stage="set_config";
        /* Cancellation during a connection attempt can complete without a
         * disconnect event. Retry only the driver's explicit transient state,
         * with a bounded wait; errors such as NVS/password failures stay errors. */
        for (unsigned attempt=0; attempt<=20; ++attempt) {
            err=esp_wifi_set_config(WIFI_IF_STA,&config);
            if (err!=ESP_ERR_WIFI_STATE || attempt==20) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    memset(password,0,sizeof(password)); memset(body,0,sizeof(body));
    if (err==ESP_OK) xEventGroupSetBits(events,CONFIGURED);
    xEventGroupClearBits(events,RECONFIGURING);
    if (err == ESP_OK) { stage="connect"; xEventGroupSetBits(events,SUBMITTED); err = esp_wifi_connect(); }
    memset(&config,0,sizeof(config));
    if (err != ESP_OK) {
        ESP_LOGW("passport_wifi","Provisioning stage=%s error=%s",stage,esp_err_to_name(err));
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Connection could not start");
    }
    return httpd_resp_sendstr(req,"Saved. Check the Passport screen; reconnect here if the network rejects the credentials.");
}
esp_err_t passport_wifi_start_provisioning(void) {
    if (!events || !lifecycle) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    if (passport_wifi_provisioning()) { xSemaphoreGive(lifecycle); return ESP_OK; }
    xEventGroupClearBits(events,SUBMITTED|CLOSE_REQUESTED|CLOSING);
    snprintf(ap_password,sizeof(ap_password),"%012lu",(unsigned long)(esp_random()%1000000000UL));
    wifi_config_t ap = {.ap = {.ssid="Gcores-Passport", .ssid_len=15, .channel=1, .max_connection=1, .authmode=WIFI_AUTH_WPA2_PSK}};
    memcpy(ap.ap.password,ap_password,strlen(ap_password));
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) goto done;
    xEventGroupSetBits(events,PORTAL);
    err = esp_wifi_set_config(WIFI_IF_AP,&ap);
    if (err != ESP_OK) goto rollback;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG(); cfg.stack_size=4096; cfg.max_open_sockets=2; cfg.lru_purge_enable=true;
    cfg.recv_wait_timeout=1; cfg.send_wait_timeout=1;
    err = httpd_start(&server,&cfg);
    if (err != ESP_OK) goto rollback;
    const httpd_uri_t get = {.uri="/",.method=HTTP_GET,.handler=get_page};
    const httpd_uri_t post = {.uri="/connect",.method=HTTP_POST,.handler=connect_post};
    err = httpd_register_uri_handler(server,&get);
    if (err == ESP_OK) err = httpd_register_uri_handler(server,&post);
    if (err == ESP_OK) { ESP_LOGI("passport_wifi","Provisioning hotspot started"); goto done; }
rollback:
    /* Preserve the original startup error. If teardown itself fails, retain
     * PORTAL/server so a subsequent exit can retry instead of leaking them. */
    end_provisioning_locked();
done:
    if (err!=ESP_OK && !passport_wifi_provisioning()) memset(ap_password,0,sizeof(ap_password));
    xSemaphoreGive(lifecycle);
    return err;
}
esp_err_t passport_wifi_init(void) {
    lifecycle=xSemaphoreCreateMutex(); if (!lifecycle) return ESP_ERR_NO_MEM;
    events = xEventGroupCreate(); if (!events) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta() || !esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg); if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    wifi_config_t saved = {0}; err = esp_wifi_get_config(WIFI_IF_STA,&saved);
    bool configured = err == ESP_OK && saved.sta.ssid[0]; memset(&saved,0,sizeof(saved));
    if (configured) xEventGroupSetBits(events,CONFIGURED);
    return configured ? esp_wifi_connect() : passport_wifi_start_provisioning();
}
bool passport_wifi_connected(void) { return events && (xEventGroupGetBits(events)&CONNECTED); }
bool passport_wifi_provisioning(void) { return events && (xEventGroupGetBits(events)&PORTAL); }
const char *passport_wifi_ap_password(void) { return ap_password; }
static esp_err_t end_provisioning_locked(void) {
    if (!(xEventGroupGetBits(events)&PORTAL) && !server) return ESP_OK;
    xEventGroupSetBits(events,CLOSING);
    /* The HTTP task may be changing STA credentials. Drain it before changing
     * AP mode; do not disconnect STA or reset its outstanding retry budget. */
    esp_err_t err=ESP_OK;
    if (server) { err=httpd_stop(server); if (err==ESP_OK) server=NULL; }
    if (err==ESP_OK) err=esp_wifi_set_mode(WIFI_MODE_STA);
    if (err==ESP_OK) {
        xEventGroupClearBits(events,PORTAL|SUBMITTED|CLOSE_REQUESTED);
        memset(ap_password,0,sizeof(ap_password));
        ESP_LOGI("passport_wifi","Provisioning hotspot closed; station preserved");
    }
    xEventGroupClearBits(events,CLOSING);
    return err;
}
void passport_wifi_poll(void) {
    if (!events || !lifecycle) return;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    if ((xEventGroupGetBits(events)&(PORTAL|CLOSE_REQUESTED))==(PORTAL|CLOSE_REQUESTED))
        end_provisioning_locked();
    xSemaphoreGive(lifecycle);
}
esp_err_t passport_wifi_end_provisioning(void) {
    if (!events || !lifecycle) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    esp_err_t err=end_provisioning_locked();
    xSemaphoreGive(lifecycle);
    return err;
}
esp_err_t passport_wifi_stop(void) {
    if (!events || !lifecycle) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    xEventGroupSetBits(events,STOPPING|CLOSING);
    esp_err_t err=ESP_OK;
    if (server) { err=httpd_stop(server); if (err==ESP_OK) server=NULL; }
    if (err==ESP_OK) err=esp_wifi_stop();
    if (err==ESP_OK) {
        xEventGroupClearBits(events,CONNECTED|PORTAL|SUBMITTED|CLOSE_REQUESTED);
        memset(ap_password,0,sizeof(ap_password));
    }
    xEventGroupClearBits(events,CLOSING);
    if (err!=ESP_OK) xEventGroupClearBits(events,STOPPING);
    xSemaphoreGive(lifecycle);
    return err;
}
