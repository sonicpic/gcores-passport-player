/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "passport_audio.h"
#include "passport_fm.h"
#include "passport_catalog.h"
#include "passport_history.h"
#include "passport_logic.h"
#include "passport_ui.h"
#include "passport_wifi.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } input_t;
typedef enum { JOB_REFRESH, JOB_CHAPTERS, JOB_SAVE, JOB_PREPARE_PLAY, JOB_START_FM } job_kind_t;
typedef struct { job_kind_t kind; int channel,episode; uint32_t position; bool completed; } job_t;
typedef struct { job_kind_t kind; esp_err_t error; bool ready,restart,paused,restore_seek; int channel,episode; uint32_t position,restart_position; } result_t;
static QueueHandle_t inputs,jobs,results;
static TaskHandle_t catalog_worker;
static key_guard_t wake_guard;
static passport_page_t page=PASSPORT_PAGE_LIBRARY;
static passport_page_t return_page[PASSPORT_PAGE_TIMELINE_TEXT+1];
static int channel,selected,menu_selected,playing_channel=-1,current_channel;
static passport_episode_t current;
static uint32_t initial_resume,history_position;
static int volume=50,battery=-1,sleep_minutes;
static int64_t last_input,last_save,sleep_deadline,portal_started;
static bool busy,dark,dirty=true,audio_ok;
static bool refreshing_catalog;
static int64_t catalog_last_success,catalog_attempted_day;
static bool ui_activity_pending;
static int brightness=70,screen_timeout_index=2;
static const uint32_t screen_timeouts[]={15,30,60,120,300,0};
static bool settings_dirty,scrub_was_paused;
static passport_screen_guard_t screen_guard;
static bsp_btn_t scrub_key;
static passport_scrub_t scrub;
static struct { bool active,paused; uint32_t target,request_id; } seek_feedback;
static bool sleep_requested;
static bool portal_close_failed;
static bool prepared_play,current_fm;
static int current_episode=-1;
static char chapter_line[220],next_chapter_line[385],fm_title[385],details[5][64];
static int64_t scrub_released_at;
static char notice[240]="",rows[5][385];
static const char *TAG="passport";
static void input_callback(bsp_btn_t key,bsp_btn_ev_t event,void *user) {
    (void)user;
    if (key_guard_consume(&wake_guard,event==BSP_BTN_RELEASE,esp_timer_get_time())) return;
    input_t input={key,event}; xQueueSend(inputs,&input,0);
}
static bool active(const passport_audio_status_t *s) { return s->state==PASSPORT_AUDIO_PLAYING || s->state==PASSPORT_AUDIO_BUFFERING; }
static void enter_page(passport_page_t next) { return_page[next]=page; page=next; dirty=true; if (next==PASSPORT_PAGE_WIFI) portal_close_failed=false; }
static bool have_current(void) { return current.url[0]!=0; }
static bool can_return_player(void) { return (current_fm || playing_channel>=0) && have_current(); }
static void catalog_refresh_succeeded(void) {
    int64_t stamp=time(NULL);
    if (!passport_catalog_day(stamp)) return;
    nvs_handle_t h;
    if (nvs_open("gpp_settings",NVS_READWRITE,&h)==ESP_OK) {
        if (nvs_set_i64(h,"catalog_updated",stamp)==ESP_OK && nvs_commit(h)==ESP_OK)
            catalog_last_success=stamp;
        nvs_close(h);
    }
}
static void screen_set_dark(bool value) {
    dark=value; ui_activity_pending=true; dirty=true;
    bsp_display_backlight(value?0:brightness);
}
/* Only a fresh audio snapshot can complete a committed local preview.
 * 已提交的本地目标只能由下一轮真实音频快照确认，不能用松手前的旧状态清除。 */
