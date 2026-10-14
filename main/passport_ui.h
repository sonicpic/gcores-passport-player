#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PASSPORT_PAGE_LIBRARY,
    PASSPORT_PAGE_PLAYER,
    PASSPORT_PAGE_CHANNELS,
    PASSPORT_PAGE_SETTINGS,
    PASSPORT_PAGE_WIFI,
    PASSPORT_PAGE_CHAPTERS,
    PASSPORT_PAGE_HELP,
    PASSPORT_PAGE_TIMELINE,
    PASSPORT_PAGE_TIMELINE_TEXT
} passport_page_t;

/* Strings are borrowed only for the duration of update and are copied by LVGL.
 * 中文字符串只在 update 期间借用，LVGL 会复制它们。 */
typedef struct {
    passport_page_t page;
    int selected;
    uint8_t item_count;
    const char *titles[5];
    const char *details[5]; /* Optional per-row subtitle, borrowed during update. */
    uint8_t row_playback[5]; /* 0: other, 1: playing, 2: paused. */
    bool row_highlight[5]; /* Prominent session navigation, independent of cursor. */
    uint8_t row_icon[5]; /* 0: text only, 1: current player, 2: Gcores FM. */
    int channel_index;
    const char *channel;
    const char *current_chapter; /* Optional formatted timeline text; null/empty means unavailable. / 当前时间轴文字，可为空。 */
    const char *next_chapter;
    const char *title;
    bool playing;
    bool fm; /* FM has no seek/progress/timeline controls. */
    bool updating_catalog;
    const char *playback_state; /* Optional short ASCII state, e.g. BUFFERING. */
    bool wifi_connected;
    uint32_t position_seconds;
    uint32_t duration_seconds;
    int volume;
    int battery_percent; /* -1: unavailable / 无读数 */
    const char *status;
    const char *hint;
} passport_view_t;

/* Caller must hold bsp_lvgl_lock(), unless already in the LVGL task.
 * 调用方必须持有 BSP LVGL 锁，或已在 LVGL 任务中。 A shared 4-second timer pages overflowing player text; a notice timer hides transient feedback.
 * 共用四秒计时器翻页超长文字，提示计时器隐藏临时状态；不新增任务。 */
void passport_ui_init(void);
void passport_ui_update(const passport_view_t *view);
/* Screen-off pauses text paging; wake retains the current page. Caller holds LVGL lock.
 * 息屏暂停文字翻页，唤醒保留当前页；调用方持有 LVGL 锁。 */
void passport_ui_set_active(bool active);
