#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "bsp_button.h"
#include "passport_audio.h"
#include "passport_catalog.h"
#include "passport_logic.h"
#include "passport_ui.h"
#include "passport_wifi.h"

typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } input_t;
typedef enum { JOB_REFRESH, JOB_CHAPTERS, JOB_SAVE, JOB_PREPARE_PLAY } job_kind_t;
typedef struct { job_kind_t kind; int channel,episode; uint32_t position; bool completed; } job_t;
typedef struct { job_kind_t kind; esp_err_t error; bool ready,restart,paused; int channel,episode; uint32_t position,restart_position; } result_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef enum {eRunning,eSuspended} eTaskState;
#define pdTRUE 1
#define pdPASS 1
#define MALLOC_CAP_8BIT 1
#define ESP_LOGI(...) ((void)0)
static QueueHandle_t jobs=(void *)1,results=(void *)2;
static TaskHandle_t catalog_worker;
static job_t queued_job;
static result_t queued_result;
static bool job_queued,result_queued,create_fail,send_fail,deleted,require_freed;
static unsigned task_creates,task_deletes;
static eTaskState task_state;
static jmp_buf suspended_jump;
static esp_err_t catalog_error,stop_error;
static void worker(void *arg);
static passport_page_t page;
static passport_page_t return_page[PASSPORT_PAGE_TIMELINE_TEXT+1];
static int channel,selected,menu_selected,playing_channel,current_channel;
static passport_episode_t current;
static bool prepared_play;
static int current_episode;
static uint32_t initial_resume,history_position;
static int volume,sleep_minutes,brightness,screen_timeout_index,battery;
static const uint32_t screen_timeouts[]={15,30,60,120,300,0};
static char chapter_line[220],next_chapter_line[220],details[5][64],rows[5][385];
static passport_view_t rendered;
bool bsp_lvgl_lock(uint32_t timeout) { (void)timeout; return true; }
void bsp_lvgl_unlock(void) {}
void passport_ui_update(const passport_view_t *view) { rendered=*view; }
static int64_t last_input,last_save,sleep_deadline,portal_started,scrub_released_at;
static bool busy,dark,dirty,settings_dirty,scrub_was_paused,sleep_requested,audio_ok,portal_close_failed;
static bool ui_activity_pending;
static passport_screen_guard_t screen_guard;
static bsp_btn_t scrub_key;
static passport_scrub_t scrub;
static struct { bool active,paused; uint32_t target,request_id; } seek_feedback;
static char notice[240];

