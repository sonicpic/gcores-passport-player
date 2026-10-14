#include "wifi_port_stubs/wifi_test_stubs.h"
#include <stdarg.h>
#include "../main/passport_wifi.c"

const char test_wifi_base[] = "wifi", test_ip_base[] = "ip";
static test_event_group_t event_group;
static test_semaphore_t semaphore;
static event_handler_t registered_wifi, registered_ip;
static wifi_config_t stored_sta;
static httpd_uri_t get_uri, post_uri;
static unsigned connects, disconnects, sta_sets, stops, registered_uris;
static esp_err_t disconnect_error, config_error, connect_error;
static bool disconnect_pending, deliver_disconnect;
static bool driver_associated, queued_old_connected;
static unsigned config_busy_attempts, delay_calls, wait_calls;
static char warning[128];
static int current_mode;
static unsigned ap_starts, wifi_stops, order_count;
static char order[32];
static esp_err_t server_stop_error, mode_error;
static esp_err_t server_start_error, ap_config_error;
static unsigned fail_uri_number;
static int64_t clock_us, receive_delay_us;
static bool close_during_receive;
int64_t esp_timer_get_time(void) { return clock_us; }
static bool in_http_handler, got_ip_in_response, callback_during_stop;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { semaphore.held=false; return &semaphore; }
int xSemaphoreTake(SemaphoreHandle_t h,unsigned ticks) {
    assert(h==&semaphore && !h->held && !in_http_handler && ticks==portMAX_DELAY);
    h->held=true; return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t h) { assert(h->held); h->held=false; return pdTRUE; }
static void record_order(char c) { assert(order_count+1<sizeof(order)); order[order_count++]=c; order[order_count]=0; }
const char *esp_err_to_name(esp_err_t err) {
    return err==ESP_ERR_TIMEOUT?"ESP_ERR_TIMEOUT":err==ESP_ERR_WIFI_STATE?"ESP_ERR_WIFI_STATE":"ESP_FAIL";
}
void test_log_warning(const char *tag,const char *format,...) {
    assert(strcmp(tag,"passport_wifi")==0);
    va_list args; va_start(args,format); vsnprintf(warning,sizeof(warning),format,args); va_end(args);
}

EventGroupHandle_t xEventGroupCreate(void) { event_group.bits=0; return &event_group; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t h, EventBits_t bits) { return h->bits |= bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t h, EventBits_t bits) { unsigned before=h->bits; h->bits &= ~bits; return before; }
EventBits_t xEventGroupGetBits(EventGroupHandle_t h) { return h->bits; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t h, EventBits_t bits, int clear, int all, unsigned ticks) {
    (void)all; (void)ticks;
    ++wait_calls;
    if (disconnect_pending && deliver_disconnect) {
        /* A queued old-network IP event must not finish this new session. */
        registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
        assert(!(h->bits & CONNECTED));
        disconnect_pending=false;
        driver_associated=false;
        registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    }
    unsigned before=h->bits; if (clear) h->bits &= ~bits; return before;
}
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t esp_event_handler_register(esp_event_base_t base,int32_t id,event_handler_t fn,void *arg) {
    (void)id; (void)arg; if (base==WIFI_EVENT) registered_wifi=fn; else registered_ip=fn; return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) { (void)cfg; return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) {
    record_order(mode==WIFI_MODE_STA?'S':'A');
    if (mode_error!=ESP_OK) return mode_error;
    current_mode=mode; if (mode==WIFI_MODE_APSTA) ++ap_starts; return ESP_OK;
}
esp_err_t esp_wifi_set_config(int mode,const wifi_config_t *cfg) {
    if (mode==WIFI_IF_STA) {
        ++sta_sets;
        if (config_busy_attempts) { --config_busy_attempts; return ESP_ERR_WIFI_STATE; }
        if (config_error==ESP_OK) stored_sta=*cfg;
        return config_error;
    }
    assert(mode==WIFI_IF_AP && cfg->ap.authmode==WIFI_AUTH_WPA2_PSK); return ap_config_error;
}
esp_err_t esp_wifi_get_config(int mode,wifi_config_t *cfg) { assert(mode==WIFI_IF_STA); *cfg=stored_sta; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int mode) { (void)mode; return ESP_OK; }
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_stop(void) {
    ++wifi_stops; record_order('W');
    if (callback_during_stop) registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(int mode) { assert(mode==WIFI_PS_MIN_MODEM); return ESP_OK; }
esp_err_t esp_wifi_connect(void) { ++connects; return connect_error; }
esp_err_t esp_wifi_disconnect(void) { ++disconnects; if (disconnect_error==ESP_OK) disconnect_pending=true; return disconnect_error; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {
    memset(ap,0,sizeof(*ap)); return driver_associated?ESP_OK:ESP_ERR_WIFI_NOT_CONNECT;
}
void vTaskDelay(unsigned ticks) {
    assert(ticks==100); ++delay_calls;
    if (queued_old_connected) {
        queued_old_connected=false;
        registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,NULL);
        registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
        assert(!passport_wifi_connected());
        registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    }
}
esp_err_t esp_netif_init(void) { return ESP_OK; }
void *esp_netif_create_default_wifi_sta(void) { return &event_group; }
void *esp_netif_create_default_wifi_ap(void) { return &event_group; }
uint32_t esp_random(void) { return 123456789; }
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *cfg) {
    assert(cfg->recv_wait_timeout==1 && cfg->send_wait_timeout==1);
    if (server_start_error!=ESP_OK) return server_start_error;
    *h=&event_group; return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t h) {
    assert(h==&event_group && !in_http_handler && semaphore.held);
    assert(xEventGroupGetBits(events)&CLOSING);
    ++stops; record_order('H');
    if (callback_during_stop) {
        registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
        registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    }
    return server_stop_error;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *uri) {
    assert(h==&event_group); ++registered_uris;
    if (registered_uris==fail_uri_number) return ESP_FAIL;
    if (uri->method==HTTP_GET) get_uri=*uri; else post_uri=*uri; return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *req,const char *type) { (void)req; (void)type; return ESP_OK; }
