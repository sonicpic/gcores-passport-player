/* Desktop-only LVGL rendering harness / 仅供桌面验证的 LVGL 渲染工具。 */
#include "lvgl.h"
#include "src/widgets/label/lv_label_private.h"
#include "passport_ui.h"
#include "bsp_display_rounding.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
LV_FONT_DECLARE(passport_cjk_16);
LV_FONT_DECLARE(passport_hint_12);
LV_FONT_DECLARE(passport_sans_14);

LV_FONT_DECLARE(passport_cjk_bold_22);
LV_FONT_DECLARE(passport_cjk_12);
static void verify_standard_fonts(void) {
    FILE *f=fopen("../../fonts/standard-characters/characters.txt","rb");assert(f);
    unsigned char data[40000];size_t n=fread(data,1,sizeof(data),f);fclose(f);
    const lv_font_t *fonts[]={&passport_cjk_bold_22,&passport_cjk_16,&passport_cjk_12};
    unsigned count=0,supplementary=0;
    for(size_t i=0;i<n;) {
        uint32_t cp=data[i++];if(cp=='\n'||cp=='\r') continue;
        unsigned rest=cp<0x80?0:cp<0xE0?1:cp<0xF0?2:3;
        if(rest) cp&=(1u<<(6-rest))-1;
        while(rest--) {assert(i<n&&(data[i]&0xC0)==0x80);cp=(cp<<6)|(data[i++]&0x3F);}
        for(unsigned j=0;j<3;j++) {lv_font_glyph_dsc_t g={0};assert(lv_font_get_glyph_dsc(fonts[j],&g,cp,0)&&!g.is_placeholder);}
        count++;if(cp>0xFFFF) supplementary++;
    }
    assert(count==8105&&supplementary==196);
    for(unsigned j=0;j<3;j++) {lv_font_glyph_dsc_t g={0};assert(!lv_font_get_glyph_dsc(fonts[j],&g,0x1F600,0)||g.is_placeholder);}
    puts("PASS all 8105 standard Chinese glyphs at three sizes, including 196 supplementary characters; unsupported emoji boundary");
}
static uint8_t framebuffer[240*320*3];
static uint16_t draw_buffer[240*40];
static lv_obj_t *player_title_object(lv_obj_t *parent) {
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++) {
        lv_obj_t *child=lv_obj_get_child(parent,i);
        lv_area_t area;lv_obj_get_coords(child,&area);
        if(lv_obj_check_type(child,&lv_label_class) && area.x1==11 && area.y1==31) return child;
        lv_obj_t *found=player_title_object(child);if(found) return found;
    }
    return NULL;
}
static lv_obj_t *notice_object(lv_obj_t *parent) {
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++) {
        lv_obj_t *child=lv_obj_get_child(parent,i);lv_area_t area;lv_obj_get_coords(child,&area);
        if(lv_obj_check_type(child,&lv_label_class)&&area.x1==12&&area.y1==173) return child;
        lv_obj_t *found=notice_object(child);if(found) return found;
    }
    return NULL;
}
static lv_obj_t *label_at(lv_obj_t *parent,int x,int y) {
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++) {
        lv_obj_t *child=lv_obj_get_child(parent,i);lv_area_t area;lv_obj_get_coords(child,&area);
        if(lv_obj_check_type(child,&lv_label_class)&&area.x1==x&&area.y1==y) return child;
        lv_obj_t *found=label_at(child,x,y);if(found) return found;
    }
    return NULL;
}
static unsigned header_pixels(int x1,int y1,int x2,int y2,bool orange) {
    unsigned count=0;
    for(int y=y1;y<=y2;y++) for(int x=x1;x<=x2;x++) {
        unsigned i=(y*240+x)*3;int r=framebuffer[i],g=framebuffer[i+1],b=framebuffer[i+2];
        if(orange?(r>180&&g<130&&b<90):(r>180&&g>180&&b>180)) count++;
    }
    return count;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    uint16_t *src=(uint16_t*)pixels;
    for(int y=area->y1;y<=area->y2;y++) for(int x=area->x1;x<=area->x2;x++) {
        uint16_t c=*src++;
        if(x<0||x>=240||y<0||y>=320) continue;
        unsigned idx=(y*240+x)*3;
        if(bsp_display_pixel_outside_rounded_rect(x,y,240,320,30)) c=0;
        framebuffer[idx]=((c>>11)&31)*255/31;
        framebuffer[idx+1]=((c>>5)&63)*255/63;
        framebuffer[idx+2]=(c&31)*255/31;
    }
    lv_display_flush_ready(display);
}

