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
static unsigned lock_takes, lock_gives;
static unsigned nvs_commits;
static unsigned char nvs_blob[1024], nvs_staged[1024];
static size_t nvs_length, nvs_staged_length;
static esp_err_t nvs_commit_error;
static int wifi_storage;
static wifi_config_t legacy_sta;
static char attempted_ssids[64][33];
static char response_body[8192];
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h) {
    assert(!in_http_handler&&!strcmp(name,"gpp_wifi"));
    if(mode==NVS_READONLY&&!nvs_length) return ESP_ERR_NVS_NOT_FOUND;
    assert(mode==NVS_READONLY||mode==NVS_READWRITE);*h=1;return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *length) {
    assert(h==1&&!strcmp(key,"profiles")&&!in_http_handler);
    if(!nvs_length) return ESP_ERR_NVS_NOT_FOUND;
    if(!out) {*length=nvs_length;return ESP_OK;}
    if(*length<nvs_length) {*length=nvs_length;return ESP_ERR_NVS_INVALID_LENGTH;}
    memcpy(out,nvs_blob,nvs_length);*length=nvs_length;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size) {
    assert(h==1&&!strcmp(key,"profiles")&&!in_http_handler&&size<=sizeof(nvs_staged));
    memcpy(nvs_staged,data,size);nvs_staged_length=size;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h==1&&!in_http_handler);++nvs_commits;
    if(nvs_commit_error!=ESP_OK) return nvs_commit_error;
    memcpy(nvs_blob,nvs_staged,nvs_staged_length);nvs_length=nvs_staged_length;return ESP_OK;
}
void nvs_close(nvs_handle_t h) {assert(h==1);}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { semaphore.held=false; return &semaphore; }
int xSemaphoreTake(SemaphoreHandle_t h,unsigned ticks) {
    assert(h==&semaphore && !h->held && !in_http_handler && ticks==portMAX_DELAY);
    h->held=true;++lock_takes; return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t h) { assert(h->held); h->held=false;++lock_gives; return pdTRUE; }
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
    assert(!in_http_handler);
    if (mode==WIFI_IF_STA) {
        ++sta_sets;
        if (config_busy_attempts) { --config_busy_attempts; return ESP_ERR_WIFI_STATE; }
        if (config_error==ESP_OK) stored_sta=*cfg;
        return config_error;
    }
    assert(mode==WIFI_IF_AP && cfg->ap.authmode==WIFI_AUTH_WPA2_PSK); return ap_config_error;
}
esp_err_t esp_wifi_get_config(int mode,wifi_config_t *cfg) { assert(mode==WIFI_IF_STA); *cfg=wifi_storage==WIFI_STORAGE_RAM?stored_sta:legacy_sta; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int mode) { wifi_storage=mode; return ESP_OK; }
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_stop(void) {
    ++wifi_stops; record_order('W');
    if (callback_during_stop) registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(int mode) { assert(mode==WIFI_PS_MIN_MODEM); return ESP_OK; }
esp_err_t esp_wifi_connect(void) {
    assert(!in_http_handler&&connects<64);memcpy(attempted_ssids[connects],stored_sta.sta.ssid,32);
    attempted_ssids[connects][32]=0;++connects; return connect_error;
}
esp_err_t esp_wifi_disconnect(void) { assert(!in_http_handler);++disconnects; if (disconnect_error==ESP_OK&&driver_associated) disconnect_pending=true; return disconnect_error; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {
    memset(ap,0,sizeof(*ap));if(driver_associated) memcpy(ap->ssid,stored_sta.sta.ssid,32);
    return driver_associated?ESP_OK:ESP_ERR_WIFI_NOT_CONNECT;
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
    if(body) {size_t n=length<0?strlen(body):(size_t)length;assert(n<sizeof(response_body));memcpy(response_body,body,n);response_body[n]=0;}
    if (!req->response) req->response=200;
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
static void test_poll(void) {
    if(disconnect_pending&&deliver_disconnect) {
        disconnect_pending=false;driver_associated=false;
        registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    }
    if(queued_old_connected) {
        queued_old_connected=false;
        registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,NULL);
        registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    }
    clock_us+=100000;passport_wifi_poll();
    assert(!semaphore.held&&lock_takes==lock_gives);
}
static void setup_device(bool saved,bool preserve) {
    if(!preserve) nvs_length=nvs_staged_length=0;
    nvs_commits=0;nvs_commit_error=ESP_OK;
    wifi_storage=WIFI_STORAGE_FLASH;memset(&legacy_sta,0,sizeof(legacy_sta));
    if(saved) memcpy(legacy_sta.sta.ssid,"old-network",11);
    lock_takes=lock_gives=0;
    profiles_dirty=apply_pending=manual_attempt=cycle_exhausted=was_connected=false;
    active_index=-1;attempted_mask=offline_backoff_s=0;
    connect_deadline=apply_deadline=next_apply=persist_retry_at=offline_retry_at=0;
    memset(&active_profile,0,sizeof(active_profile));memset(&pending_profile,0,sizeof(pending_profile));pending_lock.held=false;
    pending_count=0;pending_valid=false;memset(pending_open,0,sizeof(pending_open));
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
    test_poll();test_poll();
    if (saved) {driver_associated=true;registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);test_poll();}
    sta_sets=disconnects=0;
}
static void setup(bool saved) {setup_device(saved,false);}
static void reboot_device(void) {setup_device(false,true);}
static int post_result(const char *body,esp_err_t expected) {
    assert(post_uri.handler);
    httpd_req_t req={.content_len=strlen(body),.body=body,.recv_chunk=3};
    in_http_handler=true;
    assert(post_uri.handler(&req)==expected);
    in_http_handler=false;if(req.response==200) {test_poll();test_poll();}
    assert(!semaphore.held&&lock_takes==lock_gives);return req.response;
}
static int post(const char *body) { return post_result(body,ESP_OK); }
static void disconnect_event(void) {
    driver_associated=false;registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    test_poll();test_poll();assert(!semaphore.held&&lock_takes==lock_gives);
}
static void test_connected_portal_session(void) {
    setup(true);assert(passport_wifi_connected());
    assert(passport_wifi_start_provisioning()==ESP_OK&&registered_uris==2);
    test_poll();assert(passport_wifi_provisioning()&&stops==0);
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);test_poll();
    assert(passport_wifi_provisioning()&&stops==0); /* Existing IP must not close a newly opened editor. */
    assert(passport_wifi_end_provisioning()==ESP_OK&&stops==1);
    assert(passport_wifi_start_provisioning()==ESP_OK);
    assert(post("ssid=New+Network&password=abcdefgh")==200);
    assert(!strcmp((char*)stored_sta.sta.ssid,"New Network")&&!passport_wifi_connected());
    driver_associated=true;registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);test_poll();
    assert(!passport_wifi_provisioning()&&stops==2&&passport_wifi_connected());
    assert(!passport_wifi_ap_password()[0]);
}
static void test_bad_password_retries_are_bounded(void) {
    setup(false);disconnect_error=ESP_ERR_WIFI_NOT_CONNECT;
    assert(post("ssid=Wrong&password=badpassword")==200);
    for(unsigned i=0;i<12;i++)disconnect_event();
    assert(connects==3&&!passport_wifi_connected());
    assert(passport_wifi_provisioning()&&!stops);
}
static void test_reject_bad_inputs_without_changing_credentials(void) {
    setup(false);
    const char *invalid[]={"", "ssid=&password=abcdefgh", "ssid=OK&password=short", "ssid=Bad%", "ssid=Bad%0", "ssid=Bad%GG", "ssid=Bad%00Name", "ssid=OK&password=bad%ZZpassword"};
    for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) assert(post(invalid[i])==400);
    char body[512]; memset(body,0,sizeof(body)); strcpy(body,"ssid="); memset(body+5,'s',33); strcpy(body+38,"&password=abcdefgh"); assert(post(body)==400);
    strcpy(body,"ssid=OK&password="); size_t prefix=strlen(body); memset(body+prefix,'p',64); body[prefix+64]=0; assert(post(body)==400);
    char huge[641];memset(huge,'x',sizeof(huge)-1);huge[sizeof(huge)-1]=0;assert(post(huge)==400);
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
    setup(true);assert(passport_wifi_start_provisioning()==ESP_OK);deliver_disconnect=false;
    assert(post("ssid=New&password=abcdefgh")==200);
    assert(sta_sets==0&&!strcmp((char*)stored_sta.sta.ssid,"old-network"));
    for(unsigned i=0;i<130;i++)test_poll();
    assert(sta_sets==0&&connects==1&&passport_wifi_provisioning());
    assert(!apply_pending&&!(xEventGroupGetBits(events)&RECONFIGURING));
}
static void test_first_setup_disconnect_ok_without_event(void) {
    setup(false);deliver_disconnect=false;
    assert(post("ssid=First+Network&password=abcdefgh")==200);
    assert(wait_calls==0&&sta_sets==1&&connects==1);
    driver_associated=true;registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);test_poll();
    assert(!passport_wifi_provisioning()&&passport_wifi_connected());
}
static void test_offline_reconfiguration_without_event(void) {
    setup(true);assert(passport_wifi_start_provisioning()==ESP_OK);driver_associated=false;
    xEventGroupClearBits(events,CONNECTED);deliver_disconnect=false;
    assert(post("ssid=Replacement&password=abcdefgh")==200);
    assert(wait_calls==0&&!strcmp((char*)stored_sta.sta.ssid,"Replacement"));
}
static void test_cancel_old_attempt_and_queued_events(void) {
    setup(true);assert(passport_wifi_start_provisioning()==ESP_OK);
    driver_associated=false;xEventGroupClearBits(events,CONNECTED);
    config_busy_attempts=2;deliver_disconnect=false;queued_old_connected=true;
    assert(post("ssid=New&password=abcdefgh")==200);
    test_poll();test_poll();
    assert(sta_sets==3&&wait_calls==0&&passport_wifi_provisioning()&&!passport_wifi_connected());
    assert(!strcmp((char*)stored_sta.sta.ssid,"New"));
}
static void test_associated_without_dhcp_waits_for_disconnect(void) {
    setup(false);driver_associated=true;
    registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,NULL);
    assert(post("ssid=New&password=abcdefgh")==200);
    assert(wait_calls==0&&sta_sets==1&&connects==1&&!passport_wifi_connected());
}
static void test_cancel_busy_attempt_has_bounded_timeout(void) {
    setup(false);deliver_disconnect=false;config_busy_attempts=100;
    assert(post("ssid=New&password=abcdefgh")==200);
    for(unsigned i=0;i<70;i++)test_poll();
    assert(sta_sets<=63&&connects==0&&passport_wifi_provisioning()&&!apply_pending);
    assert(!stored_sta.sta.ssid[0]&&!strstr(warning,"abcdefgh"));
}
static void test_saved_disconnect_never_starts_ap(void) {
    setup(true);assert(!ap_starts&&current_mode==WIFI_MODE_STA);
    for(unsigned i=0;i<12;i++) {disconnect_event();assert(!passport_wifi_provisioning()&&!ap_starts);}
    assert(connects==4); /* successful initial connection plus three reconnect attempts */
    clock_us+=61000000;test_poll();test_poll();
    assert(connects==5&&!ap_starts&&!passport_wifi_provisioning());
}
static void test_exit_keeps_sta_connection_and_retries(void) {
    setup(true);assert(passport_wifi_start_provisioning()==ESP_OK);
    unsigned before=connects;order_count=0;order[0]=0;
    assert(passport_wifi_end_provisioning()==ESP_OK&&!strcmp(order,"HS"));
    assert(passport_wifi_connected()&&!passport_wifi_provisioning()&&connects==before);
    assert(!passport_wifi_ap_password()[0]);
    assert(passport_wifi_end_provisioning()==ESP_OK&&stops==1);
    disconnect_event();assert(connects==before+1&&!passport_wifi_provisioning());
    assert(passport_wifi_start_provisioning()==ESP_OK&&passport_wifi_end_provisioning()==ESP_OK);
    for(unsigned i=0;i<12;i++)disconnect_event();
    assert(!passport_wifi_provisioning()&&ap_starts==2);
}
static void test_exit_first_setup_does_not_connect_empty_sta(void) {
    setup(false); assert(ap_starts==1);
    assert(passport_wifi_end_provisioning()==ESP_OK);
    for (unsigned i=0;i<6;++i) disconnect_event();
    test_poll();
    assert(!passport_wifi_provisioning() && connects==0 && disconnects==0 && current_mode==WIFI_MODE_STA);
}
static void test_fast_got_ip_never_stops_http_handler(void) {
    setup(false);got_ip_in_response=true;
    assert(post("ssid=New&password=abcdefgh")==200);
    assert(!passport_wifi_connected()&&passport_wifi_provisioning()&&!stops);
    got_ip_in_response=false;driver_associated=true;
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);test_poll();
    assert(stops==1&&!passport_wifi_provisioning()&&passport_wifi_connected());
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
    driver_associated=true;
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    server_stop_error=ESP_FAIL; test_poll();
    assert(passport_wifi_provisioning() && stops==1);
    server_stop_error=ESP_OK; test_poll();
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
static void ip_success(void) {
    driver_associated=true;
    registered_ip(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);test_poll();
    assert(passport_wifi_connected()&&!semaphore.held&&lock_takes==lock_gives);
}
static void add_network(const char *ssid,const char *password) {
    assert(passport_wifi_start_provisioning()==ESP_OK);
    char form[240];snprintf(form,sizeof(form),"ssid=%s&password=%s",ssid,password);
    assert(post(form)==200);ip_success();assert(!passport_wifi_provisioning());
}
static bool persistent_has(const char *ssid,const char *password) {
    profiles_blob_t image;assert(nvs_length==sizeof(image));memcpy(&image,nvs_blob,sizeof(image));
    assert(image.magic==WIFI_PROFILES_MAGIC&&image.count<=2);
    for(unsigned i=0;i<image.count;i++) if(!strcmp(image.profiles[i].ssid,ssid))
        return !password||!strcmp(image.profiles[i].password,password);
    return false;
}
static void test_legacy_migration_and_persistent_reboot(void) {
    setup(true);assert(nvs_length&&persistent_has("old-network",NULL));
    assert(wifi_storage==WIFI_STORAGE_RAM);reboot_device();
    assert(!passport_wifi_provisioning()&&!ap_starts&&connects==1);
    assert(!strcmp(attempted_ssids[0],"old-network"));ip_success();
}
static void test_single_optional_profile_startup_reconnect_and_reboot(void) {
    setup(false);assert(passport_wifi_provisioning()&&connects==0);
    assert(post("ssid=Only+Network&password=abcdefgh")==200);ip_success();
    profiles_blob_t image;memcpy(&image,nvs_blob,sizeof(image));assert(image.count==1);
    assert(persistent_has("Only Network","abcdefgh"));reboot_device();
    assert(!ap_starts&&!passport_wifi_provisioning()&&connects==1);
    assert(!strcmp(attempted_ssids[0],"Only Network"));ip_success();
    assert(nvs_commits==0); /* Stable successful network does not rewrite Flash each boot. */
    disconnect_event();assert(connects==2&&!passport_wifi_provisioning());ip_success();
    reboot_device();for(unsigned i=0;i<10;i++) disconnect_event();
    assert(connects==3&&!passport_wifi_connected()&&!passport_wifi_provisioning()&&!ap_starts);
    for(unsigned i=0;i<10;i++) test_poll();
    assert(connects==3);
}
static void test_two_profiles_failover_and_success_priority(void) {
    setup(false);add_network("Home","homepass1");add_network("Office","officepass1");
    assert(persistent_has("Home",NULL)&&persistent_has("Office",NULL));reboot_device();
    assert(!strcmp(attempted_ssids[0],"Office"));
    for(unsigned i=0;i<3;i++) disconnect_event();
    assert(!strcmp((char *)stored_sta.sta.ssid,"Home"));ip_success();
    reboot_device();assert(!strcmp(attempted_ssids[0],"Home"));
    for(unsigned i=0;i<12;i++) disconnect_event();
    assert(connects==6&&!passport_wifi_connected()&&!passport_wifi_provisioning()&&!ap_starts);
}
static void test_batch_saved_before_connection_and_offline_reboot(void) {
    setup(false);
    assert(post("ssid1=Home&password1=homepass1&ssid2=Office&password2=officepass1")==200);
    assert(!passport_wifi_connected()&&passport_wifi_profile_count()==2);
    assert(persistent_has("Home","homepass1")&&persistent_has("Office","officepass1"));
    for(unsigned i=0;i<12;i++)disconnect_event();
    reboot_device();assert(passport_wifi_profile_count()==2);
    assert(persistent_has("Home","homepass1")&&persistent_has("Office","officepass1"));
    for(unsigned i=0;i<12;i++)disconnect_event();
    assert(connects==6&&!passport_wifi_connected()&&!ap_starts&&!passport_wifi_provisioning());
}
static void test_profile_one_update_preserves_other_bank(void) {
    setup(false);add_network("Home","homepass1");add_network("Office","officepass1");
    assert(passport_wifi_start_provisioning()==ESP_OK);
    assert(post("ssid1=Home&password1=newpass12&ssid2=&password2=")==200);
    assert(persistent_has("Home","newpass12")&&persistent_has("Office","officepass1"));
    for(unsigned i=0;i<12;i++)disconnect_event();
    reboot_device();assert(persistent_has("Home","newpass12")&&persistent_has("Office","officepass1"));
}
static void test_third_network_replaces_least_recent_success(void) {
    setup(false);add_network("Home","homepass1");add_network("Office","officepass1");
    add_network("Home","homepass2");add_network("Travel","travelpass1");
    assert(persistent_has("Home","homepass2")&&persistent_has("Travel",NULL));
    assert(!persistent_has("Office",NULL));reboot_device();
    assert(!strcmp(attempted_ssids[0],"Travel"));
}
static void test_chinese_ssid_byte_limit_and_encoding(void) {
    setup(false);char form[256];strcpy(form,"ssid=");
    for(unsigned i=0;i<10;i++) strcat(form,"%E6%A0%B8");
    strcat(form,"AB&password=abcdefgh");assert(post(form)==200);ip_success();
    const char *ssid="核核核核核核核核核核AB";
    assert(strlen(ssid)==32&&!memcmp(stored_sta.sta.ssid,ssid,32));
    assert(persistent_has(ssid,NULL));reboot_device();
    assert(!strcmp(attempted_ssids[0],ssid));
    assert(passport_wifi_start_provisioning()==ESP_OK);
    strcpy(form,"ssid=");for(unsigned i=0;i<11;i++) strcat(form,"%E6%A0%B8");
    strcat(form,"&password=abcdefgh");assert(post(form)==400);
    assert(post("ssid=%E6%A0&password=abcdefgh")==400);
    assert(post("ssid=%C0%AF&password=abcdefgh")==400);
    assert(post("ssid=%ED%A0%80&password=abcdefgh")==400);
    assert(post("ssid=Space+%26+Plus%2B&password=abcdefgh")==200);
    assert(!strcmp((char *)stored_sta.sta.ssid,"Space & Plus+"));
}
static void test_dhcp_deadline_switches_profile_without_ap(void) {
    setup(false);add_network("Home","homepass1");add_network("Office","officepass1");
    reboot_device();assert(!strcmp((char*)stored_sta.sta.ssid,"Office"));
    driver_associated=true;registered_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,NULL);
    for(unsigned i=0;i<3;i++){clock_us+=21000000;test_poll();test_poll();}
    assert(!strcmp((char*)stored_sta.sta.ssid,"Home"));
    for(unsigned i=0;i<3;i++){clock_us+=21000000;test_poll();test_poll();}
    assert(!passport_wifi_provisioning()&&!ap_starts&&!passport_wifi_connected());
    assert(lock_takes==lock_gives&&!semaphore.held);
}
static void test_invalid_second_and_duplicate_reject_whole_batch(void) {
    setup(false);add_network("Home","homepass1");
    unsigned char before[1024];size_t len=nvs_length;memcpy(before,nvs_blob,len);
    assert(passport_wifi_start_provisioning()==ESP_OK);
    unsigned before_sets=sta_sets,before_connects=connects;
    const char *forms[]={
        "ssid1=Home&password1=changed12&ssid2=Office&password2=bad",
        "ssid1=Home&password1=changed12&ssid2=Home&password2=officepass1",
        "ssid1=Home&password1=changed12&ssid2=&password2=officepass1",
        "ssid1=Home&password1=changed12&ssid2=%E6%A0&password2=officepass1"};
    for(unsigned i=0;i<sizeof(forms)/sizeof(forms[0]);i++)assert(post(forms[i])==400);
    assert(!memcmp(before,nvs_blob,len)&&sta_sets==before_sets&&connects==before_connects);
}
static void test_page_escapes_ssid_and_never_returns_passwords(void) {
    setup(false);add_network("Home%26%3C%22%27","privatepass1");
    assert(passport_wifi_start_provisioning()==ESP_OK);
    httpd_req_t req={0};in_http_handler=true;assert(get_uri.handler(&req)==ESP_OK);in_http_handler=false;
    assert(strstr(response_body,"Home&amp;&lt;&quot;&#39;")&&!strstr(response_body,"privatepass1"));
    assert(strstr(response_body,"name=ssid1")&&strstr(response_body,"name=ssid2"));
    assert(strstr(response_body,"name=password1")&&strstr(response_body,"name=password2"));
    assert(!semaphore.held&&lock_takes==lock_gives);
}
static void test_commit_failure_preserves_durable_bank_and_retries(void) {
    setup(false);add_network("Home","homepass1");
    unsigned char before[1024];size_t len=nvs_length;memcpy(before,nvs_blob,len);
    assert(passport_wifi_start_provisioning()==ESP_OK);nvs_commit_error=ESP_FAIL;
    assert(post("ssid1=Home&password1=changed12&ssid2=Office&password2=officepass1")==200);
    assert(nvs_length==len&&!memcmp(before,nvs_blob,len));
    assert(strstr(passport_wifi_status_text(),"保存失败"));
    nvs_commit_error=ESP_OK;clock_us+=61000000;test_poll();
    assert(persistent_has("Home","changed12")&&persistent_has("Office","officepass1"));
}
static void queue_without_poll(const char *body) {
    httpd_req_t req={.content_len=strlen(body),.body=body,.recv_chunk=3};
    unsigned before_sets=sta_sets,before_connects=connects,before_commits=nvs_commits;
    in_http_handler=true;assert(post_uri.handler(&req)==ESP_OK);in_http_handler=false;
    assert(req.response==200&&sta_sets==before_sets&&connects==before_connects&&nvs_commits==before_commits);
    assert(!semaphore.held&&lock_takes==lock_gives);
}
static void test_accepted_post_immediate_exit_is_not_lost(void) {
    setup(false);
    queue_without_poll("ssid1=Home&password1=homepass1&ssid2=Office&password2=officepass1");
    assert(nvs_length==0);assert(passport_wifi_end_provisioning()==ESP_OK);
    assert(persistent_has("Home","homepass1")&&persistent_has("Office","officepass1"));
    assert(!passport_wifi_provisioning());test_poll();test_poll();assert(connects==1);
    ip_success();reboot_device();assert(persistent_has("Office","officepass1"));
}
static void test_accepted_post_immediate_stop_is_persistent_without_connect(void) {
    setup(false);queue_without_poll("ssid1=Home&password1=homepass1");
    callback_during_stop=true;assert(passport_wifi_stop()==ESP_OK);
    assert(connects==0&&wifi_stops==1&&!passport_wifi_connected()&&!passport_wifi_provisioning());
    assert(persistent_has("Home","homepass1"));reboot_device();
    assert(connects==1&&!ap_starts&&!strcmp(attempted_ssids[0],"Home"));
}
static void test_queued_stop_commit_failure_is_reported_and_retryable(void) {
    setup(false);queue_without_poll("ssid1=Home&password1=homepass1");
    nvs_commit_error=ESP_FAIL;assert(passport_wifi_stop()==ESP_FAIL);
    assert(nvs_length==0&&profiles_dirty&&connects==0);
    nvs_commit_error=ESP_OK;clock_us+=61000000;test_poll();
    assert(persistent_has("Home","homepass1"));
}
static void test_offline_backoff_prevents_hot_loop_and_grows(void) {
    setup(false);add_network("Home","homepass1");add_network("Office","officepass1");
    reboot_device();for(unsigned i=0;i<12;i++)disconnect_event();
    assert(connects==6&&offline_backoff_s==60);
    clock_us=offline_retry_at-100001;test_poll();assert(connects==6);
    clock_us=offline_retry_at;test_poll();test_poll();assert(connects==7);
    for(unsigned i=0;i<12;i++)disconnect_event();
    assert(connects==12&&offline_backoff_s==120&&!ap_starts&&!passport_wifi_provisioning());
}
static void test_queued_submission_cannot_be_overwritten(void) {
    setup(false);queue_without_poll("ssid1=Home&password1=homepass1");
    assert(post("ssid1=Office&password1=officepass1")==503);
    test_poll();test_poll();assert(persistent_has("Home","homepass1"));
    assert(!persistent_has("Office",NULL));
}
static void test_blank_known_password_is_retained_unless_open_explicit(void) {
    setup(false);add_network("Home","homepass1");
    assert(passport_wifi_start_provisioning()==ESP_OK);
    assert(post("ssid1=Home&password1=&ssid2=&password2=")==200);
    assert(persistent_has("Home","homepass1"));ip_success();
    assert(passport_wifi_start_provisioning()==ESP_OK);
    assert(post("ssid1=Home&password1=&open1=1")==200);
    assert(persistent_has("Home",""));
}
int main(void) {
#ifdef _WIN32
    _set_error_mode(1);
#endif
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
    test_legacy_migration_and_persistent_reboot();test_single_optional_profile_startup_reconnect_and_reboot();
    test_two_profiles_failover_and_success_priority();
    test_batch_saved_before_connection_and_offline_reboot();test_profile_one_update_preserves_other_bank();
    test_third_network_replaces_least_recent_success();test_chinese_ssid_byte_limit_and_encoding();
    test_dhcp_deadline_switches_profile_without_ap();
    test_invalid_second_and_duplicate_reject_whole_batch();
    test_page_escapes_ssid_and_never_returns_passwords();
    test_commit_failure_preserves_durable_bank_and_retries();
    test_accepted_post_immediate_exit_is_not_lost();
    test_accepted_post_immediate_stop_is_persistent_without_connect();
    test_queued_stop_commit_failure_is_reported_and_retryable();
    test_offline_backoff_prevents_hot_loop_and_grows();
    test_queued_submission_cannot_be_overwritten();
    test_blank_known_password_is_retained_unless_open_explicit();
    puts("Passport Wi-Fi actual source: PASS (37 scenario groups; retained AP/race faults, optional single/dual profile persistence/migration/failover, atomic offline saves/LRU, UTF-8, DHCP, HTML privacy, accepted-submit exit/stop/overwrite races, password preservation and durable commit failures)");
    return 0;
}