static int64_t clock_us;
static unsigned seeks,pauses,volume_sets,backlights,ends,starts,saves,plays,job_starts,playback_saves;
static uint32_t last_seek;
static int last_volume,last_backlight;
static bool pause_commands[8],provisioning,connected;
static esp_err_t exit_error,seek_error,pause_error;
static job_kind_t last_job_kind;
static uint32_t last_job_position;
static passport_audio_status_t audio;
static passport_episode_t catalog_episode;
static int catalog_count,catalog_index;
static bool chapter_fixture;
int64_t esp_timer_get_time(void) { return clock_us; }
const char *esp_err_to_name(esp_err_t error) { return error==ESP_OK?"ESP_OK":"ESP_FAIL"; }
static esp_err_t stop_wait(void) { return stop_error; }
static void settings_save(void) { ++saves; }
esp_err_t save_playback(uint32_t position,bool completed) { (void)position; (void)completed; ++playback_saves; return ESP_OK; }
static int xQueueSend(QueueHandle_t queue,const void *value,unsigned timeout) {
    (void)timeout; if(send_fail) return 0;
    if(queue==jobs) {queued_job=*(const job_t *)value;job_queued=true;last_job_kind=queued_job.kind;last_job_position=queued_job.position;job_starts++;}
    else {queued_result=*(const result_t *)value;result_queued=true;}
    return pdTRUE;
}
static int xQueueReceive(QueueHandle_t queue,void *value,unsigned timeout) {
    (void)timeout;if(queue==jobs&&job_queued) {*(job_t *)value=queued_job;job_queued=false;return pdTRUE;}
    if(queue==results&&result_queued) {*(result_t *)value=queued_result;result_queued=false;return pdTRUE;}
    return 0;
}
static int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)name;(void)arg;assert(entry==worker&&stack==10240&&priority==3);task_creates++;
    if(create_fail) return 0;*handle=(void *)3;task_state=eRunning;deleted=false;return pdPASS;
}
static eTaskState eTaskGetState(TaskHandle_t task) {assert(task==catalog_worker);return task_state;}
static void vTaskDelete(TaskHandle_t task) {assert(task==catalog_worker&&task_state==eSuspended);task_deletes++;deleted=true;}
static void vTaskSuspend(TaskHandle_t task) {assert(!task);task_state=eSuspended;longjmp(suspended_jump,1);}
esp_err_t passport_catalog_refresh(int c) {(void)c;return catalog_error;}
esp_err_t passport_catalog_fetch_chapters(int c,int e) {(void)c;(void)e;return catalog_error;}
void bsp_display_backlight(int brightness_percent) { ++backlights; last_backlight=brightness_percent; }
esp_err_t passport_audio_seek(uint32_t seconds) {
    ++seeks; last_seek=seconds;
    if (seek_error==ESP_OK) { ++audio.seek_request_id; audio.seeking=true; }
    return seek_error;
}
esp_err_t passport_audio_pause(bool paused) {
    assert(pauses<8); pause_commands[pauses++]=paused; return pause_error;
}
esp_err_t passport_audio_set_volume(uint8_t percent) { ++volume_sets; last_volume=percent; return ESP_OK; }
esp_err_t passport_audio_play(const char *url,uint32_t position) { (void)url; (void)position; if(require_freed) assert(deleted && !catalog_worker); ++plays; return ESP_OK; }
void passport_audio_get_status(passport_audio_status_t *out) { *out=audio; }
bool passport_wifi_provisioning(void) { return provisioning; }
bool passport_wifi_connected(void) { return connected; }
esp_err_t passport_wifi_end_provisioning(void) {
    ++ends; if (exit_error==ESP_OK) provisioning=false; return exit_error;
}
esp_err_t passport_wifi_start_provisioning(void) { ++starts; provisioning=true; return ESP_OK; }
int passport_catalog_episode_count(int c) { (void)c; return catalog_count; }
const char *passport_wifi_ap_password(void) { return "fixture-password"; }
const char *passport_catalog_channel_name(int c) { (void)c; return "频道"; }
bool passport_catalog_get_episode(int c,int e,passport_episode_t *out) {
    (void)c; if (e<0 || e>=catalog_count) return false; *out=catalog_episode; if (e!=catalog_index) snprintf(out->guid,sizeof(out->guid),"other-%d",e); return true;
}
int passport_catalog_find_episode_by_identity(int c,const char *guid,const char *url) {
    (void)c; return !strcmp(guid,catalog_episode.guid) && !strcmp(url,catalog_episode.url)?catalog_index:-1;
}
size_t passport_catalog_chapter_count(int c,int e) { (void)c; (void)e; return 0; }
bool passport_catalog_get_chapter(int c,int e,size_t i,passport_chapter_t *out) {
    (void)c; (void)e; if (!chapter_fixture || i>1) return false; out->start_seconds=i?600:300; strcpy(out->title,i?"下一章节":"当前章节"); return true;
}
bool passport_catalog_current_chapter(int c,int e,uint32_t position,passport_chapter_t *out,size_t *index) {
    (void)position; if (!chapter_fixture) return false; if (index) *index=0; return passport_catalog_get_chapter(c,e,0,out);
}

#include "passport_main_handle.h"