int main(void)
{
    setbuf(stdout,NULL);
    lv_init();
    verify_standard_fonts();
    lv_display_t *display=lv_display_create(240,320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    passport_ui_init();
    lv_mem_monitor_t initmem;lv_mem_monitor(&initmem);printf("Init free=%lu largest=%lu\n",(unsigned long)initmem.free_size,(unsigned long)initmem.free_biggest_size);
    const char *hints[]={"长按确定返回\n双击确定息屏", "单击上下选集 确定播放\n长按上章节 下栏目 确定设置",
        "单击上下选 确定进入\n长按确定返回 双击息屏", "单击上下选 确定跳转\n长按确定返回 双击息屏",
        "单击上下选 确定切换\n长按确定返回 双击息屏", "单击确定退出并关热点\n长按下配网 确定返回",
        "长按确定返回上页\n双击确定息屏，播放继续", "单击上加下减 确定暂停\n长按上退下进 确定设置",
        "按住上退下进 松开定位\n先移动进度，松开后读取", "单击上下浏览 确定读全文\n长按确定返回 双击息屏",
        "文本阅读不改变播放位置\n长按确定返回 双击息屏"};
    const char *footer_labels[]={"音量/进度","播放/暂停/菜单","选择/栏目","打开/设置","选择","调整/返回","浏览","确认/返回"};
    lv_font_t font=passport_hint_12;
    for(unsigned h=0;h<8;h++) {
        lv_point_t size;lv_text_get_size(&size,footer_labels[h],&font,0,0,10000,LV_TEXT_FLAG_NONE);
        printf("Footer %u: %ld x %ld px\n",h,(long)size.x,(long)size.y);
        assert(size.x<=(h==1?100:70)&&size.y<=15);
    }
    const char *names[]={"library","player","channels","settings","wifi","chapters","help","settings-2","player-seek","library-2","timeline","timeline-text"};
    for(int p=0;p<12;p++) {
        passport_view_t v={.page=(passport_page_t)p,.selected=1,.item_count=5,
            .titles={"电子游戏里的声音","从像素到真实世界","那些改变我们的游戏","电台里的一封来信","游戏与生活的交点"},
            .channel_index=0,.channel="机核游戏频道",.title="电子游戏里的声音与记忆",
            .playing=true,.wifi_connected=true,.position_seconds=1458,.duration_seconds=3820,
            .volume=65,.battery_percent=87,.status="最近更新 / 01 - 05",.hint=hints[1]};
        if(p==0) {v.item_count=4;v.row_playback[1]=1;v.details[0]="vol. 127 / 1:03:52";v.details[1]="vol. 126 / 58:36";v.titles[0]="游戏，只是开始";v.titles[1]="电子游戏里的声音";v.titles[2]="从像素到真实世界";v.titles[3]="那些改变我们的游戏";v.titles[4]="电台里的一封来信";v.status="正在播放 / 01 - 05";}
        /* Synthetic timeline fixture, not extracted programme content.
         * 虚构时间轴测试文字，不代表实际节目内容。 */
        if(p==1) {v.hint=hints[7];v.current_chapter="21:10 测试章节标题";}
        if(p==2) {v.titles[0]="机核游戏频道";v.titles[1]="机核生活频道";v.titles[2]="机核音频频道";v.titles[3]="机核 PRO";v.titles[4]="核市奇谭";v.hint=hints[2];}
        if(p==3) {v.titles[0]="更新播客列表";v.titles[1]="Wi-Fi 配网";v.titles[2]="亮度：70%";v.titles[3]="自动息屏：60 秒";v.titles[4]="定时停止：关闭";v.hint=hints[4];}
        if(p==4) {v.status="保存中\n手机连接热点\nGcores-Passport\n密码：ABCDEFGHIJKL\n浏览器打开\nhttp://192.168.4.1\n使用 2.4 GHz 网络";v.hint=hints[5];}
        if(p==5) {v.titles[0]="00:00 开场";v.titles[1]="06:20 第一段声音";v.titles[2]="21:10 记忆的回声";v.titles[3]="38:40 游戏与音乐";v.titles[4]="55:30 尾声";v.hint=hints[3];}
        if(p==6) {v.item_count=0;v.status="首页：上下选节目，OK播放\n长按上下切频道，长按OK设置\n播放：上下音量，OK播放暂停\n长按上下调进度，松手定位\n设置：上下选择，OK调整\n播放/设置：长按OK回首页\n双击OK息屏，任意键唤醒\n深睡停止播放，保存进度";v.hint=hints[6];}
        if(p==7) {v.page=PASSPORT_PAGE_SETTINGS;v.selected=4;v.item_count=5;v.titles[0]="定时停止：关闭";v.titles[1]="息屏（继续播放）";v.titles[2]="停止并深睡";v.titles[3]="按键帮助";v.titles[4]="按键帮助";v.hint=hints[4];}
        if(p==8) {v.page=PASSPORT_PAGE_PLAYER;v.playback_state="SEEK";v.position_seconds=1800;v.hint=hints[8];}
        if(p==9) {v.page=PASSPORT_PAGE_LIBRARY;v.selected=0;v.item_count=1;v.titles[0]="游戏与生活的交点";v.status="最近更新 / 06 - 06";}
        if(p==10) {v.page=PASSPORT_PAGE_TIMELINE;v.item_count=4;v.titles[0]="00:00 测试开场";v.titles[1]="06:20 测试章节";v.titles[2]="21:10 测试时间点";v.titles[3]="38:40 测试长文字";v.status="虚构文字，仅验证布局";v.hint=hints[9];}
        if(p==11) {v.page=PASSPORT_PAGE_TIMELINE_TEXT;v.item_count=0;v.status="06:20\n这是时间轴的测试文字，\n用于核对全文阅读的换行\n和字体显示。这里不是实\n际节目的时间轴，也不会\n触发音频跳转。";v.hint=hints[10];}
        passport_ui_update(&v);
        lv_refr_now(display);
        if(p==4||p==6) {
            lv_obj_t *root=lv_screen_active();
            for(uint32_t j=0;j<lv_obj_get_child_count(root);j++) {
                lv_obj_t *panel=lv_obj_get_child(root,j);
                for(uint32_t k=0;k<lv_obj_get_child_count(panel);k++) {
                    lv_obj_t *o=lv_obj_get_child(panel,k);lv_area_t a;lv_obj_get_coords(o,&a);
                    if(lv_obj_check_type(o,&lv_label_class)&&a.y1==62) assert(lv_obj_is_hidden(o));
                    /* Body-only pages must allocate no unused list panels.
                     * 正文页不创建看不见的列表行。 */
                    assert(!lv_obj_check_type(o,&lv_obj_class)||a.y1<80||a.y1>=260);
                }
            }
            if(p==6) {lv_point_t size;lv_text_get_size(&size,v.status,&passport_cjk_16,0,0,216,LV_TEXT_FLAG_NONE);assert(size.y<=198);}
        }

        if(p==0||p==3) {
            const int black_points[][2]={{5,100},{230,180},{120,276},{17,84}};
            for(unsigned k=0;k<4;k++) {unsigned idx=(black_points[k][1]*240+black_points[k][0])*3;
                assert(framebuffer[idx]==0&&framebuffer[idx+1]==0&&framebuffer[idx+2]==0);}
            puts("PASS home/settings actual middle background and row fill are pure black");
        }
        char path[100];snprintf(path,sizeof(path),"%s.ppm",names[p]);
        FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");fwrite(framebuffer,1,sizeof(framebuffer),f);fclose(f);
        lv_mem_monitor_t monitor;lv_mem_monitor(&monitor);
        printf("%s: LVGL used=%lu peak=%lu free=%lu largest=%lu\n",names[p],
               (unsigned long)(monitor.total_size-monitor.free_size), (unsigned long)monitor.max_used,
               (unsigned long)monitor.free_size,(unsigned long)monitor.free_biggest_size);
    }
    passport_view_t themed={.page=PASSPORT_PAGE_LIBRARY,.item_count=1,.selected=0,
        .titles={"频道主题色测试标题"},.details={"01:20"},.row_playback={1},.channel="机核游戏频道",.volume=50,.battery_percent=50};
    const uint32_t colors[]={0xFF453A,0x51C878,0xFF922E,0x4D9EFF,0xAD78F5};
    for(int c=0;c<5;c++) {
        themed.channel_index=c;passport_ui_update(&themed);lv_refr_now(display);
        unsigned px=(47*240+15+c*43)*3;
        uint32_t color=colors[c];
        assert(abs((int)framebuffer[px]-(int)(color>>16))<10);
        assert(abs((int)framebuffer[px+1]-(int)((color>>8)&255))<10);
        assert(abs((int)framebuffer[px+2]-(int)(color&255))<10);
        char path[40];snprintf(path,sizeof(path),"channel-%d.ppm",c);
        FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");fwrite(framebuffer,1,sizeof(framebuffer),f);fclose(f);
    }
    puts("PASS five channel accent colors in actual rendered home tabs");
    passport_view_t reference={.page=PASSPORT_PAGE_PLAYER,.title="游戏，只是开始 vol. 127",
        .channel="机核游戏频道",.current_chapter="21:10 游戏的另一种可能",.next_chapter="32:40 声音与记忆",
        .playing=true,.playback_state="ON AIR",.wifi_connected=true,
        .position_seconds=1697,.duration_seconds=3832,.volume=16,.battery_percent=100};
    unsigned volume_counts[3],battery_counts[3];
    for(int i=0;i<3;i++) {
        reference.volume=i*50;reference.battery_percent=i*50;
        passport_ui_update(&reference);lv_refr_now(display);
        volume_counts[i]=header_pixels(163,7,190,19,true);
        battery_counts[i]=header_pixels(211,10,221,16,false);
    }
    assert(volume_counts[0]<volume_counts[1]&&volume_counts[1]<volume_counts[2]);
    assert(battery_counts[0]<battery_counts[1]&&battery_counts[1]<battery_counts[2]);
    puts("PASS actual rendered volume and battery fill increase at 0/50/100 percent");
    reference.volume=16;reference.battery_percent=100;
    passport_ui_update(&reference); lv_refr_now(display);
    FILE *ref=fopen("player-reference.ppm","wb");assert(ref);
    fprintf(ref,"P6\n240 320\n255\n");fwrite(framebuffer,1,sizeof(framebuffer),ref);fclose(ref);
    reference.playing=false;reference.playback_state="PAUSED";passport_ui_update(&reference);lv_refr_now(display);
    lv_obj_t *notice=notice_object(lv_screen_active());assert(notice&&!lv_obj_is_hidden(notice));
    lv_tick_inc(4100);lv_timer_handler();lv_refr_now(display);assert(lv_obj_is_hidden(notice));
    passport_ui_update(&reference);assert(lv_obj_is_hidden(notice));
    reference.hint="已选定位目标";passport_ui_update(&reference);lv_refr_now(display);
    lv_tick_inc(8100);lv_timer_handler();lv_refr_now(display);assert(!lv_obj_is_hidden(notice));
    reference.hint="";
    reference.playback_state="SEEK";passport_ui_update(&reference);lv_refr_now(display);
    lv_tick_inc(8100);lv_timer_handler();lv_refr_now(display);assert(!lv_obj_is_hidden(notice));
    passport_ui_set_active(false);assert(lv_obj_is_hidden(notice));
    passport_ui_set_active(true);assert(!lv_obj_is_hidden(notice));
    reference.playing=true;reference.playback_state="ON AIR";passport_ui_update(&reference);
    assert(lv_obj_is_hidden(notice));
    puts("PASS pause notice expires without repeat; seek notice persists; normal audio hides notice; wake restores persistent feedback");
    reference.title="游戏里的声音与记忆，第二段不同的标题内容，第三段完整显示";
    reference.current_chapter="21:10 第一段完整章节文字，后续的章节文字也要能够阅读";
    passport_ui_update(&reference);lv_refr_now(display);
    lv_obj_t *title_object=player_title_object(lv_screen_active());assert(title_object);
    assert(lv_label_get_long_mode(title_object)==LV_LABEL_LONG_SCROLL);
    for(int i=0;i<30;i++) {lv_tick_inc(100);lv_timer_handler();}
    assert(((lv_label_t *)title_object)->offset.x<0);
    passport_ui_set_active(false);lv_refr_now(display);
    assert(lv_label_get_long_mode(title_object)==LV_LABEL_LONG_CLIP);
    for(int i=0;i<30;i++) {lv_tick_inc(100);lv_timer_handler();}
    assert(((lv_label_t *)title_object)->offset.x==0);
    passport_ui_set_active(true);lv_refr_now(display);
    for(int i=0;i<30;i++) {lv_tick_inc(100);lv_timer_handler();}
    assert(((lv_label_t *)title_object)->offset.x<0);
    reference.current_chapter="";reference.next_chapter="";passport_ui_update(&reference);lv_refr_now(display);
    unsigned placeholders=0;
    lv_obj_t *page=lv_obj_get_parent(title_object);
    for(uint32_t i=0;i<lv_obj_get_child_count(page);i++) {
        lv_obj_t *child=lv_obj_get_child(page,i);
        if(lv_obj_check_type(child,&lv_label_class)&&!strcmp(lv_label_get_text(child),"暂无时间轴")) {
            lv_area_t a;lv_obj_get_coords(child,&a);assert(a.x1==117&&lv_obj_get_width(child)==112);placeholders++;
        }
        if(child!=title_object&&lv_obj_check_type(child,&lv_label_class)&&
           lv_obj_get_style_text_font(child,0)==&passport_sans_14) assert(lv_obj_is_hidden(child));
    }
    assert(placeholders==2);
    FILE *no_axis=fopen("player-no-timeline.ppm","wb");assert(no_axis);fprintf(no_axis,"P6\n240 320\n255\n");fwrite(framebuffer,1,sizeof(framebuffer),no_axis);fclose(no_axis);
    reference.page=PASSPORT_PAGE_LIBRARY;passport_ui_update(&reference);lv_refr_now(display);
    lv_tick_inc(8100);lv_timer_handler();lv_refr_now(display);
    assert(player_title_object(lv_screen_active())==NULL);
    puts("PASS real title scrolling, screen-off stop/resume, page teardown and two missing-timeline labels");
    /* Maximum catalogue UTF-8 title, full rows, fallback boundaries and
     * multi-line Wi-Fi credentials / 最长标题、多行配网及字体边界压力验证。 */
    char long_title[385];
    for(unsigned i=0;i<128;i++) {long_title[i*3]=(char)0xE6;long_title[i*3+1]=(char)0xA0;long_title[i*3+2]=(char)0xB8;}
    long_title[384]=0;
    passport_view_t stress={.page=PASSPORT_PAGE_LIBRARY,.item_count=5,.selected=4,
        .titles={long_title,long_title,long_title,long_title,long_title},.title=long_title,
        .channel="机核电台",.current_chapter=long_title,.status="网络已连接",.hint=hints[7],.battery_percent=-1};
    for(int pass=0;pass<3;pass++) for(int p=0;p<9;p++) {
        stress.page=(passport_page_t)p;
        stress.status=p==4?"保存中\n手机连接热点\nGcores-Passport\n密码：ABCDEFGHIJKL\n浏览器打开\nhttp://192.168.4.1\n使用 2.4 GHz 网络":"网络已连接";
        passport_ui_update(&stress);lv_refr_now(display);
    }
    lv_mem_monitor_t monitor;lv_mem_monitor(&monitor);
    printf("long-title stress: used=%lu peak=%lu free=%lu largest=%lu\n",
        (unsigned long)(monitor.total_size-monitor.free_size),(unsigned long)monitor.max_used,
        (unsigned long)monitor.free_size,(unsigned long)monitor.free_biggest_size);
    /* LVGL's own string replaces the former 512-byte mirrors: verify all three
     * maximum fixtures are complete and periodic updates retain the scroll.
     * 删除镜像后仍完整保留三份最长文字，相同更新不重置滚动。 */
    stress.page=PASSPORT_PAGE_PLAYER;stress.next_chapter=long_title;
    stress.playing=true;stress.playback_state="ON AIR";
    passport_ui_update(&stress);lv_refr_now(display);
    lv_obj_t *full_labels[]={label_at(lv_screen_active(),11,31),label_at(lv_screen_active(),165,93),label_at(lv_screen_active(),157,127)};
    for(unsigned i=0;i<3;i++) {assert(full_labels[i]);assert(!strcmp(lv_label_get_text(full_labels[i]),long_title));}
    for(int i=0;i<30;i++) {lv_tick_inc(100);lv_timer_handler();}
    int32_t offsets[3];
    for(unsigned i=0;i<3;i++) {offsets[i]=((lv_label_t *)full_labels[i])->offset.x;assert(offsets[i]<0);}
    passport_ui_update(&stress);lv_refr_now(display);
    for(unsigned i=0;i<3;i++) assert(((lv_label_t *)full_labels[i])->offset.x==offsets[i]);
    passport_ui_set_active(false);lv_refr_now(display);
    for(unsigned i=0;i<3;i++) assert(lv_label_get_long_mode(full_labels[i])==LV_LABEL_LONG_CLIP);
    for(int i=0;i<30;i++) {lv_tick_inc(100);lv_timer_handler();}
    for(unsigned i=0;i<3;i++) assert(((lv_label_t *)full_labels[i])->offset.x==0);
    passport_ui_set_active(true);lv_refr_now(display);
    for(int i=0;i<30;i++) {lv_tick_inc(100);lv_timer_handler();}
    for(unsigned i=0;i<3;i++) assert(((lv_label_t *)full_labels[i])->offset.x<0);
    puts("PASS complete 385-byte title/current/next fixtures; unchanged updates retain scroll; sleep stops all three animations");
    /* Switching between pages with and without dynamically allocated footers
     * must release every old object and animation, with stable pool use.
     * 动态页脚反复创建释放后池占用保持稳定。 */
    const passport_page_t lifecycle[]={PASSPORT_PAGE_LIBRARY,PASSPORT_PAGE_SETTINGS,PASSPORT_PAGE_WIFI,
        PASSPORT_PAGE_HELP,PASSPORT_PAGE_TIMELINE_TEXT,PASSPORT_PAGE_CHAPTERS,PASSPORT_PAGE_PLAYER};
    uint32_t free_baseline=0;
    for(int pass=0;pass<8;pass++) {
        for(unsigned p=0;p<sizeof(lifecycle)/sizeof(lifecycle[0]);p++) {
            stress.page=lifecycle[p];passport_ui_update(&stress);lv_refr_now(display);
            lv_tick_inc(100);lv_timer_handler();
            assert(lv_mem_test()==LV_RESULT_OK);
        }
        passport_ui_set_active(false);lv_refr_now(display);lv_timer_handler();
        lv_mem_monitor(&monitor);
        if(pass==0) free_baseline=monitor.free_size;else assert(monitor.free_size==free_baseline);
        passport_ui_set_active(true);lv_refr_now(display);
    }
    printf("PASS eight repeated page/footer lifecycle cycles: free=%lu largest=%lu peak=%lu\n",
        (unsigned long)monitor.free_size,(unsigned long)monitor.free_biggest_size,(unsigned long)monitor.max_used);
    assert(lv_mem_test()==LV_RESULT_OK);
    passport_view_t update={.page=PASSPORT_PAGE_SETTINGS,.item_count=5,.selected=0,
        .titles={"回到当前播放","更新播客列表","Wi-Fi 配网","亮度：70%","自动息屏：60 秒"},
        .row_highlight={true},.updating_catalog=true,.status="正在更新节目库，请稍后……"};
    passport_ui_update(&update);lv_refr_now(display);
    lv_obj_t *settings_notice=label_at(lv_screen_active(),12,267);assert(settings_notice);
    assert(!strcmp(lv_label_get_text(settings_notice),update.status));
    assert(lv_label_get_long_mode(settings_notice)==LV_LABEL_LONG_SCROLL);
    unsigned highlight_pixel=(97*240+22)*3;
    assert(framebuffer[highlight_pixel]>30 && framebuffer[highlight_pixel]>framebuffer[highlight_pixel+1]);
    FILE *highlight_file=fopen("settings-return.ppm","wb");assert(highlight_file);
    fprintf(highlight_file,"P6\n240 320\n255\n");fwrite(framebuffer,1,sizeof(framebuffer),highlight_file);fclose(highlight_file);
    update.page=PASSPORT_PAGE_PLAYER;update.playback_state="READY";
    passport_ui_update(&update);lv_refr_now(display);
    lv_obj_t *player_notice=notice_object(lv_screen_active());assert(player_notice);
    assert(!strcmp(lv_label_get_text(player_notice),update.status) && !lv_obj_is_hidden(player_notice));
    assert(lv_obj_get_width(player_notice)==216 && lv_label_get_long_mode(player_notice)==LV_LABEL_LONG_SCROLL);
    passport_ui_set_active(false);assert(lv_label_get_long_mode(player_notice)==LV_LABEL_LONG_CLIP);
    passport_ui_set_active(true);assert(lv_label_get_long_mode(player_notice)==LV_LABEL_LONG_SCROLL);
    update.updating_catalog=false;update.playback_state="PAUSED";
    passport_ui_update(&update);lv_refr_now(display);
    assert(!strcmp(lv_label_get_text(player_notice),"已暂停") && lv_obj_get_width(player_notice)==100);
    puts("PASS highlighted return row and full catalog update notices in Settings/Player, sleep animation pause and transient notice restoration");
    update=(passport_view_t){.page=PASSPORT_PAGE_SETTINGS,.item_count=5,.selected=1,
        .titles={"回到当前播放","机核 FM","更新播客列表","Wi-Fi 配网","亮度：70%"},
        .row_highlight={true,true},.row_icon={1,2}};
    passport_ui_update(&update);lv_refr_now(display);
    FILE *fm_file=fopen("settings-fm.ppm","wb");assert(fm_file);
    fprintf(fm_file,"P6\n240 320\n255\n");fwrite(framebuffer,1,sizeof(framebuffer),fm_file);fclose(fm_file);
    update.page=PASSPORT_PAGE_PLAYER;update.fm=true;update.title="游戏里的声音与记忆";
    update.next_chapter="电台里的一封来信，游戏与生活的交点";
    update.channel="机核 FM";update.playback_state="READY";update.playing=true;
    passport_ui_update(&update);lv_refr_now(display);
    assert(lv_obj_is_hidden(label_at(lv_screen_active(),12,155)));
    assert(lv_obj_is_hidden(label_at(lv_screen_active(),120,155)));
    lv_obj_t *fm_prefix=label_at(lv_screen_active(),102,86);
    lv_obj_t *fm_upcoming=label_at(lv_screen_active(),102,102);
    assert(fm_prefix&&!strcmp(lv_label_get_text(fm_prefix),"即将播放"));
    assert(fm_upcoming&&!strcmp(lv_label_get_text(fm_upcoming),update.next_chapter));
    assert(lv_obj_get_width(fm_upcoming)==127&&lv_obj_get_height(fm_upcoming)==20);
    assert(lv_label_get_long_mode(fm_upcoming)==LV_LABEL_LONG_SCROLL);
    fm_file=fopen("player-fm.ppm","wb");assert(fm_file);
    fprintf(fm_file,"P6\n240 320\n255\n");fwrite(framebuffer,1,sizeof(framebuffer),fm_file);fclose(fm_file);
    uint8_t fm_bars[127*20*3];
    for(int y=0;y<20;y++) memcpy(fm_bars+y*127*3,framebuffer+((124+y)*240+102)*3,127*3);
    lv_tick_inc(300);lv_timer_handler();lv_refr_now(display);
    bool changed=false;
    for(int y=0;y<20;y++) if(memcmp(fm_bars+y*127*3,framebuffer+((124+y)*240+102)*3,127*3)) changed=true;
    assert(changed);
    for(int i=0;i<30;i++) {lv_tick_inc(100);lv_timer_handler();}
    int32_t fm_offset=((lv_label_t *)fm_upcoming)->offset.x;assert(fm_offset<0);
    passport_ui_update(&update);lv_refr_now(display);
    assert(((lv_label_t *)fm_upcoming)->offset.x==fm_offset);
    passport_ui_set_active(false);lv_refr_now(display);
    for(int y=0;y<20;y++) memcpy(fm_bars+y*127*3,framebuffer+((124+y)*240+102)*3,127*3);
    lv_tick_inc(1000);lv_timer_handler();lv_refr_now(display);
    for(int y=0;y<20;y++) assert(!memcmp(fm_bars+y*127*3,framebuffer+((124+y)*240+102)*3,127*3));
    passport_ui_set_active(true);update.playing=false;update.playback_state="PAUSED";
    passport_ui_update(&update);lv_refr_now(display);
    for(int y=0;y<20;y++) memcpy(fm_bars+y*127*3,framebuffer+((124+y)*240+102)*3,127*3);
    lv_tick_inc(1000);lv_timer_handler();lv_refr_now(display);
    for(int y=0;y<20;y++) assert(!memcmp(fm_bars+y*127*3,framebuffer+((124+y)*240+102)*3,127*3));
    update.title=long_title;update.next_chapter=long_title;
    passport_ui_update(&update);lv_refr_now(display);
    assert(!strcmp(lv_label_get_text(fm_upcoming),long_title));
    for(int i=0;i<8;i++) {
        update.fm=false;update.next_chapter="32:40 测试章节";passport_ui_update(&update);lv_refr_now(display);
        assert(label_at(lv_screen_active(),157,127));
        update.fm=true;update.next_chapter="下一节目测试";passport_ui_update(&update);lv_refr_now(display);
        assert(label_at(lv_screen_active(),102,102));assert(lv_mem_test()==LV_RESULT_OK);
    }
    lv_mem_monitor(&monitor);
    printf("PASS FM names, scrolling, 4 Hz activity, sleep/pause stop, repeated FM/podcast switch; pool used=%lu peak=%lu\n",
        (unsigned long)(monitor.total_size-monitor.free_size),(unsigned long)monitor.max_used);
    assert(lv_mem_test()==LV_RESULT_OK);
    return 0;
}
