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
#include "nvs.h"
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
#define REQUESTED BIT9
#define SAVE_SUCCESS BIT10
#define WIFI_PROFILES_MAGIC UINT32_C(0x47575032)
#define CONNECT_TIMEOUT_US INT64_C(20000000)
#define APPLY_TIMEOUT_US INT64_C(2000000)
#define PROFILE_RETRIES 3
static EventGroupHandle_t events;
/* Only application worker APIs take this lock. HTTP/event callbacks must never
 * take it: httpd_stop() waits for any running HTTP handler to return. */
static SemaphoreHandle_t lifecycle;
static httpd_handle_t server;
static char ap_password[13];
static unsigned retries;
typedef struct { char ssid[33], password[64]; uint32_t last_success; } wifi_profile_t;
typedef struct { uint32_t magic, sequence; uint8_t count, reserved[3]; wifi_profile_t profiles[2]; } profiles_blob_t;
static profiles_blob_t profiles;
static wifi_profile_t active_profile, pending_profile[2];
static uint8_t pending_count;
static bool pending_open[2];
static const char *status_text="未连接";
static portMUX_TYPE pending_lock = portMUX_INITIALIZER_UNLOCKED;
static int active_index = -1;
static unsigned attempted_mask, offline_backoff_s;
static bool profiles_dirty, apply_pending, manual_attempt, cycle_exhausted, was_connected, pending_valid;
static int64_t connect_deadline, apply_deadline, next_apply, persist_retry_at, offline_retry_at;