static void setup(passport_page_t initial) {
    catalog_worker=NULL;job_queued=result_queued=create_fail=send_fail=deleted=require_freed=false;task_creates=task_deletes=0;task_state=eRunning;catalog_error=stop_error=ESP_OK;
    page=initial; channel=selected=menu_selected=current_channel=0; playing_channel=-1;
    memset(&current,0,sizeof(current)); initial_resume=history_position=0;
    prepared_play=false; current_episode=-1; catalog_count=5; catalog_index=0; chapter_fixture=false;
    volume=50; sleep_minutes=0; brightness=70; screen_timeout_index=2;
    last_input=last_save=sleep_deadline=portal_started=scrub_released_at=0;
    busy=dark=dirty=settings_dirty=scrub_was_paused=sleep_requested=portal_close_failed=false; audio_ok=true;
    ui_activity_pending=false;
    memset(return_page,0,sizeof(return_page));
    memset(&screen_guard,0,sizeof(screen_guard)); memset(&scrub,0,sizeof(scrub)); notice[0]=0;
    memset(&seek_feedback,0,sizeof(seek_feedback));
    clock_us=1000000; seeks=pauses=volume_sets=backlights=ends=starts=saves=plays=job_starts=playback_saves=0;
    last_seek=0; last_volume=last_backlight=-1; memset(pause_commands,0,sizeof(pause_commands));
    provisioning=false; connected=true; exit_error=seek_error=pause_error=ESP_OK;
    audio=(passport_audio_status_t){.state=PASSPORT_AUDIO_PLAYING,.position_seconds=500,.duration_seconds=10000};
    memset(&catalog_episode,0,sizeof(catalog_episode));
    strcpy(catalog_episode.url,"https://example.com/episode.mp3"); strcpy(catalog_episode.guid,"episode-guid");
}
static void fire(bsp_btn_t key,bsp_btn_ev_t event) {
    clock_us+=1000; handle((input_t){key,event},&audio);
}
static void test_volume_direction_and_limits(void) {
    setup(PASSPORT_PAGE_PLAYER);
    fire(BSP_BTN_UP,BSP_BTN_CLICK); assert(volume==55 && last_volume==55);
    fire(BSP_BTN_DOWN,BSP_BTN_CLICK); assert(volume==50 && last_volume==50);
    fire(BSP_BTN_UP,BSP_BTN_DOUBLE); assert(volume==60 && last_volume==60);
    volume=100; fire(BSP_BTN_UP,BSP_BTN_CLICK); assert(volume==100);
    volume=0; fire(BSP_BTN_DOWN,BSP_BTN_CLICK); assert(volume==0);
    assert(volume_sets==5 && seeks==0);
}
static void test_scrub_preview_commits_once_on_matching_release(void) {
    setup(PASSPORT_PAGE_PLAYER);
    fire(BSP_BTN_DOWN,BSP_BTN_PRESS); assert(seeks==0 && pauses==0);
    fire(BSP_BTN_DOWN,BSP_BTN_LONG);
    assert(scrub.active && scrub.target==530 && seeks==0 && pauses==1 && pause_commands[0]);
    for (unsigned i=0;i<35;++i) {
        clock_us+=200000; assert(passport_scrub_tick(&scrub,clock_us/1000));
        fire(BSP_BTN_DOWN,BSP_BTN_LONG); assert(seeks==0 && pauses==1);
    }
    uint32_t target=scrub.target; assert(target>530);
    fire(BSP_BTN_UP,BSP_BTN_RELEASE); assert(seeks==0 && scrub.active);
    fire(BSP_BTN_DOWN,BSP_BTN_RELEASE);
    assert(seeks==1 && last_seek==target && !scrub.active && history_position==target);
    assert(pauses==2 && !pause_commands[1]);
    fire(BSP_BTN_DOWN,BSP_BTN_RELEASE); assert(seeks==1 && pauses==2);
}
static void test_paused_scrub_preserves_pause_and_rewinds(void) {
    setup(PASSPORT_PAGE_PLAYER); audio.state=PASSPORT_AUDIO_PAUSED;
    fire(BSP_BTN_UP,BSP_BTN_PRESS); fire(BSP_BTN_UP,BSP_BTN_LONG);
    assert(scrub.active && scrub.target==470 && pauses==1 && pause_commands[0]);
    fire(BSP_BTN_UP,BSP_BTN_RELEASE);
    assert(seeks==1 && last_seek==470 && pauses==1);
}
static void test_seek_feedback_waits_for_actual_completion(void) {
    setup(PASSPORT_PAGE_PLAYER);
    fire(BSP_BTN_DOWN,BSP_BTN_LONG); fire(BSP_BTN_DOWN,BSP_BTN_RELEASE);
    assert(seek_feedback.active && !seek_feedback.paused && seek_feedback.target==530);
    passport_view_t view={.position_seconds=500,.playback_state="ON AIR"};
    seek_feedback_view(&view); assert(view.position_seconds==530 && !strcmp(view.playback_state,"SEEK"));
    audio.seeking=true; audio.state=PASSPORT_AUDIO_PAUSED;
    seek_feedback_observe(&audio); assert(seek_feedback.active);
    audio.state=PASSPORT_AUDIO_BUFFERING; seek_feedback_observe(&audio); assert(seek_feedback.active);
    audio.state=PASSPORT_AUDIO_PLAYING; seek_feedback_observe(&audio); assert(seek_feedback.active);
    audio.seeking=false; audio.seek_complete_id=0;
    seek_feedback_observe(&audio); assert(seek_feedback.active); /* Older PLAYING snapshot is not acknowledgement. */
    audio.seek_complete_id=seek_feedback.request_id;
    seek_feedback_observe(&audio); assert(!seek_feedback.active);
    setup(PASSPORT_PAGE_PLAYER); audio.state=PASSPORT_AUDIO_PAUSED;
    fire(BSP_BTN_UP,BSP_BTN_LONG); fire(BSP_BTN_UP,BSP_BTN_RELEASE);
    assert(seek_feedback.active && seek_feedback.paused && seek_feedback.target==470);
    view=(passport_view_t){.position_seconds=500,.playback_state="PAUSED"};
    seek_feedback_view(&view); assert(view.position_seconds==470 && !strcmp(view.playback_state,"PAUSED"));
    audio.seeking=true; seek_feedback_observe(&audio); assert(seek_feedback.active);
    fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(!seek_feedback.paused && !pause_commands[1]);
    audio.state=PASSPORT_AUDIO_BUFFERING; seek_feedback_observe(&audio); assert(seek_feedback.active);
    audio.state=PASSPORT_AUDIO_PLAYING; audio.seeking=false; audio.seek_complete_id=seek_feedback.request_id;
    seek_feedback_observe(&audio); assert(!seek_feedback.active);
    setup(PASSPORT_PAGE_PLAYER); seek_error=ESP_FAIL;
    fire(BSP_BTN_DOWN,BSP_BTN_LONG); fire(BSP_BTN_DOWN,BSP_BTN_RELEASE);
    assert(!seek_feedback.active && history_position==0 && strstr(notice,"定位失败"));
    setup(PASSPORT_PAGE_PLAYER);
    fire(BSP_BTN_DOWN,BSP_BTN_LONG); fire(BSP_BTN_DOWN,BSP_BTN_RELEASE);
    audio.state=PASSPORT_AUDIO_ERROR; audio.seeking=false; seek_feedback_observe(&audio);
    assert(!seek_feedback.active);
    setup(PASSPORT_PAGE_PLAYER); audio.state=PASSPORT_AUDIO_PAUSED;
    fire(BSP_BTN_UP,BSP_BTN_LONG); fire(BSP_BTN_UP,BSP_BTN_RELEASE);
    uint32_t first_request=seek_feedback.request_id;
    fire(BSP_BTN_UP,BSP_BTN_LONG); assert(scrub.target==440); /* Start at retained displayed target, not stale 500. */
    fire(BSP_BTN_UP,BSP_BTN_RELEASE);
    assert(seek_feedback.target==440 && seek_feedback.paused && seek_feedback.request_id!=first_request);
    audio.seeking=false; audio.state=PASSPORT_AUDIO_PLAYING; audio.seek_complete_id=first_request;
    seek_feedback_observe(&audio); assert(seek_feedback.active);
}
static void test_dark_gesture_is_completely_swallowed(void) {
    for (unsigned key=0;key<3;++key) {
        setup(PASSPORT_PAGE_PLAYER); dark=true;
        fire((bsp_btn_t)key,BSP_BTN_PRESS);
        assert(!dark && backlights==1 && last_backlight==70);
        fire((bsp_btn_t)key,BSP_BTN_LONG);
        fire((bsp_btn_t)key,BSP_BTN_RELEASE);
        fire((bsp_btn_t)key,BSP_BTN_CLICK);
        fire((bsp_btn_t)key,BSP_BTN_DOUBLE);
        assert(!dark && seeks==0 && pauses==0 && volume_sets==0 && !scrub.active);
        assert(page==PASSPORT_PAGE_PLAYER);
        clock_us+=400000;
        fire((bsp_btn_t)key,BSP_BTN_PRESS); fire((bsp_btn_t)key,BSP_BTN_RELEASE); fire((bsp_btn_t)key,BSP_BTN_CLICK);
        assert(key==BSP_BTN_OK?pauses==1:volume_sets==1);
    }
}
static void test_double_ok_blanks_without_stopping_playback(void) {
    setup(PASSPORT_PAGE_PLAYER);
    fire(BSP_BTN_OK,BSP_BTN_DOUBLE);
    assert(dark && backlights==1 && last_backlight==0 && pauses==0 && plays==0 && seeks==0);
}
static void test_wifi_exit_and_failure_stays_on_page(void) {
    const bsp_btn_ev_t exits[]={BSP_BTN_CLICK,BSP_BTN_LONG};
    for (unsigned i=0;i<2;++i) {
        setup(PASSPORT_PAGE_WIFI); provisioning=true;
        fire(BSP_BTN_OK,exits[i]);
        assert(ends==1 && !provisioning && page==PASSPORT_PAGE_LIBRARY && connected);
        setup(PASSPORT_PAGE_WIFI); provisioning=true; exit_error=ESP_FAIL;
        fire(BSP_BTN_OK,exits[i]);
        assert(ends==1 && provisioning && page==PASSPORT_PAGE_WIFI && connected);
        assert(strstr(notice,"关闭热点失败") && portal_close_failed);
        exit_error=ESP_OK; fire(BSP_BTN_OK,exits[i]);
        assert(ends==2 && page==PASSPORT_PAGE_LIBRARY);
    }
    setup(PASSPORT_PAGE_WIFI); fire(BSP_BTN_OK,BSP_BTN_CLICK);
    assert(ends==0 && page==PASSPORT_PAGE_LIBRARY);
    portal_close_failed=true; enter_page(PASSPORT_PAGE_WIFI);
    assert(!portal_close_failed);
}
static void test_same_identity_returns_without_restart_or_save(void) {
    const passport_audio_state_t running[]={PASSPORT_AUDIO_PLAYING,PASSPORT_AUDIO_BUFFERING,PASSPORT_AUDIO_PAUSED};
    for (unsigned i=0;i<3;++i) {
        setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0; audio.state=running[i];
        initial_resume=20; fire(BSP_BTN_OK,BSP_BTN_CLICK);
        assert(page==PASSPORT_PAGE_PLAYER && plays==0 && playback_saves==0 && pauses==0 && job_starts==0);
        assert(audio.position_seconds==500 && initial_resume==0);
    }
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0;
    play(123,true);
    assert(plays==0 && playback_saves==0 && seeks==1 && last_seek==123 && pauses==1 && !pause_commands[0]);
    assert(page==PASSPORT_PAGE_PLAYER);
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0; audio.state=PASSPORT_AUDIO_PAUSED;
    play(123,true); assert(plays==0 && playback_saves==0 && seeks==1 && pauses==1 && !pause_commands[0]);
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0; seek_error=ESP_FAIL;
    play(123,true); assert(plays==0 && playback_saves==0 && seeks==1 && pauses==0);
    assert(strstr(notice,"定位失败"));
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0;
    strcpy(catalog_episode.url,"https://example.com/new-source.mp3"); play(123,true);
    assert(plays==0 && playback_saves==0 && seeks==0 && job_starts==1 && last_job_kind==JOB_PREPARE_PLAY && last_job_position==123);
    prepared_play=true; play(123,true); prepared_play=false;
    assert(plays==1 && playback_saves==0 && seeks==0 && history_position==123 && current_episode==selected);
    assert(last_save==clock_us); /* Old stream's periodic-save deadline cannot carry into a new identity. */
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0;
    strcpy(catalog_episode.guid,"different-guid"); play(0,false);
    assert(plays==0 && playback_saves==0 && job_starts==1 && last_job_kind==JOB_PREPARE_PLAY);
    prepared_play=true; play(0,false); assert(plays==1);
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0;
    strcpy(catalog_episode.url,"https://example.com/different.mp3"); play(0,false);
    assert(plays==0 && playback_saves==0 && job_starts==1 && last_job_kind==JOB_PREPARE_PLAY);
    prepared_play=true; play(0,false); assert(plays==1);
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0; audio.state=PASSPORT_AUDIO_ENDED;
    play(0,false); assert(plays==0 && playback_saves==0 && job_starts==1);
    prepared_play=true; play(0,false); assert(plays==1);
}
static void test_navigation_keeps_player_without_audio_commands(void) {
    setup(PASSPORT_PAGE_PLAYER); current=catalog_episode; playing_channel=0;
    fire(BSP_BTN_OK,BSP_BTN_LONG);
    assert(page==PASSPORT_PAGE_LIBRARY && selected==0);
    fire(BSP_BTN_OK,BSP_BTN_LONG);
    assert(page==PASSPORT_PAGE_SETTINGS && return_page[PASSPORT_PAGE_SETTINGS]==PASSPORT_PAGE_LIBRARY);
    fire(BSP_BTN_OK,BSP_BTN_LONG);
    assert(page==PASSPORT_PAGE_LIBRARY && plays==0 && playback_saves==0 && pauses==0);
    fire(BSP_BTN_OK,BSP_BTN_CLICK);
    assert(page==PASSPORT_PAGE_PLAYER && plays==0 && playback_saves==0);
    /* Nested Wi-Fi returns to Settings, then Home, preserving the stream. */
    fire(BSP_BTN_OK,BSP_BTN_LONG); fire(BSP_BTN_OK,BSP_BTN_LONG);
    menu_selected=1; fire(BSP_BTN_OK,BSP_BTN_CLICK);
    assert(page==PASSPORT_PAGE_WIFI && provisioning && return_page[PASSPORT_PAGE_WIFI]==PASSPORT_PAGE_SETTINGS);
    fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(page==PASSPORT_PAGE_SETTINGS && ends==1);
    fire(BSP_BTN_OK,BSP_BTN_LONG); assert(page==PASSPORT_PAGE_LIBRARY);
}
static void test_home_tabs_and_selection_are_separate(void) {
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=0;
    fire(BSP_BTN_UP,BSP_BTN_CLICK); assert(selected==0 && channel==0);
    fire(BSP_BTN_DOWN,BSP_BTN_CLICK); assert(selected==1 && channel==0);
    fire(BSP_BTN_DOWN,BSP_BTN_LONG); assert(channel==1 && selected==0 && page==PASSPORT_PAGE_LIBRARY);
    fire(BSP_BTN_UP,BSP_BTN_LONG); assert(channel==0 && selected==0);
    fire(BSP_BTN_UP,BSP_BTN_LONG); assert(channel==4 && selected==0);
    fire(BSP_BTN_DOWN,BSP_BTN_LONG); assert(channel==0 && selected==0);
    selected=4; fire(BSP_BTN_DOWN,BSP_BTN_CLICK); assert(selected==4);
    assert(plays==0 && pauses==0 && seeks==0 && job_starts==0);
    /* Empty cached tabs remain browsable without interrupting playback. */
    catalog_count=0; fire(BSP_BTN_DOWN,BSP_BTN_LONG);
    assert(channel==1 && selected==0 && job_starts==0);
    fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(page==PASSPORT_PAGE_LIBRARY && plays==0 && job_starts==0);
    busy=true; fire(BSP_BTN_DOWN,BSP_BTN_LONG); assert(channel==1);
}
static void test_settings_reindexed_without_redundant_pages(void) {
    setup(PASSPORT_PAGE_SETTINGS);
    fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(job_starts==1 && last_job_kind==JOB_REFRESH);
    task_state=eSuspended; result_t done={.kind=JOB_REFRESH}; assert(finish_catalog_result(&done));
    menu_selected=2; fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(brightness==80 && settings_dirty);
    menu_selected=3; fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(screen_timeout_index==3);
    menu_selected=4; fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(sleep_minutes==15 && sleep_deadline>clock_us);
    menu_selected=7; fire(BSP_BTN_DOWN,BSP_BTN_CLICK); assert(menu_selected==7);
    fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(page==PASSPORT_PAGE_HELP);
    fire(BSP_BTN_OK,BSP_BTN_LONG); assert(page==PASSPORT_PAGE_SETTINGS);
    menu_selected=6; fire(BSP_BTN_OK,BSP_BTN_CLICK); assert(sleep_requested && !plays && !pauses);
}
static void test_render_home_identity_marker_and_next_chapter(void) {
    setup(PASSPORT_PAGE_LIBRARY); current=catalog_episode; playing_channel=current_channel=0;
    catalog_episode.duration_seconds=3601;
    render(&audio); assert(rendered.item_count==4 && rendered.selected==0 && rendered.channel_index==0);
    assert(rendered.row_playback[0]==1 && rendered.row_playback[1]==0 && !strcmp(rendered.details[0],"60:01"));
    audio.state=PASSPORT_AUDIO_PAUSED; render(&audio); assert(rendered.row_playback[0]==2);
    /* Feed reorder uses GUID+URL rather than a previously saved index. */
    catalog_index=4; selected=4; render(&audio);
    assert(rendered.item_count==1 && rendered.selected==0 && rendered.row_playback[0]==2);
    strcpy(catalog_episode.guid,"replaced-guid"); render(&audio); assert(rendered.row_playback[0]==0);
    current=catalog_episode; chapter_fixture=true; page=PASSPORT_PAGE_PLAYER; channel=3;
    render(&audio); assert(rendered.channel_index==0 && strstr(rendered.current_chapter,"05:00") && strstr(rendered.next_chapter,"10:00"));
    chapter_fixture=false; render(&audio); assert(!rendered.current_chapter && !rendered.next_chapter);
    page=PASSPORT_PAGE_SETTINGS; menu_selected=0; render(&audio);
    assert(rendered.item_count==5 && !strcmp(rendered.titles[0],"更新播客列表") && !strcmp(rendered.titles[1],"Wi-Fi 配网"));
    menu_selected=7; render(&audio); assert(rendered.item_count==3 && rendered.selected==2 && !strcmp(rendered.titles[2],"按键帮助"));
}
static void run_catalog_worker(void) {
    assert(catalog_worker && job_queued);
    if(setjmp(suspended_jump)==0) worker(NULL);
    assert(task_state==eSuspended && result_queued && !job_queued);
}
static void test_catalog_worker_reclaimed_before_audio(void) {
    setup(PASSPORT_PAGE_LIBRARY);require_freed=true;
    assert(begin(JOB_PREPARE_PLAY,0,0,123,false));
    assert(busy && catalog_worker && task_creates==1 && plays==0);
    assert(!begin(JOB_REFRESH,0,0,0,false));
    run_catalog_worker();result_t result;assert(xQueueReceive(results,&result,0)==pdTRUE);
    /* Result delivery alone is not proof that the producer stopped. */
    task_state=eRunning;assert(!finish_catalog_result(&result));
    assert(busy && catalog_worker && task_deletes==0 && plays==0);
    task_state=eSuspended;assert(finish_catalog_result(&result));
    assert(task_deletes==1 && !busy && !catalog_worker && plays==1 && page==PASSPORT_PAGE_PLAYER && history_position==123);
    assert(!finish_catalog_result(&result)); /* no double deletion */
    const passport_audio_state_t prior[]={PASSPORT_AUDIO_PLAYING,PASSPORT_AUDIO_PAUSED};
    for(unsigned i=0;i<2;i++) {
        setup(PASSPORT_PAGE_SETTINGS);current=catalog_episode;audio.state=prior[i];require_freed=true;
        catalog_error=ESP_FAIL; /* Existing playback must recover even if refresh fails. */
        assert(begin(JOB_REFRESH,0,0,0,false));run_catalog_worker();
        assert(plays==0 && pauses==0);assert(xQueueReceive(results,&result,0)==pdTRUE);
        assert(result.restart && result.ready && result.restart_position==500 && result.paused==(i==1));
        assert(finish_catalog_result(&result));assert(plays==1 && pauses==i && result.error==ESP_FAIL && page==PASSPORT_PAGE_SETTINGS);
        if(i) assert(pause_commands[0]);
    }
    setup(PASSPORT_PAGE_SETTINGS);current=catalog_episode;stop_error=ESP_FAIL;
    assert(begin(JOB_REFRESH,0,0,0,false));run_catalog_worker();
    assert(xQueueReceive(results,&result,0)==pdTRUE && !result.ready);
    assert(finish_catalog_result(&result) && plays==0 && task_deletes==1);
    setup(PASSPORT_PAGE_LIBRARY);create_fail=true;
    assert(!begin(JOB_PREPARE_PLAY,0,0,0,false));assert(!busy && !catalog_worker && !job_queued && task_deletes==0);
    create_fail=false;assert(begin(JOB_SAVE,0,0,500,false));run_catalog_worker();
    assert(xQueueReceive(results,&result,0)==pdTRUE);assert(finish_catalog_result(&result));
    assert(plays==0 && task_deletes==1 && playback_saves==1);
    setup(PASSPORT_PAGE_LIBRARY);send_fail=true;
    assert(!begin(JOB_REFRESH,0,0,0,false) && !busy && task_creates==0 && !catalog_worker);
}
int main(void) {
    test_volume_direction_and_limits(); test_scrub_preview_commits_once_on_matching_release();
    test_paused_scrub_preserves_pause_and_rewinds(); test_dark_gesture_is_completely_swallowed();
    test_seek_feedback_waits_for_actual_completion();
    test_double_ok_blanks_without_stopping_playback(); test_wifi_exit_and_failure_stays_on_page();
    test_same_identity_returns_without_restart_or_save(); test_navigation_keeps_player_without_audio_commands();
    test_home_tabs_and_selection_are_separate(); test_settings_reindexed_without_redundant_pages();
    test_render_home_identity_marker_and_next_chapter();
    test_catalog_worker_reclaimed_before_audio();
    puts("Passport main actual handle/play: PASS (13 scenario groups; volume, deferred seek, completion feedback, wake swallowing, blanking, AP exit and identity-preserving navigation)");
    return 0;
}
