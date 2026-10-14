#include "passport_ui.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(passport_cjk_16);
LV_FONT_DECLARE(passport_cjk_bold_22);
LV_FONT_DECLARE(passport_cjk_12);
LV_FONT_DECLARE(passport_sans_12);
LV_FONT_DECLARE(passport_sans_14);
LV_FONT_DECLARE(passport_channel_11);
LV_FONT_DECLARE(passport_hint_12);
LV_IMAGE_DECLARE(passport_reference_badge);
LV_IMAGE_DECLARE(passport_player_background);
LV_IMAGE_DECLARE(passport_common_background);
LV_IMAGE_DECLARE(passport_footer_arrows);
LV_IMAGE_DECLARE(passport_footer_ok);
LV_IMAGE_DECLARE(passport_settings_footer);
LV_IMAGE_DECLARE(passport_home_footer);
LV_IMAGE_DECLARE(passport_header_speaker);
#define BG 0x0C0E0E
#define PANEL 0x141616
#define LINE 0x303431
#define MUTED 0x999B96
#define WHITE 0xF5F0E4
#define RED 0xFF5D2C

static lv_obj_t *screen,*content,*player,*heading,*channel,*header;
static lv_obj_t *rows[5],*row_titles[5],*row_details[5],*tabs[5];
static lv_obj_t *track_title,*chapter,*next_chapter,*player_channel,*play_state;
static lv_obj_t *progress,*times,*duration,*chapter_time,*next_time,*status,*footer_left,*footer_right;
/* These two descriptors are immutable and already reside in Flash.
 * 两份字体描述直接引用 Flash，仅需要修改 fallback 的正文小字保留副本。 */
#define body_font passport_cjk_16
#define title_font passport_cjk_bold_22
static lv_font_t chapter_font;
static lv_timer_t *feedback_timer;
static bool display_active=true,player_visible,feedback_persistent;
static int shown_volume=-1,shown_battery=-2,selected_row=-1;
static passport_page_t last_page=(passport_page_t)-1;
static bool last_network;
static uint32_t home_accent=0xFF453A;