static void seek_feedback_observe(const passport_audio_status_t *audio) {
    if (busy) return;
    if (seek_feedback.active &&
        (audio->state==PASSPORT_AUDIO_ERROR || audio->state==PASSPORT_AUDIO_IDLE || audio->state==PASSPORT_AUDIO_ENDED ||
         (!audio->seeking && audio->state==PASSPORT_AUDIO_PLAYING && audio->seek_complete_id==seek_feedback.request_id))) {
        seek_feedback.active=false; dirty=true;
    }
}
static esp_err_t seek_with_feedback(uint32_t target,bool resume) {
    esp_err_t e=passport_audio_seek(target);
    if (e!=ESP_OK) return e;
    seek_feedback.active=true; seek_feedback.target=target; seek_feedback.paused=!resume;
    passport_audio_status_t accepted; passport_audio_get_status(&accepted);
    seek_feedback.request_id=accepted.seek_request_id;
    history_position=target;
    if (resume) {
        e=passport_audio_pause(false);
        if (e!=ESP_OK) seek_feedback.paused=true;
    }
    return e;
}
static void seek_feedback_view(passport_view_t *view) {
    if (scrub.active) { view->position_seconds=scrub.target; view->playback_state="SEEK"; }
    else if (seek_feedback.active) {
        view->position_seconds=seek_feedback.target;
        view->playback_state=seek_feedback.paused?"PAUSED":"SEEK";
    }
}
static esp_err_t stop_wait(void) {
    if (!audio_ok) return ESP_OK;
    esp_err_t err=passport_audio_stop(); if (err!=ESP_OK) return err;
    for (int i=0;i<150;i++) { vTaskDelay(pdMS_TO_TICKS(100)); if (passport_audio_is_quiescent()) return ESP_OK; }
    return ESP_ERR_TIMEOUT;
}
static esp_err_t save_playback(uint32_t position,bool completed) {
    if (current_fm || playing_channel<0 || !current.guid[0]) return ESP_OK;
    passport_resume_t snapshot={.channel=playing_channel,.episode=UINT8_MAX,.position_seconds=position,
        .total_seconds=current.duration_seconds,.completed=completed};
    snprintf(snapshot.guid,sizeof(snapshot.guid),"%s",current.guid);
    snprintf(snapshot.url,sizeof(snapshot.url),"%s",current.url);
    snprintf(snapshot.title,sizeof(snapshot.title),"%s",current.title);
    if (snapshot.total_seconds && snapshot.position_seconds>snapshot.total_seconds)
        snapshot.position_seconds=snapshot.total_seconds;
    esp_err_t per_episode=passport_history_save(snapshot.channel,snapshot.guid,snapshot.position_seconds,
        snapshot.total_seconds,snapshot.completed);
    esp_err_t latest=passport_catalog_save_playback(&snapshot);
    return per_episode!=ESP_OK?per_episode:latest;
}
static void worker(void *arg) {
    (void)arg; job_t job;
    if (xQueueReceive(jobs,&job,portMAX_DELAY)==pdTRUE) {
        result_t result={.kind=job.kind,.error=ESP_OK,.channel=job.channel,.episode=job.episode,.position=job.position};
        if (job.kind==JOB_SAVE) result.error=save_playback(job.position,job.completed);
        else {
            passport_audio_status_t before; passport_audio_get_status(&before);
            result.restart=job.kind!=JOB_PREPARE_PLAY && job.kind!=JOB_START_FM && (active(&before)||before.state==PASSPORT_AUDIO_PAUSED);
            result.paused=seek_feedback.active?seek_feedback.paused:before.state==PASSPORT_AUDIO_PAUSED;
            result.restore_seek=seek_feedback.active || before.state==PASSPORT_AUDIO_BUFFERING;
            result.restart_position=seek_feedback.active?seek_feedback.target:
                before.state==PASSPORT_AUDIO_BUFFERING?history_position:before.position_seconds;
            passport_episode_t selected_snapshot;
            bool preserve_selection=job.kind==JOB_REFRESH && passport_catalog_get_episode(job.channel,job.episode,&selected_snapshot);
            if (job.kind==JOB_PREPARE_PLAY || job.kind==JOB_START_FM) save_playback(seek_feedback.active?seek_feedback.target:
                before.state==PASSPORT_AUDIO_BUFFERING?history_position:before.position_seconds,before.state==PASSPORT_AUDIO_ENDED);
            result.error=stop_wait();
            result.ready=result.error==ESP_OK;
            if (result.error==ESP_OK) {
                if (job.kind==JOB_REFRESH) {
                    /* Each channel commits independently. Continue after an error,
                     * retaining that channel's previous transactional snapshot. */
                    for (int c=0;c<PASSPORT_CHANNEL_COUNT;c++) {
                        esp_err_t refreshed=passport_catalog_refresh(c);
                        if (refreshed!=ESP_OK && result.error==ESP_OK) result.error=refreshed;
                    }
                } else if (job.kind==JOB_START_FM) passport_catalog_release_timeline();
                else {
                    result.error=passport_catalog_fetch_chapters(job.channel,job.episode);
                    if (result.error!=ESP_OK && result.error!=ESP_ERR_NOT_FOUND) {
                        ESP_LOGW(TAG,"Timeline prepare retry error=%d",(int)result.error);
                        vTaskDelay(pdMS_TO_TICKS(200));
                        result.error=passport_catalog_fetch_chapters(job.channel,job.episode);
                    }
                }
            }
            if (job.kind==JOB_REFRESH && result.ready && preserve_selection) {
                int restored=passport_catalog_find_episode_by_identity(job.channel,selected_snapshot.guid,selected_snapshot.url);
                result.episode=restored>=0?restored:0;
            }
            /* The app restores audio only after deleting this suspended task,
             * returning its full stack before decoder admission. */
        }
        ESP_LOGI(TAG,"Catalog worker stack_free=%lu",(unsigned long)uxTaskGetStackHighWaterMark(NULL));
        xQueueSend(results,&result,portMAX_DELAY);
    }
    /* No self-delete: external deletion of a suspended task frees RAM now,
     * without waiting for the idle task to reclaim the catalogue stack. */
    for (;;) vTaskSuspend(NULL);
}
static bool begin(job_kind_t kind,int c,int e,uint32_t position,bool completed) {
    if (busy || catalog_worker) return false;
    if (kind!=JOB_SAVE && passport_wifi_provisioning()) { enter_page(PASSPORT_PAGE_WIFI); return false; }
    job_t job={kind,c,e,position,completed};
    busy=true;
    if (xQueueSend(jobs,&job,0)!=pdTRUE) { busy=false; return false; }
    if (xTaskCreate(worker,"catalog",10240,NULL,3,&catalog_worker)!=pdPASS) {
        /* Creation failed; no task owns the queued job. Permit a safe retry. */
        job_t discarded; xQueueReceive(jobs,&discarded,0);
        catalog_worker=NULL; busy=false; dirty=true;
        snprintf(notice,sizeof(notice),"目录任务内存不足，请重试");
        return false;
    }
    if (kind==JOB_REFRESH) initial_resume=0;
    refreshing_catalog=kind==JOB_REFRESH;
    dirty=true;
    if (kind!=JOB_SAVE) snprintf(notice,sizeof(notice),"%s",refreshing_catalog?"正在更新节目库，请稍后……":"加载中");
    return true;
}
static void auto_refresh_catalog(int64_t utc_seconds,const passport_audio_status_t *audio) {
    if (passport_wifi_connected() && !passport_wifi_provisioning() && !busy &&
        !active(audio) && audio->state!=PASSPORT_AUDIO_PAUSED && !scrub.active && !seek_feedback.active &&
        passport_catalog_update_due(utc_seconds,catalog_last_success,catalog_attempted_day)) {
        /* Mark the day even if task admission fails: never retry in every UI tick. */
        catalog_attempted_day=passport_catalog_day(utc_seconds);
        begin(JOB_REFRESH,channel,selected,0,false);
    }
}
static void settings_save(void) {
    nvs_handle_t h;
    if (nvs_open("gpp_settings",NVS_READWRITE,&h)==ESP_OK) {
        if (nvs_set_u8(h,"volume",volume)==ESP_OK && nvs_set_u8(h,"brightness",brightness)==ESP_OK &&
            nvs_set_u8(h,"screen_timeout",screen_timeout_index)==ESP_OK && nvs_commit(h)==ESP_OK) settings_dirty=false;
        nvs_close(h);
    }
}
static void render(const passport_audio_status_t *a) {
    passport_view_t v={.page=page,.selected=selected,.channel_index=channel,.channel=passport_catalog_channel_name(channel),.title=current.title,
        .fm=current_fm,.playing=a->state==PASSPORT_AUDIO_PLAYING,.updating_catalog=refreshing_catalog,.wifi_connected=passport_wifi_connected(),.position_seconds=a->position_seconds,
        .duration_seconds=current.duration_seconds?current.duration_seconds:a->duration_seconds,.volume=volume,.battery_percent=battery,.status=notice,.hint="长按确定返回\n双击确定息屏"};
    const char *states[]={"READY","BUFFER","ON AIR","PAUSED","ENDED","ERROR"};
    v.playback_state=(unsigned)a->state<6?states[a->state]:"ERROR";
    seek_feedback_view(&v);
    if (page==PASSPORT_PAGE_PLAYER && have_current()) { v.channel_index=current_channel; v.channel=current_fm?"机核 FM":passport_catalog_channel_name(current_channel); }
    if (current_fm && page==PASSPORT_PAGE_PLAYER) {
        passport_fm_get_titles(fm_title,sizeof(fm_title),next_chapter_line,sizeof(next_chapter_line));
        v.title=fm_title[0]?fm_title:"机核 FM";
        v.next_chapter=next_chapter_line[0]?next_chapter_line:"节目信息暂不可用";
    }
    passport_chapter_t current_chapter; size_t chapter_index=0;
    if (!current_fm && have_current() && passport_catalog_current_chapter_by_identity(current_channel,current.guid,current.url,v.position_seconds,&current_chapter,&chapter_index)) {
        snprintf(chapter_line,sizeof(chapter_line),"%02lu:%02lu %s",(unsigned long)(current_chapter.start_seconds/60),
            (unsigned long)(current_chapter.start_seconds%60),current_chapter.title);
        v.current_chapter=chapter_line;
        passport_chapter_t next_chapter;
        if (passport_catalog_get_chapter_by_identity(current_channel,current.guid,current.url,chapter_index+1,&next_chapter)) {
            snprintf(next_chapter_line,sizeof(next_chapter_line),"%02lu:%02lu %s",(unsigned long)(next_chapter.start_seconds/60),
                (unsigned long)(next_chapter.start_seconds%60),next_chapter.title);
            v.next_chapter=next_chapter_line;
        }
    }
    // A real timeline whose first chapter starts after zero is not unavailable.
    if (!current_fm && have_current() && !v.current_chapter &&
        passport_catalog_get_chapter_by_identity(current_channel,current.guid,current.url,0,&current_chapter)) {
        snprintf(chapter_line,sizeof(chapter_line),"%02lu:%02lu %s",
                 (unsigned long)(current_chapter.start_seconds/60),
                 (unsigned long)(current_chapter.start_seconds%60),current_chapter.title);
        v.current_chapter=chapter_line;
        passport_chapter_t next_chapter;
        if (passport_catalog_get_chapter_by_identity(current_channel,current.guid,current.url,1,&next_chapter)) {
            snprintf(next_chapter_line,sizeof(next_chapter_line),"%02lu:%02lu %s",
                     (unsigned long)(next_chapter.start_seconds/60),
                     (unsigned long)(next_chapter.start_seconds%60),next_chapter.title);
            v.next_chapter=next_chapter_line;
        }
    }
    if (page==PASSPORT_PAGE_LIBRARY) {
        int count=passport_catalog_episode_count(channel),start=selected/4*4;
        v.selected=selected-start; v.item_count=count>start?(count-start>4?4:count-start):0;
        passport_episode_t ep;
        for (int i=0;i<v.item_count;i++) {
            rows[i][0]=details[i][0]=0;
            if (passport_catalog_get_episode(channel,start+i,&ep)) {
                snprintf(rows[i],sizeof(rows[i]),"%s",ep.title);
                if (ep.duration_seconds) snprintf(details[i],sizeof(details[i]),"%lu:%02lu",(unsigned long)(ep.duration_seconds/60),(unsigned long)(ep.duration_seconds%60));
                bool same=have_current() && channel==current_channel && !strcmp(current.guid,ep.guid) && !strcmp(current.url,ep.url);
                if (same && (active(a) || a->state==PASSPORT_AUDIO_PAUSED)) v.row_playback[i]=a->state==PASSPORT_AUDIO_PAUSED?2:1;
            }
            v.titles[i]=rows[i]; v.details[i]=details[i];
        }
        v.hint="短按上下选择 OK打开\n长按上左下右 OK设置";
    } else if (page==PASSPORT_PAGE_SETTINGS) {
        bool session=can_return_player();
        menu_selected=passport_clamp_selection(menu_selected,0,9+session);
        int start=menu_selected/5*5;
        const char *items[8]={"更新播客列表","Wi-Fi 配网",NULL,NULL,NULL,"息屏（继续播放）","停止并深睡","按键帮助"};
        snprintf(rows[0],sizeof(rows[0]),"亮度：%d%%",brightness); items[2]=rows[0];
        if (screen_timeouts[screen_timeout_index]) snprintf(rows[1],sizeof(rows[1]),"自动息屏：%lu 秒",(unsigned long)screen_timeouts[screen_timeout_index]);
        else snprintf(rows[1],sizeof(rows[1]),"自动息屏：常亮");
        items[3]=rows[1];
        if (sleep_minutes) snprintf(rows[2],sizeof(rows[2]),"定时停止：%d 分钟",sleep_minutes);
        else snprintf(rows[2],sizeof(rows[2]),"定时停止：关闭");
        items[4]=rows[2];
        v.selected=menu_selected-start; v.item_count=9+session-start>5?5:9+session-start;
        for (int i=0;i<v.item_count;i++) {
            int item=start+i;
            v.row_highlight[i]=item<=session;
            v.row_icon[i]=session && item==0?1:item==session?2:0;
            v.titles[i]=session && item==0?"回到当前播放":item==session?"机核 FM":items[item-session-1];
        }
        v.hint="单击上下选 确定切换\n长按确定返回 双击息屏";
    } else if (page==PASSPORT_PAGE_WIFI) {
        if (passport_wifi_provisioning()) snprintf(notice,sizeof(notice),"%s\n手机连接热点\nGcores-Passport\n密码：%s\n浏览器打开\nhttp://192.168.4.1\n使用 2.4 GHz 网络%s",passport_wifi_status_text(),passport_wifi_ap_password(),portal_close_failed?"\n关闭失败，按确定重试":"");
        else { portal_close_failed=false; snprintf(notice,sizeof(notice),"%s",passport_wifi_connected()?"网络已连接，热点已关闭\n按确定返回上页":passport_wifi_status_text()); }
        v.hint="单击确定退出并关热点\n长按下配网 确定返回";
    } else if (page==PASSPORT_PAGE_HELP) {
        v.status="首页：上下选节目，OK播放\n长按上下切频道，长按OK设置\n播放：上下音量，OK播放暂停\n长按上下调进度，松手定位\n设置：上下选择，OK调整\n播放/设置：长按OK回首页\n双击OK息屏，任意键唤醒\n深睡停止播放，保存进度";
        v.hint="长按确定返回上页\n双击确定息屏，播放继续";
    } else if (page==PASSPORT_PAGE_PLAYER) v.hint=scrub.active?"按住上退下进 松开定位\n先移动进度，松开后读取":seek_feedback.active?(seek_feedback.paused?"已选定位目标\n单击确定后定位":"正在定位…\n目标进度保留，请稍候"):"单击上加下减 确定暂停\n长按上退下进 OK首页";
    if (refreshing_catalog) v.status="正在更新节目库，请稍后……";
    if (bsp_lvgl_lock(200)) { passport_ui_update(&v); bsp_lvgl_unlock(); }
}
static void sleep_now(void) {
    if (busy || bsp_button_read_mv()<BSP_BTN_MV_RELEASED_MIN) return;
    passport_audio_status_t before; passport_audio_get_status(&before);
    save_playback(seek_feedback.active?seek_feedback.target:
        before.state==PASSPORT_AUDIO_BUFFERING?history_position:before.position_seconds,before.state==PASSPORT_AUDIO_ENDED);
    if (stop_wait()!=ESP_OK) { snprintf(notice,sizeof(notice),"音频未停止，请重试"); dirty=true; return; }
    settings_save();
    if (passport_wifi_stop()!=ESP_OK) { snprintf(notice,sizeof(notice),"网络未停止，请重试"); dirty=true; return; }
    int level=0;
    /* All calls below are terminal operations; a failed prerequisite reboots. */
    if (bsp_button_prepare_deep_sleep(&level)!=ESP_OK || level!=1 ||
        esp_deep_sleep_enable_gpio_wakeup(1ULL<<BSP_BTN_GPIO,ESP_GPIO_WAKEUP_GPIO_LOW)!=ESP_OK ||
        bsp_battery_sleep()!=ESP_OK || bsp_audio_sleep()!=ESP_OK || bsp_audio_prepare_deep_sleep()!=ESP_OK ||
        bsp_i2c_prepare_deep_sleep()!=ESP_OK || !bsp_lvgl_lock(1000) || bsp_display_prepare_deep_sleep()!=ESP_OK) {
        ESP_LOGE(TAG,"Sleep prerequisite failed; restart to restore peripherals"); esp_restart();
    }
    esp_deep_sleep_start(); esp_restart();
}
static void play(uint32_t position,bool explicit_seek) {
    if (!audio_ok) { snprintf(notice,sizeof(notice),"音频初始化失败"); return; }
    passport_episode_t next;
    if (!passport_catalog_get_episode(channel,selected,&next)) return;
    passport_audio_status_t running; passport_audio_get_status(&running);
    if (have_current() && channel==current_channel && !strcmp(current.url,next.url) && !strcmp(current.guid,next.guid) &&
        (active(&running) || running.state==PASSPORT_AUDIO_PAUSED)) {
        if (explicit_seek) {
            esp_err_t e=seek_with_feedback(position,true);
            if (e!=ESP_OK) snprintf(notice,sizeof(notice),"定位失败：%s",esp_err_to_name(e));
        }
        page=PASSPORT_PAGE_PLAYER;
        if (!prepared_play && !passport_catalog_timeline_ready(channel,next.guid,next.url) &&
            passport_wifi_connected() && !passport_wifi_provisioning())
            begin(JOB_CHAPTERS,channel,selected,running.position_seconds,false);
        return;
    }
    if (!passport_wifi_connected() || passport_wifi_provisioning()) { enter_page(PASSPORT_PAGE_WIFI); return; }
    if (!prepared_play) {
        bool replay_completed=have_current() && channel==current_channel && !strcmp(current.guid,next.guid) &&
            running.state==PASSPORT_AUDIO_ENDED;
        passport_history_progress_t saved;
        if (!explicit_seek && !position && !replay_completed && passport_history_lookup(channel,next.guid,&saved))
            position=saved.completed?0:saved.position_seconds;
        if (next.duration_seconds && position>=next.duration_seconds) position=0;
        begin(JOB_PREPARE_PLAY,channel,selected,position,false); return;
    }
    esp_err_t e=passport_audio_play(next.url,position,next.duration_seconds);
    if (e==ESP_OK) {
        esp_err_t pinned=passport_catalog_set_playback(channel,&next);
        if (pinned!=ESP_OK) ESP_LOGW(TAG,"Playback metadata pin failed: %s",esp_err_to_name(pinned));
        seek_feedback.active=false; current_fm=false; current=next; current_channel=playing_channel=channel; current_episode=selected; history_position=position; last_save=esp_timer_get_time(); page=PASSPORT_PAGE_PLAYER; snprintf(notice,sizeof(notice),"加载中");
    }
    else snprintf(notice,sizeof(notice),"播放失败：%s",esp_err_to_name(e));
}
static void handle(input_t input,const passport_audio_status_t *audio) {
    if ((unsigned)input.key>=3) return;
    last_input=esp_timer_get_time(); dirty=true;
    bool consume=passport_screen_guard_consume(&screen_guard,input.key,input.event==BSP_BTN_PRESS,input.event==BSP_BTN_RELEASE,dark,last_input);
    if (input.event==BSP_BTN_PRESS) {
        if (dark) screen_set_dark(false);
        return;
    }
    if (consume) return;
    if (input.event==BSP_BTN_RELEASE) {
        if (scrub.active && input.key==scrub_key) {
            uint32_t target;
            if (passport_scrub_commit(&scrub,&target)) {
                esp_err_t e=seek_with_feedback(target,!scrub_was_paused);
                scrub_released_at=0;
                snprintf(notice,sizeof(notice),e==ESP_OK?"正在定位，远距离需要读取…":"定位失败：%s",esp_err_to_name(e));
            }
        }
        return;
    }
    if (input.event!=BSP_BTN_CLICK && input.event!=BSP_BTN_DOUBLE && input.event!=BSP_BTN_LONG) return;
    if (dark) { screen_set_dark(false); return; }
    bool held=input.event==BSP_BTN_LONG; int dir=input.key==BSP_BTN_UP?-1:1;
    if (scrub.active) return;
    if (input.event==BSP_BTN_DOUBLE && input.key==BSP_BTN_OK) { screen_set_dark(true); return; }
    if (held && input.key==BSP_BTN_OK) {
        if (page==PASSPORT_PAGE_WIFI && passport_wifi_provisioning() && passport_wifi_end_provisioning()!=ESP_OK) { portal_close_failed=true; snprintf(notice,sizeof(notice),"关闭热点失败，请重试"); return; }
        if (page==PASSPORT_PAGE_LIBRARY) { enter_page(PASSPORT_PAGE_SETTINGS); menu_selected=0; }
        else if (page==PASSPORT_PAGE_PLAYER || page==PASSPORT_PAGE_SETTINGS) page=PASSPORT_PAGE_LIBRARY;
        else page=return_page[page];
        if (settings_dirty) settings_save();
        return;
    }
    if (busy) return;
    if (page==PASSPORT_PAGE_LIBRARY) {
        if (held && input.key!=BSP_BTN_OK) {
            /* Browse cached tabs without stopping the current audio stream. */
            channel=(channel+dir+PASSPORT_CHANNEL_COUNT)%PASSPORT_CHANNEL_COUNT;
            selected=0; initial_resume=0;
            snprintf(notice,sizeof(notice),passport_catalog_episode_count(channel)?"%s":"暂无节目，请在设置更新列表",passport_catalog_channel_name(channel));
        }
        else if (!held && input.key==BSP_BTN_OK) { play(initial_resume,false); initial_resume=0; }
        else if (!held) { selected=passport_clamp_selection(selected,dir,passport_catalog_episode_count(channel)); initial_resume=0; }
    } else if (page==PASSPORT_PAGE_PLAYER) {
        if (!held && input.key==BSP_BTN_OK) {
            if (audio->state==PASSPORT_AUDIO_IDLE || audio->state==PASSPORT_AUDIO_ERROR || audio->state==PASSPORT_AUDIO_ENDED) {
                if (!passport_wifi_connected() || passport_wifi_provisioning()) enter_page(PASSPORT_PAGE_WIFI);
                else if (current_fm) { begin(JOB_START_FM,channel,selected,0,false); }
                else if (current.url[0]) {
                    int index=passport_catalog_find_episode_by_identity(current_channel,current.guid,current.url);
                    if (index>=0) { channel=current_channel; selected=index; play(audio->state==PASSPORT_AUDIO_ENDED?0:history_position,false); }
                    else if (passport_audio_play(current.url,audio->state==PASSPORT_AUDIO_ENDED?0:history_position,current.duration_seconds)==ESP_OK) { playing_channel=current_channel; last_save=esp_timer_get_time(); }
                }
            } else {
                bool pause=seek_feedback.active? !seek_feedback.paused : audio->state!=PASSPORT_AUDIO_PAUSED;
                if (passport_audio_pause(pause)==ESP_OK && seek_feedback.active) seek_feedback.paused=pause;
            }
        }
        else if (held && !current_fm && (active(audio) || audio->state==PASSPORT_AUDIO_PAUSED)) {
            scrub_key=input.key; scrub_was_paused=seek_feedback.active?seek_feedback.paused:audio->state==PASSPORT_AUDIO_PAUSED;
            if (passport_audio_pause(true)==ESP_OK) {
                scrub_released_at=0;
                passport_scrub_begin(&scrub,seek_feedback.active?seek_feedback.target:audio->position_seconds,
                    current.duration_seconds?current.duration_seconds:audio->duration_seconds,dir,last_input/1000);
            }
        }
        else if (!held) { volume=passport_clamp_selection(volume,-dir*(input.event==BSP_BTN_DOUBLE?10:5),101); passport_audio_set_volume(volume); settings_dirty=true; }
    } else if (page==PASSPORT_PAGE_SETTINGS) {
        bool session=can_return_player();
        int item=menu_selected-session-1;
        if (!held && input.key==BSP_BTN_OK) {
            if (session && menu_selected==0) enter_page(PASSPORT_PAGE_PLAYER);
            else if (menu_selected==session) { if (passport_wifi_connected()) { enter_page(PASSPORT_PAGE_PLAYER); begin(JOB_START_FM,channel,selected,0,false); } else enter_page(PASSPORT_PAGE_WIFI); }
            else if (item==0) { if (passport_wifi_connected()) begin(JOB_REFRESH,channel,selected,0,false); else enter_page(PASSPORT_PAGE_WIFI); }
            else if (item==1) { if (stop_wait()==ESP_OK && passport_wifi_start_provisioning()==ESP_OK) { portal_started=esp_timer_get_time(); enter_page(PASSPORT_PAGE_WIFI); } }
            else if (item==2) { brightness=brightness>=100?20:brightness==60?70:brightness==70?80:brightness+20; settings_dirty=true; }
            else if (item==3) { screen_timeout_index=(screen_timeout_index+1)%6; settings_dirty=true; }
            else if (item==4) { sleep_minutes=(sleep_minutes+15)%75; sleep_deadline=sleep_minutes?esp_timer_get_time()+(int64_t)sleep_minutes*60000000:0; }
            else if (item==5) screen_set_dark(true);
            else if (item==6) { sleep_requested=true; snprintf(notice,sizeof(notice),"松开按键后停止播放并深睡"); }
            else if (item==7) enter_page(PASSPORT_PAGE_HELP);
        } else if (!held) menu_selected=passport_clamp_selection(menu_selected,dir,9+session);
    } else if (page==PASSPORT_PAGE_WIFI) {
        if (held && input.key==BSP_BTN_DOWN) { if (stop_wait()==ESP_OK && passport_wifi_start_provisioning()==ESP_OK) portal_started=esp_timer_get_time(); }
        else if (!held && input.key==BSP_BTN_OK) {
            if (!passport_wifi_provisioning() || passport_wifi_end_provisioning()==ESP_OK) page=return_page[PASSPORT_PAGE_WIFI];
            else { portal_close_failed=true; snprintf(notice,sizeof(notice),"关闭热点失败，请重试"); }
        }
    }
}
static bool finish_catalog_result(result_t *result) {
    /* Queue delivery can preempt the lower-priority worker before it suspends.
     * Keep busy and retry next app tick; never delete a running task. */
    if (!catalog_worker || eTaskGetState(catalog_worker)!=eSuspended) return false;
    vTaskDelete(catalog_worker); catalog_worker=NULL;
    /* Catalog freshness is independent of whether restoring audio succeeds. */
    if (result->kind==JOB_REFRESH && result->error==ESP_OK) catalog_refresh_succeeded();
    if (result->restart && result->ready && current.url[0]) {
        esp_err_t e=current_fm?passport_audio_fm():passport_audio_play(current.url,result->restart_position,current.duration_seconds);
        if (e==ESP_OK && result->paused) e=passport_audio_pause(true);
        if (e==ESP_OK) {
            history_position=result->restart_position;
            seek_feedback.active=!current_fm && result->restore_seek;
            seek_feedback.paused=result->paused;
            seek_feedback.target=result->restart_position;
            passport_audio_status_t accepted; passport_audio_get_status(&accepted);
            seek_feedback.request_id=accepted.seek_request_id;
        }
        if (result->error==ESP_OK) result->error=e;
    }
    refreshing_catalog=false;
    busy=false; dirty=true;
    ESP_LOGI(TAG,"Catalog job=%u result=%s heap=%lu min=%lu largest=%lu app_stack_free=%lu",(unsigned)result->kind,esp_err_to_name(result->error),
        (unsigned long)esp_get_free_heap_size(),(unsigned long)esp_get_minimum_free_heap_size(),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned long)uxTaskGetStackHighWaterMark(NULL));
    if (result->kind!=JOB_SAVE) snprintf(notice,sizeof(notice),"%s",result->error==ESP_OK?"已更新":esp_err_to_name(result->error));
    if (result->kind==JOB_START_FM && result->ready) {
        esp_err_t e=passport_audio_fm();
        if (e==ESP_OK) {
            memset(&current,0,sizeof(current));
            snprintf(current.title,sizeof(current.title),"机核 FM");
            snprintf(current.url,sizeof(current.url),"https://www.gcores.com/radios#fm");
            current_fm=true; playing_channel=-1; current_episode=-1;
            seek_feedback.active=false; scrub.active=false; history_position=0;
            page=PASSPORT_PAGE_PLAYER; snprintf(notice,sizeof(notice),"加载中");
        } else snprintf(notice,sizeof(notice),"播放出错，请重试");
    }
    if (result->kind==JOB_PREPARE_PLAY && result->ready) {
        channel=result->channel; selected=result->episode;
        prepared_play=true; play(result->position,false); prepared_play=false;
    }
    if (result->kind==JOB_REFRESH && result->ready && channel==result->channel) selected=result->episode;
    if (result->kind!=JOB_SAVE && selected>=0) selected=passport_clamp_selection(selected,0,passport_catalog_episode_count(channel));
    return true;
}
static void app_task(void *arg) {
    (void)arg; last_input=last_save=esp_timer_get_time();
    bool connected_before=false;
    int64_t last_battery=0,last_ui=0;
    passport_audio_state_t previous=PASSPORT_AUDIO_IDLE;
    result_t pending_result; bool result_pending=false;
    for (;;) {
        passport_audio_status_t audio; passport_audio_get_status(&audio);
        seek_feedback_observe(&audio);
        if (!busy && !seek_feedback.active && (audio.state==PASSPORT_AUDIO_PLAYING || audio.state==PASSPORT_AUDIO_PAUSED)) history_position=audio.position_seconds;
        input_t input;
        if (xQueueReceive(inputs,&input,pdMS_TO_TICKS(100))==pdTRUE) {
            passport_page_t before_page=page;
            handle(input,&audio);
            if (page!=before_page) ESP_LOGI(TAG,"Navigation key=%u event=%u page=%u->%u",
                (unsigned)input.key,(unsigned)input.event,(unsigned)before_page,(unsigned)page);
        }
        if (!result_pending && xQueueReceive(results,&pending_result,0)==pdTRUE) result_pending=true;
        if (result_pending && finish_catalog_result(&pending_result)) result_pending=false;
        passport_wifi_poll(); bool connected=passport_wifi_connected();
        if (connected!=connected_before) { dirty=true; connected_before=connected; if (connected) snprintf(notice,sizeof(notice),"网络已连接");
            ESP_LOGI(TAG,"Network connected=%u heap=%lu min=%lu largest=%lu",(unsigned)connected,
                (unsigned long)esp_get_free_heap_size(),(unsigned long)esp_get_minimum_free_heap_size(),
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)); }
        auto_refresh_catalog(time(NULL),&audio);
        int64_t now=esp_timer_get_time();
        /* Recover a RELEASE dropped by a full input queue while a worker was
         * draining network services. Stable physical release is authoritative. */
        if (scrub.active) {
            if (bsp_button_read_mv()>=BSP_BTN_MV_RELEASED_MIN) {
                if (!scrub_released_at) scrub_released_at=now;
                if (now-scrub_released_at>=250000) handle((input_t){scrub_key,BSP_BTN_RELEASE},&audio);
            } else scrub_released_at=0;
        }
        if (passport_scrub_tick(&scrub,now/1000)) { dirty=true; last_input=now; }
        if (now-last_battery>=30000000) { battery=bsp_battery_soc(); last_battery=now; dirty=true; }
        if (audio.state!=previous) { previous=audio.state; dirty=true;
            if (audio.state==PASSPORT_AUDIO_ERROR) snprintf(notice,sizeof(notice),"音频：%s",esp_err_to_name(audio.last_error));
            else if (audio.state==PASSPORT_AUDIO_PLAYING) snprintf(notice,sizeof(notice),"正在播放");
            else if (audio.state==PASSPORT_AUDIO_PAUSED) snprintf(notice,sizeof(notice),"已暂停");
            else if (audio.state==PASSPORT_AUDIO_ENDED) { snprintf(notice,sizeof(notice),"本期播放完毕"); last_save=now-60000000; } }
        if (!current_fm && playing_channel>=0 && now-last_save>=60000000 && !busy && !seek_feedback.active &&
            (audio.state==PASSPORT_AUDIO_PLAYING || audio.state==PASSPORT_AUDIO_PAUSED || audio.state==PASSPORT_AUDIO_ENDED)) {
            if (begin(JOB_SAVE,playing_channel,0,audio.position_seconds,audio.state==PASSPORT_AUDIO_ENDED)) last_save=now;
            settings_save(); }
        passport_power_stage_t power=passport_power_stage_configured((now-last_input)/1000,active(&audio)||scrub.active,busy,passport_wifi_provisioning(),screen_timeouts[screen_timeout_index]*1000);
        if (sleep_requested && !busy && bsp_button_read_mv()>=BSP_BTN_MV_RELEASED_MIN) { sleep_requested=false; sleep_now(); }
        if ((power==PASSPORT_POWER_SLEEP || (sleep_deadline && now>=sleep_deadline && !busy && !scrub.active)) &&
            bsp_button_read_mv()>=BSP_BTN_MV_RELEASED_MIN) { sleep_now(); sleep_deadline=0; }
        else if (power==PASSPORT_POWER_DARK && !dark) screen_set_dark(true);
        else if (!dark) bsp_display_backlight(power==PASSPORT_POWER_DIM?(brightness>20?20:brightness):brightness);
        if (ui_activity_pending && bsp_lvgl_lock(200)) {
            passport_ui_set_active(!dark); ui_activity_pending=false;
            bsp_lvgl_unlock();
        }
        if (!dark && (dirty || ((page==PASSPORT_PAGE_PLAYER || page==PASSPORT_PAGE_WIFI) && now-last_ui>=1000000))) { render(&audio); dirty=false; last_ui=now; }
        if (passport_wifi_provisioning() && now-portal_started>600000000) {
            esp_err_t e=passport_wifi_end_provisioning();
            snprintf(notice,sizeof(notice),e==ESP_OK?"配网超时，热点已关闭":"关闭热点失败，请退出重试");
            if (e==ESP_OK) page=PASSPORT_PAGE_LIBRARY;
            else { portal_close_failed=true; portal_started=now-590000000; }
            dirty=true;
        }
    }
}
void app_main(void) {
    esp_err_t err=nvs_flash_init();
    if (err!=ESP_OK) { ESP_LOGE(TAG,"NVS failed (%s); no automatic erase",esp_err_to_name(err)); return; }
    const esp_pm_config_t pm={.max_freq_mhz=160,.min_freq_mhz=80,.light_sleep_enable=false}; ESP_ERROR_CHECK(esp_pm_configure(&pm));
    inputs=xQueueCreate(16,sizeof(input_t)); jobs=xQueueCreate(1,sizeof(job_t)); results=xQueueCreate(1,sizeof(result_t));
    if (!inputs || !jobs || !results) return;
    ESP_ERROR_CHECK(bsp_display_init()); if (!bsp_lvgl_init()) return;
    if (bsp_lvgl_lock(1000)) { passport_ui_init(); bsp_lvgl_unlock(); } bsp_display_backlight(70);
    bsp_battery_init(); audio_ok=passport_audio_init()==ESP_OK; ESP_ERROR_CHECK(passport_catalog_init());
    esp_err_t history_init=passport_history_init();
    if (history_init!=ESP_OK) ESP_LOGW(TAG,"Episode history unavailable: %s",esp_err_to_name(history_init));
    nvs_handle_t h; uint8_t saved;
    if (nvs_open("gpp_settings",NVS_READONLY,&h)==ESP_OK) {
        if (nvs_get_u8(h,"volume",&saved)==ESP_OK && saved<=100) volume=saved;
        if (nvs_get_u8(h,"brightness",&saved)==ESP_OK && saved>=20 && saved<=100) brightness=saved;
        if (nvs_get_u8(h,"screen_timeout",&saved)==ESP_OK && saved<6) screen_timeout_index=saved;
        nvs_get_i64(h,"catalog_updated",&catalog_last_success);
        nvs_close(h);
    }
    passport_audio_set_volume(volume);
    passport_resume_t resume;
    if (passport_catalog_load_resume(&resume) && resume.channel<5) { channel=resume.channel;
        passport_history_progress_t prior;
        if (!passport_history_lookup(resume.channel,resume.guid,&prior)) {
            uint32_t legacy_position=resume.total_seconds && resume.position_seconds>resume.total_seconds?
                resume.total_seconds:resume.position_seconds;
            passport_history_save(resume.channel,resume.guid,legacy_position,resume.total_seconds,resume.completed);
        }
        current_channel=resume.channel;
        snprintf(current.guid,sizeof(current.guid),"%s",resume.guid); snprintf(current.url,sizeof(current.url),"%s",resume.url);
        snprintf(current.title,sizeof(current.title),"%s",resume.title); current.duration_seconds=resume.total_seconds;
        history_position=resume.completed?0:resume.position_seconds;
        passport_catalog_set_playback(current_channel,&current);
        int restored=passport_catalog_find_episode_by_identity(channel,resume.guid,resume.url);
        if (restored>=0) { selected=restored; initial_resume=resume.completed?0:resume.position_seconds; }
    }
    ESP_ERROR_CHECK(passport_wifi_init()); portal_started=esp_timer_get_time(); if (passport_wifi_provisioning()) page=PASSPORT_PAGE_WIFI;
    esp_sntp_config_t ntp=ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com"); esp_netif_sntp_init(&ntp);
    if (esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_GPIO) key_guard_arm(&wake_guard,esp_timer_get_time(),5000000);
    ESP_ERROR_CHECK(bsp_button_init(input_callback,NULL)); key_guard_sync_released(&wake_guard,bsp_button_read_mv()>=BSP_BTN_MV_RELEASED_MIN);
    if (xTaskCreate(app_task,"passport",6144,NULL,4,NULL)!=pdPASS) { ESP_LOGE(TAG,"Task allocation failed"); return; }
    ESP_LOGI(TAG,"C3 240x320; heap=%lu largest=%lu; DFS 80-160 MHz; USB light sleep disabled",(unsigned long)esp_get_free_heap_size(),(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