esp_err_t httpd_resp_set_status(httpd_req_t *req,const char *status) {
    assert(strcmp(status,"503 Service Unavailable")==0); req->response=503; return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *req,const char *body,int length) {
    (void)body; (void)length; if (!req->response) req->response=200;
    if (got_ip_in_response) registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *req,const char *body) { return httpd_resp_send(req,body,-1); }
esp_err_t httpd_resp_send_err(httpd_req_t *req,int status,const char *body) { (void)body; req->response=status; return ESP_OK; }
int httpd_req_recv(httpd_req_t *req,char *out,size_t capacity) {
    clock_us+=receive_delay_us;
    if (close_during_receive) xEventGroupSetBits(events,CLOSING);
    size_t n=req->content_len-req->offset; if (n>capacity) n=capacity;
    if (req->recv_chunk && n>req->recv_chunk) n=req->recv_chunk;
    memcpy(out,req->body+req->offset,n); req->offset+=n; return (int)n;
}
static void setup(bool saved) {
    server=NULL; retries=0; registered_uris=connects=disconnects=sta_sets=stops=0;
    disconnect_error=config_error=connect_error=ESP_OK; disconnect_pending=false; deliver_disconnect=true;
    driver_associated=saved; queued_old_connected=false;
    config_busy_attempts=delay_calls=wait_calls=0;
    warning[0]=0;
    current_mode=0; ap_starts=wifi_stops=order_count=0; order[0]=0;
    server_stop_error=mode_error=server_start_error=ap_config_error=ESP_OK; fail_uri_number=0;
    in_http_handler=got_ip_in_response=callback_during_stop=false;
    clock_us=1000000; receive_delay_us=0; close_during_receive=false;
    memset(&stored_sta,0,sizeof(stored_sta)); if (saved) memcpy(stored_sta.sta.ssid,"old-network",11);
    assert(passport_wifi_init()==ESP_OK);
    if (saved) registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
}
static int post_result(const char *body,esp_err_t expected) {
    assert(post_uri.handler);
    httpd_req_t req={.content_len=strlen(body),.body=body,.recv_chunk=3};
    in_http_handler=true;
    assert(post_uri.handler(&req)==expected);
    in_http_handler=false; return req.response;
}
static int post(const char *body) { return post_result(body,ESP_OK); }
static void test_connected_portal_session(void) {
    setup(true); assert(passport_wifi_connected());
    assert(passport_wifi_start_provisioning()==ESP_OK && registered_uris==2);
    passport_wifi_poll(); assert(passport_wifi_provisioning() && stops==0);
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    assert(passport_wifi_provisioning() && stops==0);
    passport_wifi_poll(); assert(!passport_wifi_provisioning() && stops==1);
    assert(passport_wifi_start_provisioning()==ESP_OK);
    unsigned before=connects;
    assert(post("ssid=New+Network&password=abcdefgh")==200);
    assert(disconnects==1 && connects==before+1 && sta_sets==1);
    assert(strcmp((const char *)stored_sta.sta.ssid,"New Network")==0);
    assert(!passport_wifi_connected());
    passport_wifi_poll(); assert(passport_wifi_provisioning());
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    passport_wifi_poll(); assert(!passport_wifi_provisioning() && stops==2);
    assert(passport_wifi_ap_password()[0]==0);
}
static void test_bad_password_retries_are_bounded(void) {
    setup(false); disconnect_error=ESP_ERR_WIFI_NOT_CONNECT;
    assert(post("ssid=Wrong&password=badpassword")==200);
    unsigned before=connects;
    for (unsigned i=0;i<12;i++) registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    assert(connects==before+5 && !passport_wifi_connected());
    passport_wifi_poll(); assert(passport_wifi_provisioning() && stops==0);
}
static void test_reject_bad_inputs_without_changing_credentials(void) {
    setup(false);
    const char *invalid[]={"", "ssid=&password=abcdefgh", "ssid=OK&password=short", "ssid=Bad%", "ssid=Bad%0", "ssid=Bad%GG", "ssid=Bad%00Name", "ssid=OK&password=bad%ZZpassword"};
    for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) assert(post(invalid[i])==400);
    char body[512]; memset(body,0,sizeof(body)); strcpy(body,"ssid="); memset(body+5,'s',33); strcpy(body+38,"&password=abcdefgh"); assert(post(body)==400);
    strcpy(body,"ssid=OK&password="); size_t prefix=strlen(body); memset(body+prefix,'p',64); body[prefix+64]=0; assert(post(body)==400);
    memset(body,'x',sizeof(body)-1); body[sizeof(body)-1]=0; assert(post(body)==400);
    assert(disconnects==0 && connects==0 && sta_sets==0);
}
static void test_open_and_maximum_length_networks(void) {
    setup(false); disconnect_error=ESP_ERR_WIFI_NOT_CONNECT;
    assert(post("ssid=%E6%9C%BA%E6%A0%B8+WiFi&password=")==200);
    assert(strcmp((const char *)stored_sta.sta.ssid,"机核 WiFi")==0);
    assert(stored_sta.sta.password[0]==0);
    char body[160]; strcpy(body,"ssid="); memset(body+5,'s',32); strcpy(body+37,"&password="); size_t prefix=strlen(body); memset(body+prefix,'p',63); body[prefix+63]=0;
    assert(post(body)==200 && stored_sta.sta.password[63]==0);
}
static void test_disconnect_timeout_keeps_portal_and_old_config(void) {
    setup(true); assert(passport_wifi_start_provisioning()==ESP_OK); deliver_disconnect=false;
    unsigned before=connects;
    assert(post("ssid=New&password=abcdefgh")==500);
    assert(sta_sets==0 && connects==before && passport_wifi_provisioning());
    assert(strcmp((const char *)stored_sta.sta.ssid,"old-network")==0);
    assert(strstr(warning,"stage=disconnect_wait") && strstr(warning,"ESP_ERR_TIMEOUT"));
}
static void test_first_setup_disconnect_ok_without_event(void) {
    setup(false); deliver_disconnect=false;
    assert(post("ssid=First+Network&password=abcdefgh")==200);
    assert(disconnects==1 && wait_calls==0 && sta_sets==1 && connects==1);
    assert(strcmp((const char *)stored_sta.sta.ssid,"First Network")==0);
    passport_wifi_poll(); assert(passport_wifi_provisioning());
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    passport_wifi_poll(); assert(!passport_wifi_provisioning());
}
static void test_offline_reconfiguration_without_event(void) {
    setup(true); assert(passport_wifi_start_provisioning()==ESP_OK);
    driver_associated=false;
    /* Saved credentials exhausted their retries; no association exists. */
    for (unsigned i=0;i<6;i++) registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    unsigned before=connects; deliver_disconnect=false;
    assert(post("ssid=Replacement&password=abcdefgh")==200);
    assert(wait_calls==0 && sta_sets==1 && connects==before+1);
}
static void test_cancel_old_attempt_and_queued_events(void) {
    setup(true); assert(passport_wifi_start_provisioning()==ESP_OK);
    driver_associated=false; xEventGroupClearBits(events,CONNECTED);
    config_busy_attempts=2; deliver_disconnect=false; queued_old_connected=true;
    unsigned before=connects;
    assert(post("ssid=New&password=abcdefgh")==200);
    assert(delay_calls==2 && wait_calls==0 && sta_sets==3 && connects==before+1);
    assert(passport_wifi_provisioning() && !passport_wifi_connected());
    assert(strcmp((const char *)stored_sta.sta.ssid,"New")==0);
}
static void test_associated_without_dhcp_waits_for_disconnect(void) {
    setup(false); driver_associated=true;
    registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,NULL);
    assert(!passport_wifi_connected());
    assert(post("ssid=New&password=abcdefgh")==200);
    assert(wait_calls==1 && sta_sets==1 && connects==1);
}
static void test_cancel_busy_attempt_has_bounded_timeout(void) {
    setup(false); deliver_disconnect=false; config_busy_attempts=100;
    assert(post("ssid=New&password=abcdefgh")==500);
    assert(delay_calls==20 && sta_sets==21 && connects==0 && passport_wifi_provisioning());
    assert(stored_sta.sta.ssid[0]==0);
    assert(strstr(warning,"stage=set_config") && strstr(warning,"ESP_ERR_WIFI_STATE"));
    assert(!strstr(warning,"abcdefgh") && !strstr(warning,"ssid="));
}
static void test_saved_disconnect_never_starts_ap(void) {
    setup(true); assert(ap_starts==0 && current_mode==WIFI_MODE_STA);
    unsigned before=connects;
    for (unsigned i=0;i<12;++i) {
        registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
        passport_wifi_poll();
        assert(!passport_wifi_provisioning() && ap_starts==0);
    }
    assert(connects==before+5);
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    assert(passport_wifi_connected());
}
static void test_exit_keeps_sta_connection_and_retries(void) {
    setup(true); assert(passport_wifi_start_provisioning()==ESP_OK);
    unsigned before=connects;
    order_count=0; order[0]=0;
    assert(passport_wifi_end_provisioning()==ESP_OK);
    assert(strcmp(order,"HS")==0 && stops==1 && current_mode==WIFI_MODE_STA);
    assert(passport_wifi_connected() && !passport_wifi_provisioning());
    assert(disconnects==0 && connects==before && stored_sta.sta.ssid[0]);
    assert(passport_wifi_ap_password()[0]==0);
    assert(passport_wifi_end_provisioning()==ESP_OK && stops==1);
    registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    assert(connects==before+1 && !passport_wifi_provisioning());
    assert(passport_wifi_start_provisioning()==ESP_OK);
    assert(passport_wifi_end_provisioning()==ESP_OK);
    assert(retries==1);
    for (unsigned i=0;i<10;++i) registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    assert(connects==before+5 && ap_starts==2);
}
static void test_exit_first_setup_does_not_connect_empty_sta(void) {
    setup(false); assert(ap_starts==1);
    assert(passport_wifi_end_provisioning()==ESP_OK);
    for (unsigned i=0;i<6;++i) registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    passport_wifi_poll();
    assert(!passport_wifi_provisioning() && connects==0 && disconnects==0 && current_mode==WIFI_MODE_STA);
}
static void test_fast_got_ip_never_stops_http_handler(void) {
    setup(false); got_ip_in_response=true;
    assert(post("ssid=New&password=abcdefgh")==200);
    assert(passport_wifi_connected() && passport_wifi_provisioning() && stops==0);
    passport_wifi_poll();
    assert(stops==1 && !passport_wifi_provisioning() && passport_wifi_connected());
}
static void test_shutdown_failures_remain_retryable(void) {
    setup(true); assert(passport_wifi_start_provisioning()==ESP_OK);
    server_stop_error=ESP_FAIL; order_count=0; order[0]=0;
    assert(passport_wifi_end_provisioning()==ESP_FAIL);
    assert(strcmp(order,"H")==0 && server && passport_wifi_provisioning());
    assert(passport_wifi_connected() && passport_wifi_ap_password()[0]);
    server_stop_error=ESP_OK; mode_error=ESP_FAIL;
    assert(passport_wifi_end_provisioning()==ESP_FAIL);
    assert(!server && passport_wifi_provisioning());
    mode_error=ESP_OK;
    assert(passport_wifi_end_provisioning()==ESP_OK);
    assert(stops==2 && !passport_wifi_provisioning() && passport_wifi_connected());
}
static void test_close_rejects_late_form_and_auto_close_retries(void) {
    setup(false); xEventGroupSetBits(events,CLOSING);
    assert(post("ssid=New&password=abcdefgh")==503);
    assert(sta_sets==0 && connects==0);
    xEventGroupClearBits(events,CLOSING);
    assert(post("ssid=New&password=abcdefgh")==200);
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    server_stop_error=ESP_FAIL; passport_wifi_poll();
    assert(passport_wifi_provisioning() && stops==1);
    server_stop_error=ESP_OK; passport_wifi_poll();
    assert(!passport_wifi_provisioning() && stops==2);
}
static void test_wifi_stop_suppresses_shutdown_event_reconnects(void) {
    setup(true); assert(passport_wifi_start_provisioning()==ESP_OK);
    unsigned before=connects;
    callback_during_stop=true; order_count=0; order[0]=0;
    assert(passport_wifi_stop()==ESP_OK);
    assert(strcmp(order,"HW")==0 && wifi_stops==1 && connects==before);
    assert(!passport_wifi_connected() && !passport_wifi_provisioning());
    assert(passport_wifi_ap_password()[0]==0);
}
static void test_partial_start_rolls_back_and_keeps_failure_retryable(void) {
    setup(true); ap_config_error=ESP_FAIL;
    assert(passport_wifi_start_provisioning()==ESP_FAIL);
    assert(!passport_wifi_provisioning() && current_mode==WIFI_MODE_STA && !server);
    assert(passport_wifi_connected() && passport_wifi_ap_password()[0]==0);
    ap_config_error=ESP_OK; server_start_error=ESP_FAIL;
    assert(passport_wifi_start_provisioning()==ESP_FAIL);
    assert(!passport_wifi_provisioning() && current_mode==WIFI_MODE_STA && stops==0);
    server_start_error=ESP_OK; fail_uri_number=2;
    assert(passport_wifi_start_provisioning()==ESP_FAIL);
    assert(!passport_wifi_provisioning() && !server && stops==1);
    registered_uris=0; server_stop_error=ESP_FAIL;
    assert(passport_wifi_start_provisioning()==ESP_FAIL);
    assert(passport_wifi_provisioning() && server && current_mode==WIFI_MODE_APSTA);
    assert(passport_wifi_ap_password()[0] && !(xEventGroupGetBits(events)&CLOSING));
    server_stop_error=ESP_OK;
    assert(passport_wifi_end_provisioning()==ESP_OK && !passport_wifi_provisioning() && !server);
    fail_uri_number=0;
    assert(passport_wifi_start_provisioning()==ESP_OK && passport_wifi_provisioning());
    assert(passport_wifi_start_provisioning()==ESP_OK);
    assert(passport_wifi_end_provisioning()==ESP_OK);
}
static void test_closing_cancels_slow_body_before_mutation(void) {
    setup(false); close_during_receive=true;
    assert(post_result("ssid=New&password=abcdefgh",ESP_FAIL)==0);
    assert(connects==0 && sta_sets==0 && disconnects==0);
    xEventGroupClearBits(events,CLOSING); close_during_receive=false;
    assert(post("ssid=New&password=abcdefgh")==200);
}
static void test_form_total_deadline_and_normal_chunking(void) {
    setup(false); receive_delay_us=1000000;
    int64_t started=clock_us;
    assert(post_result("ssid=New&password=abcdefgh",ESP_FAIL)==0);
    assert(clock_us-started==5000000 && connects==0 && sta_sets==0);
    /* The same fragmented phone request fits the five-second total budget. */
    receive_delay_us=100000;
    assert(post("ssid=New&password=abcdefgh")==200);
    assert(connects==1 && sta_sets==1);
}
int main(void) {
    test_connected_portal_session(); test_bad_password_retries_are_bounded();
    test_reject_bad_inputs_without_changing_credentials(); test_open_and_maximum_length_networks();
    test_disconnect_timeout_keeps_portal_and_old_config();
    test_first_setup_disconnect_ok_without_event(); test_offline_reconfiguration_without_event();
    test_cancel_old_attempt_and_queued_events(); test_associated_without_dhcp_waits_for_disconnect();
    test_cancel_busy_attempt_has_bounded_timeout();
    test_saved_disconnect_never_starts_ap(); test_exit_keeps_sta_connection_and_retries();
    test_exit_first_setup_does_not_connect_empty_sta(); test_fast_got_ip_never_stops_http_handler();
    test_shutdown_failures_remain_retryable(); test_close_rejects_late_form_and_auto_close_retries();
    test_wifi_stop_suppresses_shutdown_event_reconnects();
    test_partial_start_rolls_back_and_keeps_failure_retryable();
    test_closing_cancels_slow_body_before_mutation(); test_form_total_deadline_and_normal_chunking();
    puts("Passport Wi-Fi actual source: PASS (20 scenario groups; AP lifecycle, safe shutdown, bounded body, STA preservation, first setup, retries and invalid forms)");
    return 0;
}