static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o,false);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);return o;
}
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const lv_font_t *font,uint32_t color,const char *value) {
    lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_obj_set_style_anim_duration(o,lv_anim_speed_clamped(24,1000,10000),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_label_set_text(o,value);return o;
}
static lv_obj_t *reference_image(lv_obj_t *parent,int x,int y,const lv_image_dsc_t *source) {
    lv_obj_t *o=lv_image_create(parent);lv_image_set_src(o,source);lv_obj_set_pos(o,x,y);return o;
}
static void visible(lv_obj_t *o,bool show) {
    if(!o) return;
    lv_obj_set_hidden(o,!show);
}
static void rect(lv_layer_t *l,const lv_area_t *c,int x,int y,int w,int h,uint32_t color,int radius) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=radius;
    lv_area_t a={c->x1+x,c->y1+y,c->x1+x+w-1,c->y1+y+h-1};lv_draw_rect(l,&d,&a);
}
static void draw_header(lv_event_t *e) {
    lv_area_t c;lv_obj_get_coords(lv_event_get_target_obj(e),&c);lv_layer_t *l=lv_event_get_layer(e);
    /* Five compact bars rise into a right triangle; battery remains continuous. */
    for(int i=0;i<5;i++) rect(l,&c,164+i*4,18-(3+i*2),2,3+i*2,i*20<shown_volume?RED:LINE,0);
    if(shown_battery>0) rect(l,&c,210,9,(shown_battery*12+99)/100,8,WHITE,1);
    if(shown_battery<0) rect(l,&c,214,13,5,1,MUTED,0);
}
static void draw_row_art(lv_event_t *e) {
    lv_area_t c;lv_obj_get_coords(lv_event_get_target_obj(e),&c);lv_layer_t *l=lv_event_get_layer(e);
    int index=(int)(uintptr_t)lv_event_get_user_data(e);
    if(last_page==PASSPORT_PAGE_SETTINGS) {
        rect(l,&c,0,31,216,1,LINE,0);
        if(index==selected_row) rect(l,&c,0,6,3,20,RED,0);
        return;
    }
    if(index==selected_row) {
        lv_draw_rect_dsc_t border;lv_draw_rect_dsc_init(&border);border.bg_opa=LV_OPA_TRANSP;
        border.border_color=lv_color_hex(last_page==PASSPORT_PAGE_LIBRARY?home_accent:RED);border.border_width=2;border.border_opa=LV_OPA_COVER;border.radius=6;
        lv_draw_rect(l,&border,&c);
    }
    lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.src=&passport_reference_badge;
    lv_area_t a={c.x1+6,c.y1+8,c.x1+33,c.y1+35};lv_draw_image(l,&d,&a);
}
static void clean_text(lv_obj_t *o, const char *input, char *clean, size_t capacity)
{
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)(input ? input : "");
    const lv_font_t *font = lv_obj_get_style_text_font(o, LV_PART_MAIN);
    while (*p && used < capacity - 1) {
        uint32_t cp = *p;
        unsigned bytes = cp < 0x80 ? 1 : cp >= 0xC2 && cp <= 0xDF ? 2 :
                         cp >= 0xE0 && cp <= 0xEF ? 3 : cp >= 0xF0 && cp <= 0xF4 ? 4 : 0;
        bool valid = bytes != 0;
        if (bytes > 1) {
            cp &= (1u << (7 - bytes)) - 1;
            for (unsigned i = 1; i < bytes; ++i) {
                if (!p[i] || (p[i] & 0xC0) != 0x80) { valid = false; break; }
                cp = (cp << 6) | (p[i] & 0x3F);
            }
            if ((bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) ||
                (bytes == 4 && cp < 0x10000) || cp > 0x10FFFF ||
                (cp >= 0xD800 && cp <= 0xDFFF)) valid = false;
        }
        if (!valid) { clean[used++] = '?'; ++p; continue; }
        lv_font_glyph_dsc_t glyph = {0};
        bool available = cp == '\n' || (lv_font_get_glyph_dsc(font, &glyph, cp, 0) && !glyph.is_placeholder);
        if (!available) {
            ESP_LOGW("passport_ui", "Unsupported glyph U+%04lX", (unsigned long)cp);
            clean[used++] = '?';
        } else {
            if (used + bytes >= capacity) break;
            memcpy(clean + used, p, bytes);
            used += bytes;
        }
        p += bytes;
    }
    clean[used] = '\0';
}

static void text(lv_obj_t *o, const char *input)
{
    if(!o) return;
    char clean[512];
    clean_text(o,input,clean,sizeof(clean));
    /* Unchanged values must not invalidate the display on a periodic update.
     * 周期更新时，未变文本不会触发屏幕重绘。 */
    if (strcmp(lv_label_get_text(o), clean) != 0) lv_label_set_text(o, clean);
}