static esp_err_t end_provisioning_locked(void);
static const char page[] = "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>GCORES Passport</title><style>body{background:#101216;color:#eee;font:16px system-ui;max-width:420px;margin:32px auto;padding:24px}h1{border-left:5px solid #ef3b45;padding-left:16px}fieldset{border:1px solid #444;border-radius:6px;margin:16px 0;padding:14px}input,button{box-sizing:border-box;width:100%%;padding:12px;margin:8px 0;background:#20242a;color:white;border:1px solid #444;border-radius:6px}input[type=checkbox]{width:auto}button{background:#e73541}</style><h1>GCORES / 机核</h1><p>保存一个或两个 2.4 GHz 网络，第二个可选。中文名称最多 32 字节。网络暂未开启也可以保存。</p><p>已保存网络的密码留空表示保留；新网络留空表示开放网络。密码不会显示。</p><form method=post action=/connect><fieldset><legend>网络 1</legend><input name=ssid1 maxlength=32 value=\"%s\" placeholder='网络名称 SSID'><input name=password1 type=password maxlength=63 autocomplete=new-password placeholder='新密码（可留空）'><label><input name=open1 type=checkbox value=1>设为开放网络</label></fieldset><fieldset><legend>网络 2（可选）</legend><input name=ssid2 maxlength=32 value=\"%s\" placeholder='网络名称 SSID'><input name=password2 type=password maxlength=63 autocomplete=new-password placeholder='新密码（可留空）'><label><input name=open2 type=checkbox value=1>设为开放网络</label></fieldset><button>保存并连接</button></form>";
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    EventBits_t bits=xEventGroupGetBits(events);
    if (base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP) {
        if(!(bits&(RECONFIGURING|STOPPING)))xEventGroupSetBits(events,SAVE_SUCCESS);
    } else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events,CONNECTED|SAVE_SUCCESS);xEventGroupSetBits(events,DISCONNECTED);
    }
}
static bool ssid_utf8_valid(const char *text) {
    unsigned left=0,min=0x80,max=0xbf;
    for(const unsigned char*p=(const unsigned char*)text;*p;p++){
        unsigned c=*p;
        if(left){if(c<min||c>max)return false;left--;min=0x80;max=0xbf;continue;}
        if(c<0x80){if(c<0x20||c==0x7f)return false;continue;}
        min=0x80;max=0xbf;
        if(c>=0xc2&&c<=0xdf)left=1;
        else if(c>=0xe0&&c<=0xef){left=2;if(c==0xe0)min=0xa0;if(c==0xed)max=0x9f;}
        else if(c>=0xf0&&c<=0xf4){left=3;if(c==0xf0)min=0x90;if(c==0xf4)max=0x8f;}
        else return false;
    }
    return !left;
}
static bool profile_valid(const wifi_profile_t*p){
    if(!memchr(p->ssid,0,sizeof(p->ssid))||!memchr(p->password,0,sizeof(p->password)))return false;
    size_t sl=strlen(p->ssid),pl=strlen(p->password);
    return sl&&sl<=32&&ssid_utf8_valid(p->ssid)&&(!pl||(pl>=8&&pl<=63));
}
static bool load_profiles(void){
    nvs_handle_t h;profiles_blob_t saved={0};size_t size=sizeof(saved);
    if(nvs_open("gpp_wifi",NVS_READONLY,&h)!=ESP_OK)return false;
    esp_err_t err=nvs_get_blob(h,"profiles",&saved,&size);nvs_close(h);
    bool valid=err==ESP_OK&&size==sizeof(saved)&&saved.magic==WIFI_PROFILES_MAGIC&&saved.count&&saved.count<=2&&!saved.reserved[0]&&!saved.reserved[1]&&!saved.reserved[2];
    for(unsigned i=0;valid&&i<saved.count;i++)valid=profile_valid(&saved.profiles[i])&&saved.profiles[i].last_success<=saved.sequence;
    if(valid&&saved.count==2&&!strcmp(saved.profiles[0].ssid,saved.profiles[1].ssid))valid=false;
    if(valid)profiles=saved;
    memset(&saved,0,sizeof(saved));return valid;
}
static void persist_profiles(int64_t now){
    if(!profiles_dirty||now<persist_retry_at)return;
    nvs_handle_t h;esp_err_t err=nvs_open("gpp_wifi",NVS_READWRITE,&h);
    if(err==ESP_OK){err=nvs_set_blob(h,"profiles",&profiles,sizeof(profiles));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}
    if(err==ESP_OK){profiles_dirty=false;persist_retry_at=0;}
    else{persist_retry_at=now+INT64_C(60000000);ESP_LOGW("passport_wifi","Saved network commit failed; retry deferred");}
}
static int preferred_profile(void){
    if(!profiles.count)return -1;
    return profiles.count==2&&profiles.profiles[1].last_success>profiles.profiles[0].last_success?1:0;
}
static void remember_success(void){
    taskENTER_CRITICAL(&pending_lock);
    int found=-1;for(unsigned i=0;i<profiles.count;i++)if(!strcmp(profiles.profiles[i].ssid,active_profile.ssid))found=(int)i;
    if(found>=0&&!strcmp(profiles.profiles[found].password,active_profile.password)&&found==preferred_profile()&&profiles.profiles[found].last_success){
        active_profile=profiles.profiles[found];active_index=found;taskEXIT_CRITICAL(&pending_lock);return; /* Stable MRU: no boot/reconnect Flash write. */
    }
    if(found<0){if(profiles.count<2)found=profiles.count++;else found=profiles.profiles[0].last_success<=profiles.profiles[1].last_success?0:1;}
    if(profiles.sequence==UINT32_MAX){int best=preferred_profile();for(unsigned i=0;i<profiles.count;i++)profiles.profiles[i].last_success=(int)i==best?2:1;profiles.sequence=2;}
    active_profile.last_success=++profiles.sequence;profiles.profiles[found]=active_profile;active_index=found;
    profiles.magic=WIFI_PROFILES_MAGIC;profiles_dirty=true;persist_retry_at=0;
    taskEXIT_CRITICAL(&pending_lock);
}
/* Cancellation/configuration advance in poll; never wait for association/DHCP. */
static void begin_attempt(const wifi_profile_t*profile,int index,int64_t now){
    active_profile=*profile;active_index=index;cycle_exhausted=false;connect_deadline=0;status_text="连接中";
    apply_deadline=now+APPLY_TIMEOUT_US;next_apply=now;apply_pending=true;
    xEventGroupSetBits(events,RECONFIGURING);xEventGroupClearBits(events,CONNECTED|DISCONNECTED|SAVE_SUCCESS);
    esp_err_t err=esp_wifi_disconnect();
    if(err!=ESP_OK&&err!=ESP_ERR_WIFI_NOT_CONNECT){apply_pending=false;xEventGroupClearBits(events,RECONFIGURING);xEventGroupSetBits(events,DISCONNECTED);}
}
static bool advance_profile(int64_t now){
    if(manual_attempt)return false;
    int best=-1;for(unsigned i=0;i<profiles.count;i++)if(!(attempted_mask&(1u<<i))&&(best<0||profiles.profiles[i].last_success>profiles.profiles[best].last_success))best=(int)i;
    if(best<0)return false;
    attempted_mask|=1u<<best;retries=1;begin_attempt(&profiles.profiles[best],best,now);return true;
}
static void schedule_offline_retry(int64_t now){
    cycle_exhausted=true;connect_deadline=0;
    status_text=profiles_dirty?"保存失败，网络不可用":"已保存，网络不可用";
    if(!manual_attempt&&profiles.count){offline_backoff_s=offline_backoff_s?offline_backoff_s*2:60;if(offline_backoff_s>300)offline_backoff_s=300;offline_retry_at=now+(int64_t)offline_backoff_s*1000000;}
}
static void html_ssid(char *out,size_t capacity,const char *in){
    size_t used=0;for(const unsigned char*p=(const unsigned char*)in;*p;p++){
        const char*escape=*p=='&'?"&amp;":*p=='<'?"&lt;":*p=='>'?"&gt;":*p=='"'?"&quot;":*p=='\''?"&#39;":NULL;
        size_t n=escape?strlen(escape):1;if(used+n>=capacity)break;
        if(escape)memcpy(out+used,escape,n);else out[used]=(char)*p;used+=n;
    }out[used]=0;
}
static esp_err_t get_page(httpd_req_t *req){
    char names[2][33]={{0}},escaped[2][193];
    taskENTER_CRITICAL(&pending_lock);
    for(unsigned i=0;i<profiles.count;i++)memcpy(names[i],profiles.profiles[i].ssid,sizeof(names[i]));
    taskEXIT_CRITICAL(&pending_lock);
    html_ssid(escaped[0],sizeof(escaped[0]),names[0]);html_ssid(escaped[1],sizeof(escaped[1]),names[1]);
    size_t capacity=sizeof(page)+sizeof(escaped);char*rendered=malloc(capacity);
    if(!rendered)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Page unavailable");
    snprintf(rendered,capacity,page,escaped[0],escaped[1]);
    httpd_resp_set_type(req,"text/html; charset=utf-8");esp_err_t err=httpd_resp_send(req,rendered,HTTPD_RESP_USE_STRLEN);free(rendered);return err;
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
    char body[640];
    if(req->content_len<=0||req->content_len>=sizeof(body))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid form size");
    size_t used=0;int64_t deadline=esp_timer_get_time()+5000000;
    while(used<req->content_len){
        /* Bound total form time, not merely individual socket recv calls. */
        if((xEventGroupGetBits(events)&CLOSING)||esp_timer_get_time()>=deadline)return ESP_FAIL;
        int n=httpd_req_recv(req,body+used,req->content_len-used);if(n<=0)return ESP_FAIL;used+=(size_t)n;
    }
    if((xEventGroupGetBits(events)&CLOSING)||esp_timer_get_time()>=deadline)return ESP_FAIL;
    body[used]=0;wifi_profile_t batch[2]={0};bool open[2]={false,false};
    for(char*token=strtok(body,"&");token;token=strtok(NULL,"&")){
        char*equal=strchr(token,'=');if(!equal)continue;*equal++=0;
        int index=-1;bool password=false;
        if(!strcmp(token,"ssid")||!strcmp(token,"ssid1"))index=0;
        else if(!strcmp(token,"ssid2"))index=1;
        else if(!strcmp(token,"password")||!strcmp(token,"password1")){index=0;password=true;}
        else if(!strcmp(token,"password2")){index=1;password=true;}
        else if(!strcmp(token,"open1")){open[0]=!strcmp(equal,"1");continue;}
        else if(!strcmp(token,"open2")){open[1]=!strcmp(equal,"1");continue;}
        if(index>=0&&!decode(password?batch[index].password:batch[index].ssid,password?sizeof(batch[index].password):sizeof(batch[index].ssid),equal)){
            memset(batch,0,sizeof(batch));memset(body,0,sizeof(body));return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid UTF-8 SSID or password size");
        }
    }
    unsigned count=0;
    for(unsigned i=0;i<2;i++){
        if(!batch[i].ssid[0]){if(batch[i].password[0]||open[i]){memset(batch,0,sizeof(batch));memset(body,0,sizeof(body));return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Network name required");}continue;}
        if(!profile_valid(&batch[i])){memset(batch,0,sizeof(batch));memset(body,0,sizeof(body));return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Check SSID (32 UTF-8 bytes) and WPA password length");}
        if(count!=i){batch[count]=batch[i];open[count]=open[i];}count++;
    }
    if(!count||(count==2&&!strcmp(batch[0].ssid,batch[1].ssid))){memset(batch,0,sizeof(batch));memset(body,0,sizeof(body));return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Enter one or two distinct network names");}
    if(xEventGroupGetBits(events)&CLOSING){memset(batch,0,sizeof(batch));memset(body,0,sizeof(body));return ESP_FAIL;}
    taskENTER_CRITICAL(&pending_lock);
    if(pending_valid){
        taskEXIT_CRITICAL(&pending_lock);memset(batch,0,sizeof(batch));memset(body,0,sizeof(body));
        httpd_resp_set_status(req,"503 Service Unavailable");httpd_resp_set_type(req,"text/plain; charset=utf-8");
        return httpd_resp_sendstr(req,"上一份配置正在保存，请稍后再提交。");
    }
    memcpy(pending_profile,batch,sizeof(batch));memcpy(pending_open,open,sizeof(open));pending_count=(uint8_t)count;pending_valid=true;status_text="保存中";taskEXIT_CRITICAL(&pending_lock);
    xEventGroupSetBits(events,REQUESTED|SUBMITTED);memset(batch,0,sizeof(batch));memset(body,0,sizeof(body));
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(req,"配置已提交，设备正在保存并连接。网络暂未开启也会保存，请查看设备屏幕状态。");
}

esp_err_t passport_wifi_start_provisioning(void) {
    if (!events || !lifecycle) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    if (passport_wifi_provisioning()) { xSemaphoreGive(lifecycle); return ESP_OK; }
    xEventGroupClearBits(events,SUBMITTED|CLOSE_REQUESTED|CLOSING|REQUESTED);
    taskENTER_CRITICAL(&pending_lock);memset(pending_profile,0,sizeof(pending_profile));pending_count=0;pending_valid=false;taskEXIT_CRITICAL(&pending_lock);
    connect_deadline=0;apply_pending=false;cycle_exhausted=true;offline_retry_at=0;
    xEventGroupClearBits(events,RECONFIGURING);
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
    /* Read legacy credentials before RAM storage; failover never rewrites the
     * driver's Flash credentials. Migrate once via the application poll. */
    wifi_config_t saved={0};err=esp_wifi_get_config(WIFI_IF_STA,&saved);wifi_profile_t legacy={0};
    if(err==ESP_OK){memcpy(legacy.ssid,saved.sta.ssid,32);memcpy(legacy.password,saved.sta.password,63);}memset(&saved,0,sizeof(saved));
    memset(&profiles,0,sizeof(profiles));profiles.magic=WIFI_PROFILES_MAGIC;
    if(!load_profiles()&&profile_valid(&legacy)){profiles.count=1;profiles.profiles[0]=legacy;profiles_dirty=true;}
    memset(&legacy,0,sizeof(legacy));ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    if(profiles.count){xEventGroupSetBits(events,CONFIGURED);manual_attempt=false;attempted_mask=0;advance_profile(esp_timer_get_time());return ESP_OK;}
    return passport_wifi_start_provisioning();
}
bool passport_wifi_connected(void) { return events && (xEventGroupGetBits(events)&CONNECTED); }
bool passport_wifi_provisioning(void) { return events && (xEventGroupGetBits(events)&PORTAL); }
const char *passport_wifi_ap_password(void) { return ap_password; }
unsigned passport_wifi_profile_count(void){
    unsigned count;taskENTER_CRITICAL(&pending_lock);count=profiles.count;taskEXIT_CRITICAL(&pending_lock);return count;
}
const char *passport_wifi_status_text(void){return status_text;}
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
static bool process_requested(int64_t now,bool connect){
    wifi_profile_t batch[2];bool requested,open[2];unsigned count;
    taskENTER_CRITICAL(&pending_lock);requested=pending_valid;count=pending_count;
    if(requested){memcpy(batch,pending_profile,sizeof(batch));memcpy(open,pending_open,sizeof(open));memset(pending_profile,0,sizeof(pending_profile));pending_count=0;pending_valid=false;}
    taskEXIT_CRITICAL(&pending_lock);
    if(requested){
        xEventGroupClearBits(events,REQUESTED);
        /* Resolve blank passwords from the old bank before replacing either
         * slot; a batch never loses the optional second profile. */
        for(unsigned i=0;i<count;i++){
            for(unsigned j=0;j<profiles.count;j++)if(!strcmp(batch[i].ssid,profiles.profiles[j].ssid)){
                batch[i].last_success=profiles.profiles[j].last_success;
                if(!open[i]&&!batch[i].password[0])memcpy(batch[i].password,profiles.profiles[j].password,sizeof(batch[i].password));
            }
            if(open[i])memset(batch[i].password,0,sizeof(batch[i].password));
        }
        taskENTER_CRITICAL(&pending_lock);
        if(count==2){profiles.count=2;profiles.profiles[0]=batch[0];profiles.profiles[1]=batch[1];}
        else{
            int found=-1;for(unsigned i=0;i<profiles.count;i++)if(!strcmp(profiles.profiles[i].ssid,batch[0].ssid))found=(int)i;
            if(found<0){if(profiles.count<2)found=profiles.count++;else found=profiles.profiles[0].last_success<=profiles.profiles[1].last_success?0:1;}
            profiles.profiles[found]=batch[0];
        }
        profiles.magic=WIFI_PROFILES_MAGIC;profiles_dirty=true;persist_retry_at=0;
        taskEXIT_CRITICAL(&pending_lock);
        persist_profiles(now);
        manual_attempt=false;was_connected=false;attempted_mask=0;offline_retry_at=0;offline_backoff_s=0;
        int first=-1;for(unsigned i=0;i<profiles.count;i++)if(!strcmp(profiles.profiles[i].ssid,batch[0].ssid))first=(int)i;
        if(connect&&first>=0){attempted_mask=1u<<first;retries=1;begin_attempt(&profiles.profiles[first],first,now);}
        status_text=profiles_dirty?"保存失败":connect?"已保存，连接中":"已保存";
        memset(batch,0,sizeof(batch));
    }
    return requested;
}
void passport_wifi_poll(void){
    if(!events||!lifecycle)return;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    int64_t now=esp_timer_get_time();EventBits_t bits=xEventGroupGetBits(events);
    if(bits&STOPPING){xSemaphoreGive(lifecycle);return;}
    process_requested(now,true);
    if(apply_pending&&now>=next_apply){
        wifi_ap_record_t ap;esp_err_t association=esp_wifi_sta_get_ap_info(&ap),err=ESP_ERR_WIFI_STATE;
        if(association==ESP_ERR_WIFI_NOT_CONNECT){
            wifi_config_t config={0};memcpy(config.sta.ssid,active_profile.ssid,strlen(active_profile.ssid));memcpy(config.sta.password,active_profile.password,strlen(active_profile.password));config.sta.pmf_cfg.capable=true;
            err=esp_wifi_set_config(WIFI_IF_STA,&config);memset(&config,0,sizeof(config));
        }else if(association!=ESP_OK)err=association;
        if(err==ESP_ERR_WIFI_STATE&&now<apply_deadline)next_apply=now+INT64_C(100000);
        else{
            apply_pending=false;xEventGroupClearBits(events,RECONFIGURING|DISCONNECTED|SAVE_SUCCESS);
            if(err==ESP_OK){xEventGroupSetBits(events,CONFIGURED);err=esp_wifi_connect();}
            if(err==ESP_OK)connect_deadline=now+CONNECT_TIMEOUT_US;
            else{connect_deadline=0;xEventGroupSetBits(events,DISCONNECTED);}
        }
    }
    bits=xEventGroupGetBits(events);
    if(bits&SAVE_SUCCESS){
        wifi_ap_record_t ap;xEventGroupClearBits(events,SAVE_SUCCESS);
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK&&!memcmp(ap.ssid,active_profile.ssid,strlen(active_profile.ssid))&&ap.ssid[strlen(active_profile.ssid)]==0){
            connect_deadline=0;cycle_exhausted=false;offline_retry_at=0;offline_backoff_s=0;
            if(!was_connected)remember_success();
            was_connected=true;status_text=profiles_dirty?"已连接，保存待重试":"已连接";xEventGroupClearBits(events,DISCONNECTED);xEventGroupSetBits(events,CONNECTED|((bits&(PORTAL|SUBMITTED))==(PORTAL|SUBMITTED)?CLOSE_REQUESTED:0));
        }
    }
    bits=xEventGroupGetBits(events);
    if(!(bits&(CONNECTED|RECONFIGURING))&&!apply_pending&&!cycle_exhausted&&((bits&DISCONNECTED)||(connect_deadline&&now>=connect_deadline))){
        xEventGroupClearBits(events,DISCONNECTED);
        if(was_connected){was_connected=false;manual_attempt=false;attempted_mask=active_index>=0?1u<<active_index:0;retries=0;}
        if(retries<PROFILE_RETRIES){retries++;begin_attempt(&active_profile,active_index,now);}
        else if(!advance_profile(now))schedule_offline_retry(now);
    }
    if(cycle_exhausted&&offline_retry_at&&now>=offline_retry_at&&!(bits&(CONNECTED|PORTAL|RECONFIGURING))){
        offline_retry_at=0;manual_attempt=false;attempted_mask=0;advance_profile(now);
    }
    persist_profiles(now);
    if(passport_wifi_connected()&&!profiles_dirty)status_text="已连接";
    if((xEventGroupGetBits(events)&(PORTAL|CLOSE_REQUESTED))==(PORTAL|CLOSE_REQUESTED))end_provisioning_locked();
    xSemaphoreGive(lifecycle);
}
esp_err_t passport_wifi_end_provisioning(void){
    if(!events||!lifecycle)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lifecycle,portMAX_DELAY);esp_err_t err=end_provisioning_locked();
    if(err==ESP_OK){
        xEventGroupClearBits(events,SAVE_SUCCESS|RECONFIGURING);
        apply_pending=false;connect_deadline=0;offline_retry_at=0;cycle_exhausted=!profiles.count;
        /* httpd_stop drained the accepted handler: never discard its queued
         * batch merely because the user immediately exited management. */
        bool queued=process_requested(esp_timer_get_time(),true);
        if(!queued&&!passport_wifi_connected()&&profiles.count){manual_attempt=false;attempted_mask=0;advance_profile(esp_timer_get_time());}
    }
    xSemaphoreGive(lifecycle);return err;
}
esp_err_t passport_wifi_stop(void){
    if(!events||!lifecycle)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    xEventGroupSetBits(events,STOPPING|CLOSING);xEventGroupClearBits(events,SAVE_SUCCESS|RECONFIGURING);
    apply_pending=false;connect_deadline=0;cycle_exhausted=true;offline_retry_at=0;
    esp_err_t err=ESP_OK;
    if(server){err=httpd_stop(server);if(err==ESP_OK)server=NULL;}
    if(err==ESP_OK){
        process_requested(esp_timer_get_time(),false);
        persist_profiles(esp_timer_get_time());
        if(profiles_dirty){err=ESP_FAIL;status_text="保存失败，请重试";}
    }
    if(err==ESP_OK)err=esp_wifi_stop();
    if(err==ESP_OK){xEventGroupClearBits(events,CONNECTED|PORTAL|SUBMITTED|CLOSE_REQUESTED|REQUESTED);memset(ap_password,0,sizeof(ap_password));}
    xEventGroupClearBits(events,CLOSING);
    if(err!=ESP_OK)xEventGroupClearBits(events,STOPPING);
    xSemaphoreGive(lifecycle);return err;
}