/* Overflow-only animation; clip mode stops redraws while the screen sleeps. */
static void scroll_label(lv_obj_t *o,bool active) {
    if(!o) return;
    lv_label_long_mode_t mode=active?LV_LABEL_LONG_SCROLL:LV_LABEL_LONG_CLIP;
    if(lv_label_get_long_mode(o)!=mode) lv_label_set_long_mode(o,mode);
}
static void scroll_state(void) {
    if(player_visible) {
        scroll_label(track_title,display_active);scroll_label(chapter,display_active);scroll_label(next_chapter,display_active);
    } else if(last_page==PASSPORT_PAGE_LIBRARY) {
        for(int i=0;i<4;i++) {bool active=display_active&&rows[i]&&!lv_obj_is_hidden(rows[i]);scroll_label(row_titles[i],active);scroll_label(row_details[i],active);}
    }
}
static void feedback_tick(lv_timer_t *t) {visible(play_state,false);lv_timer_pause(t);}
static void title_episode_suffix(char *title)
{
    for(size_t i=0;title[i];i++) {
        if((title[i]!='v' && title[i]!='V') || (i && title[i-1]!=' ' && title[i-1]!='\n')) continue;
        const char *s=title+i;
        if((s[1]!='o' && s[1]!='O') || (s[2]!='l' && s[2]!='L')) continue;
        const char *number=s+3;if(*number=='.') number++;
        while(*number==' ') number++;
        const char *end=number;while(*end>='0' && *end<='9') end++;
        if(end==number) continue;
        const char *tail=end;while(*tail==' ') tail++;
        if(*tail || (size_t)(end-number)>24) continue;
        while(i && (title[i-1]==' ' || title[i-1]=='\n')) i--;
        title[i]=0;break;
    }
}

static void title_update(const char *input) {
    char clean[512];clean_text(track_title,input,clean,sizeof(clean));title_episode_suffix(clean);
    /* LVGL owns the displayed string; no second persistent copy or nested
     * 512-byte sanitization frame is needed. / LVGL 保存显示文字，无需镜像。 */
    if(strcmp(clean,lv_label_get_text(track_title))) lv_label_set_text(track_title,clean);
}
static void time_text(lv_obj_t *o,uint32_t seconds) {
    char value[24];
    if(seconds>=3600) snprintf(value,sizeof(value),"%lu:%02lu:%02lu",(unsigned long)(seconds/3600),(unsigned long)(seconds/60%60),(unsigned long)(seconds%60));
    else snprintf(value,sizeof(value),"%02lu:%02lu",(unsigned long)(seconds/60),(unsigned long)(seconds%60));
    text(o,value);
}

static void create_browser(passport_page_t page) {
    bool home=page==PASSPORT_PAGE_LIBRARY;
    bool body=page==PASSPORT_PAGE_WIFI||page==PASSPORT_PAGE_HELP||page==PASSPORT_PAGE_TIMELINE_TEXT;
    content=box(screen,0,0,240,320,BG);
    reference_image(content,0,0,&passport_common_background);
    heading=home?NULL:label(content,12,31,216,&title_font,WHITE,"播客列表");
    channel=home?label(content,12,62,216,&chapter_font,RED,""):NULL;
    for(int i=0;i<5;i++) {
        if(home) {
        tabs[i]=label(content,12+i*43,35,40,&body_font,WHITE,"");
        lv_obj_set_height(tabs[i],25);lv_obj_set_style_text_align(tabs[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_bg_color(tabs[i],lv_color_hex(RED),0);lv_obj_set_style_radius(tabs[i],7,0);
        }
        if(body||(home&&i==4)) continue;
        rows[i]=box(content,12,80+i*48,216,46,PANEL);
        lv_obj_set_style_radius(rows[i],6,0);lv_obj_set_style_border_color(rows[i],lv_color_hex(RED),0);
        lv_obj_add_event_cb(rows[i],draw_row_art,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)i);
        row_titles[i]=label(rows[i],home?39:10,home?1:4,home?171:200,&body_font,WHITE,"");lv_obj_set_height(row_titles[i],24);
        if(home) row_details[i]=label(rows[i],39,25,171,&chapter_font,MUTED,"");

    }
    status=label(content,12,264,216,&chapter_font,MUTED,"");
    /* Footer objects live with the page that actually draws them. They retain
     * the same transparent backgrounds, positions and draw order. / 页脚按需创建。 */
    if(home||page==PASSPORT_PAGE_SETTINGS) {
        reference_image(content,0,286,home?&passport_home_footer:&passport_settings_footer);
    } else {
        lv_obj_t *footer=box(content,8,286,224,30,BG);lv_obj_set_style_bg_opa(footer,LV_OPA_TRANSP,0);
        reference_image(footer,4,11,&passport_footer_arrows);reference_image(footer,103,10,&passport_footer_ok);
        footer_left=label(footer,31,10,70,&passport_hint_12,WHITE,"");
        footer_right=label(footer,124,10,100,&passport_hint_12,WHITE,"");
    }
}
static void create_player(void) {
    player=box(screen,0,0,240,320,BG);reference_image(player,0,0,&passport_player_background);visible(player,false);
    track_title=label(player,11,31,216,&title_font,WHITE,"");lv_obj_set_height(track_title,32);
    player_channel=label(player,102,72,127,&passport_channel_11,RED,"");
    next_time=label(player,118,127,34,&passport_sans_12,MUTED,"");
    chapter_time=label(player,117,96,40,&passport_sans_14,RED,"");
    chapter=label(player,165,93,64,&body_font,WHITE,"");lv_obj_set_height(chapter,24);
    next_chapter=label(player,157,127,72,&chapter_font,MUTED,"");lv_obj_set_height(next_chapter,17);
    play_state=label(player,12,173,100,&chapter_font,WHITE,"");lv_obj_set_height(play_state,38);
    lv_label_set_long_mode(play_state,LV_LABEL_LONG_WRAP);lv_obj_set_style_bg_color(play_state,lv_color_hex(BG),0);
    lv_obj_set_style_bg_opa(play_state,LV_OPA_70,0);visible(play_state,false);
    progress=lv_bar_create(player);lv_obj_remove_style_all(progress);lv_obj_set_pos(progress,12,149);lv_obj_set_size(progress,216,4);
    lv_bar_set_range(progress,0,1000);lv_obj_set_style_bg_opa(progress,LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_bg_color(progress,lv_color_hex(LINE),LV_PART_MAIN);lv_obj_set_style_radius(progress,2,LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress,lv_color_hex(RED),LV_PART_INDICATOR);lv_obj_set_style_bg_opa(progress,LV_OPA_COVER,LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress,3,LV_PART_INDICATOR);
    times=label(player,12,155,108,&passport_sans_12,WHITE,"");duration=label(player,120,155,108,&passport_sans_12,WHITE,"");
    lv_obj_set_style_text_align(duration,LV_TEXT_ALIGN_RIGHT,0);
    visible(player,true);
}
void passport_ui_init(void) {
    if(screen) return;
    chapter_font=passport_cjk_12;
    chapter_font.fallback=&passport_sans_12;
    screen=box(NULL,0,0,240,320,BG);
    header=box(screen,0,0,240,26,BG);lv_obj_set_style_bg_opa(header,LV_OPA_TRANSP,0);
    lv_obj_add_event_cb(header,draw_header,LV_EVENT_DRAW_MAIN,NULL);
    feedback_timer=lv_timer_create(feedback_tick,4000,NULL);lv_timer_pause(feedback_timer);
    lv_screen_load(screen);
}

void passport_ui_update(const passport_view_t *v) {
    if(!v||!screen) return;
    static const char *names[]={"播客列表","播放","栏目","设置","Wi-Fi 配网","章节","按键帮助","时间轴","章节文字"};
    static const char *tab_names[]={"游戏","生活","兴趣","核市","银屏"};
    passport_page_t page=v->page>=PASSPORT_PAGE_LIBRARY&&v->page<=PASSPORT_PAGE_TIMELINE_TEXT?v->page:PASSPORT_PAGE_LIBRARY;
    bool is_player=page==PASSPORT_PAGE_PLAYER,is_home=page==PASSPORT_PAGE_LIBRARY;
    static const uint32_t channel_colors[]={0xFF453A,0x51C878,0xFF922E,0x4D9EFF,0xAD78F5};
    uint32_t accent=is_home?channel_colors[v->channel_index>=0&&v->channel_index<5?v->channel_index:0]:home_accent;
    bool accent_changed=accent!=home_accent;home_accent=accent;
    bool body=page==PASSPORT_PAGE_WIFI||page==PASSPORT_PAGE_HELP||page==PASSPORT_PAGE_TIMELINE_TEXT;
    bool page_changed=page!=last_page;
    player_visible=is_player;
    if(page_changed) {
        lv_timer_pause(feedback_timer);
        if(player) {lv_obj_delete(player);player=NULL;}
        if(content) {lv_obj_delete(content);content=NULL;}
        /* Old footer children have just been deleted with content. / 清掉旧页子控件指针。 */
        heading=channel=status=footer_left=footer_right=NULL;
        track_title=chapter=next_chapter=player_channel=play_state=NULL;
        progress=times=duration=chapter_time=next_time=NULL;
        for(int i=0;i<5;i++) tabs[i]=rows[i]=row_titles[i]=row_details[i]=NULL;
        if(is_player) create_player();else create_browser(page);
        lv_obj_move_foreground(header);
    }
    if(!is_player) {text(heading,names[page]);text(channel,v->channel?v->channel:"");visible(channel,is_home);}
    if(is_home) lv_obj_set_style_text_color(channel,lv_color_hex(home_accent),0);
    int vol=v->volume<0?0:v->volume>100?100:v->volume;
    int batt=v->battery_percent<0?-1:v->battery_percent>100?100:v->battery_percent;
    if(last_network!=v->wifi_connected||shown_battery!=batt||shown_volume!=vol||page_changed) {
        last_network=v->wifi_connected;shown_battery=batt;shown_volume=vol;lv_obj_invalidate(header);
    }
    char value[80];
    bool selection_changed=selected_row!=v->selected;selected_row=v->selected;
    for(int i=0;i<5;i++) {
        if(is_player) break;
        if(tabs[i]) visible(tabs[i],is_home);
        if(is_home) {
            text(tabs[i],tab_names[i]);bool active=i==v->channel_index;
            lv_obj_set_style_bg_color(tabs[i],lv_color_hex(home_accent),0);
            lv_obj_set_style_bg_opa(tabs[i],active?LV_OPA_COVER:LV_OPA_TRANSP,0);
            lv_obj_set_style_text_color(tabs[i],lv_color_hex(active?WHITE:MUTED),0);
        }
        bool show=!body&&i<v->item_count&&(!is_home||i<4);
        if(!rows[i]) continue;
        visible(rows[i],show);if(!show) continue;
        lv_obj_set_pos(rows[i],12,(is_home?80:91)+i*(is_home?46:34));lv_obj_set_height(rows[i],is_home?44:32);
        lv_obj_set_style_bg_opa(rows[i],page==PASSPORT_PAGE_SETTINGS?LV_OPA_TRANSP:LV_OPA_70,0);if(selection_changed||accent_changed) lv_obj_invalidate(rows[i]);
        text(row_titles[i],v->titles[i]?v->titles[i]:"");
        lv_obj_set_style_text_color(row_titles[i],lv_color_hex(page==PASSPORT_PAGE_SETTINGS&&i==selected_row?RED:WHITE),0);
        if(row_details[i]) visible(row_details[i],is_home);
        if(is_home) {
            snprintf(value,sizeof(value),"%s%s%s",v->details[i]?v->details[i]:"",
                     v->row_playback[i]?"  ":"",v->row_playback[i]==2?"已暂停":v->row_playback[i]?"正在播放":"");
            text(row_details[i],value);
            lv_obj_set_style_text_color(row_details[i],lv_color_hex(v->row_playback[i]==1?home_accent:MUTED),0);
        }
    }
    if(!is_player) {visible(status,true);lv_obj_set_pos(status,12,body?78:267);
    lv_obj_set_height(status,body?198:17);lv_label_set_long_mode(status,body?LV_LABEL_LONG_WRAP:LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(status,page==PASSPORT_PAGE_HELP?&body_font:&chapter_font,0);
    lv_obj_set_style_text_color(status,lv_color_hex(page==PASSPORT_PAGE_HELP?WHITE:MUTED),0);
    text(status,v->status?v->status:"");}
    if(is_player) {
        title_update(v->title?v->title:"选择节目开始收听");
        text(player_channel,v->channel?v->channel:"");
        const char *current=v->current_chapter?v->current_chapter:"";
        const char *split=strchr(current,' ');
        if(split&&split-current<16) {
            snprintf(value,sizeof(value),"%.*s",(int)(split-current),current);text(chapter_time,value);current=split+1;
        } else text(chapter_time,"");
        const char *upcoming=v->next_chapter?v->next_chapter:"";
        split=strchr(upcoming,' ');
        if(split&&split-upcoming<16) {snprintf(value,sizeof(value),"%.*s",(int)(split-upcoming),upcoming);text(next_time,value);upcoming=split+1;}
        else text(next_time,"");
        bool no_timeline=!current[0]&&!upcoming[0];
        visible(chapter_time,!no_timeline);visible(next_time,!no_timeline);
        lv_obj_set_pos(chapter,no_timeline?117:165,93);lv_obj_set_width(chapter,no_timeline?112:64);
        lv_obj_set_pos(next_chapter,no_timeline?117:157,127);lv_obj_set_width(next_chapter,no_timeline?112:72);
        text(chapter,no_timeline?"暂无时间轴":current);
        text(next_chapter,no_timeline?"暂无时间轴":upcoming);
        const char *state=v->playback_state?v->playback_state:v->playing?"ON AIR":"PAUSED";
        const char *feedback="";bool persistent=false;
        if(!strcmp(state,"PAUSED")) {
            persistent=v->hint&&strstr(v->hint,"已选定位");
            feedback=persistent?"已选定位目标\n确定后定位":"已暂停";
        }
        else if(!strcmp(state,"SEEK")) {feedback=v->hint&&strstr(v->hint,"按住")?"松手后定位":"正在定位…";persistent=true;}
        else if(!strcmp(state,"BUFFER")) {feedback="加载中";persistent=true;}
        else if(!strcmp(state,"ERROR")) {feedback="播放出错\n请重试";persistent=true;}
        feedback_persistent=persistent;
        bool feedback_changed=strcmp(feedback,lv_label_get_text(play_state))!=0;
        if(feedback_changed||page_changed) {
            text(play_state,feedback);
            visible(play_state,feedback[0]!=0);
            if(feedback[0]&&!persistent&&display_active) {lv_timer_reset(feedback_timer);lv_timer_resume(feedback_timer);}
            else lv_timer_pause(feedback_timer);
        }
        uint32_t fraction=v->duration_seconds?(uint32_t)((uint64_t)v->position_seconds*1000/v->duration_seconds):0;
        int fraction_value=fraction>1000?1000:(int)fraction;
        if(lv_bar_get_value(progress)!=fraction_value) lv_bar_set_value(progress,fraction_value,LV_ANIM_OFF);
        time_text(times,v->position_seconds);time_text(duration,v->duration_seconds);
    } else {lv_timer_pause(feedback_timer);}
    if(is_player) {text(footer_left,"音量/进度");text(footer_right,"播放/暂停/菜单");}
    else if(is_home) {text(footer_left,"选择/栏目");text(footer_right,"打开/设置");}
    else if(body) {text(footer_left,"浏览");text(footer_right,"确认/返回");}
    else {text(footer_left,"选择");text(footer_right,"调整/返回");}
    last_page=page;
    scroll_state();
}
void passport_ui_set_active(bool active) {
    if(display_active==active) return;
    display_active=active;
    if(active&&player&&feedback_persistent&&lv_label_get_text(play_state)[0]) visible(play_state,true);
    if(!active&&feedback_timer) {lv_timer_pause(feedback_timer);if(player) visible(play_state,false);}
    scroll_state();
}
