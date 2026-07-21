#include "ui.h"
#include "esp_log.h"
#include <stdio.h>
#include <ArduinoJson.h>
#include "network.h"

static const char *TAG = "ui";

/* =====================================================================
 * ai_music UI (LVGL v8.4, 240x320 竖屏)
 * 按 Figma 设计稿实现:深色极光绿主题
 * 五页:主菜单 / 录音 / 生成中 / 播放 / 历史
 * ===================================================================== */

static lv_font_t *f16 = &lv_font_zh_16;
static lv_font_t *f22 = &lv_font_zh_22;

/* ---- 配色 (from Figma) ---- */
static const lv_color_t C_BG       = LV_COLOR_MAKE(0x12, 0x14, 0x14);
static const lv_color_t C_BLACK    = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static const lv_color_t C_GREEN    = LV_COLOR_MAKE(0x00, 0xE3, 0x8B);
static const lv_color_t C_GREEN_BR = LV_COLOR_MAKE(0x00, 0xFF, 0x9D);
static const lv_color_t C_GREEN_FL = LV_COLOR_MAKE(0x56, 0xFF, 0xA8);
static const lv_color_t C_DKGREEN  = LV_COLOR_MAKE(0x00, 0x21, 0x10);
static const lv_color_t C_GRAYBTN  = LV_COLOR_MAKE(0x33, 0x35, 0x35);
static const lv_color_t C_CARD     = LV_COLOR_MAKE(0x28, 0x2A, 0x2B);
static const lv_color_t C_STROKE   = LV_COLOR_MAKE(0x3B, 0x4A, 0x3F);
static const lv_color_t C_WHITE    = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);
static const lv_color_t C_TEXT_LT  = LV_COLOR_MAKE(0xE2, 0xE2, 0xE2);
static const lv_color_t C_TEXT_GG  = LV_COLOR_MAKE(0xB9, 0xCB, 0xBC);
static const lv_color_t C_ICON_INA = LV_COLOR_MAKE(0x9C, 0xA3, 0xAF);
static const lv_color_t C_GREEN_DK = LV_COLOR_MAKE(0x00, 0x71, 0x43);
static const lv_color_t C_DARKER   = LV_COLOR_MAKE(0x0C, 0x0F, 0x0F);
static const lv_color_t C_MUTED_G  = LV_COLOR_MAKE(0x84, 0x95, 0x87);
static const lv_color_t C_INNER    = LV_COLOR_MAKE(0x1A, 0x1C, 0x1C);

/* ---- screens ---- */
static lv_obj_t *scr_menu, *scr_record, *scr_gen, *scr_play, *scr_hist;

/* ---- 录音页运行时控件 ---- */
static lv_obj_t *rec_step_label, *rec_pill_label, *rec_timer_label;
static lv_obj_t *rec_start_label = NULL;
static bool rec_recording = false;
static uint8_t rec_step = 1;
static int rec_countdown = 30;
static lv_timer_t *rec_timer = NULL;

/* ---- 生成中页运行时控件 ---- */
static lv_obj_t *gen_status, *gen_btn;
static lv_timer_t *gen_timer = NULL;

/* ---- 播放页运行时控件 ---- */
static lv_obj_t *play_name, *play_bar, *play_btn_label = NULL;

/* ---- 演示数据 ---- */
static const char *cur_song = "Midnight Drift";
#define MAX_HIST 20
static struct { char name[64]; char url[128]; } g_hist[MAX_HIST];
static int g_hist_count = 0;
static lv_obj_t *hist_list = NULL;

/* ---- 前置声明 ---- */
static void goto_menu(lv_event_t *e);
static void goto_record(lv_event_t *e);
static void goto_gen(lv_event_t *e);
static void goto_play(lv_event_t *e);
static void goto_hist(lv_event_t *e);
static void update_record_page(void);

/* ===================== 辅助 ===================== */
static void set_bg(lv_obj_t *o, lv_color_t c)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *new_label(lv_obj_t *p, const char *txt, lv_font_t *f, lv_color_t color)
{
    lv_obj_t *lab = lv_label_create(p);
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_font(lab, f, 0);
    lv_obj_set_style_text_color(lab, color, 0);
    return lab;
}

static lv_obj_t * __attribute__((noinline, optimize("O2"))) new_btn(lv_obj_t *p, const char *txt, lv_event_cb_t cb,
                         lv_coord_t w, lv_coord_t h, lv_color_t bg, lv_color_t txt_color,
                         lv_font_t *f, lv_coord_t radius, bool glow)
{
    lv_obj_t *btn = lv_btn_create(p);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, radius, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    (void)glow;  /* 阴影已移除(避免 Cache panic) */
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_font(lab, f, 0);
    lv_obj_set_style_text_color(lab, txt_color, 0);
    lv_obj_center(lab);
    return btn;
}

/* 容器:无背景边框内边距,用于布局分组 */
static lv_obj_t *new_box(lv_obj_t *p, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *box = lv_obj_create(p);
    lv_obj_set_size(box, w, h);
    set_bg(box, C_BG);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    return box;
}

/* ===================== 主菜单 ===================== */
static void create_menu(void)
{
    scr_menu = lv_obj_create(NULL);
    set_bg(scr_menu, C_BG);
    lv_obj_set_style_pad_all(scr_menu, 0, 0);

    /* 顶部标题栏 */
    lv_obj_t *header = new_box(scr_menu, 240, 44);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, C_GRAYBTN, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_t *t = new_label(header, "AI音乐", f22, C_GREEN);
    lv_obj_center(t);

    /* 生成歌曲 */
    lv_obj_t *b1 = new_btn(scr_menu, LV_SYMBOL_AUDIO "  生成歌曲", goto_record,
                           208, 64, C_GREEN, C_BLACK, f22, 12, true);
    lv_obj_set_pos(b1, 16, 80);

    /* 历史歌曲 */
    lv_obj_t *b2 = new_btn(scr_menu, LV_SYMBOL_LIST "  历史歌曲", goto_hist,
                           208, 64, C_GRAYBTN, C_WHITE, f22, 12, false);
    lv_obj_set_style_border_width(b2, 1, 0);
    lv_obj_set_style_border_color(b2, C_STROKE, 0);
    lv_obj_set_pos(b2, 16, 160);

    /* 底部导航栏 */
    lv_obj_t *navbar = new_box(scr_menu, 240, 56);
    lv_obj_set_pos(navbar, 0, 264);
    lv_obj_set_style_border_side(navbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(navbar, C_GRAYBTN, 0);
    lv_obj_set_style_border_width(navbar, 1, 0);
    lv_obj_set_flex_flow(navbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(navbar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    new_btn(navbar, LV_SYMBOL_HOME, goto_menu, 60, 44, C_GREEN_BR, C_BLACK, f16, 8, false);
    lv_obj_t *n2 = new_btn(navbar, LV_SYMBOL_AUDIO, goto_record, 60, 44, C_DARKER, C_ICON_INA, f16, 8, false);
    lv_obj_t *n3 = new_btn(navbar, LV_SYMBOL_LIST, goto_hist, 60, 44, C_DARKER, C_ICON_INA, f16, 8, false);
}

/* ===================== 录音页 ===================== */
static void rec_countdown_cb(lv_timer_t *t)
{
    rec_countdown--;
    if (rec_countdown <= 0) {
        lv_label_set_text(rec_timer_label, "0");
        lv_label_set_text(rec_step_label, "已录制");
        lv_label_set_text(rec_start_label, LV_SYMBOL_PLAY "  开始");
        rec_recording = false;
        rec_timer = NULL;
        if (rec_step == 1) { rec_step = 2; update_record_page(); }
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", rec_countdown);
    lv_label_set_text(rec_timer_label, buf);
}

static void rec_toggle_cb(lv_event_t *e)
{
    (void)e;
    if (!rec_recording) {
        rec_recording = true;
        rec_countdown = 30;
        lv_label_set_text(rec_step_label, "录音中...");
        lv_label_set_text(rec_timer_label, "30");
        lv_label_set_text(rec_start_label, LV_SYMBOL_STOP "  结束");
        if (rec_timer) lv_timer_del(rec_timer);
        rec_timer = lv_timer_create(rec_countdown_cb, 1000, NULL);
        lv_timer_set_repeat_count(rec_timer, 30);
    } else {
        rec_recording = false;
        if (rec_timer) { lv_timer_del(rec_timer); rec_timer = NULL; }
        lv_label_set_text(rec_step_label, "已录制");
        lv_label_set_text(rec_start_label, LV_SYMBOL_PLAY "  开始");
        if (rec_step == 1) { rec_step = 2; rec_countdown = 30; update_record_page(); }
    }
}

static void rec_next_cb(lv_event_t *e)
{
    if (rec_timer) { lv_timer_del(rec_timer); rec_timer = NULL; }
    rec_recording = false;
    lv_label_set_text(rec_start_label, LV_SYMBOL_PLAY "  开始");
    if (rec_step == 1) {
        rec_step = 2;
        update_record_page();
    } else {
        goto_gen(e);
    }
}

static void update_record_page(void)
{
    if (rec_step == 1) {
        lv_label_set_text(rec_step_label, "录环境音");
        lv_label_set_text(rec_pill_label, "1/2");
    } else {
        lv_label_set_text(rec_step_label, "录心里话");
        lv_label_set_text(rec_pill_label, "2/2");
    }
    lv_label_set_text(rec_timer_label, "30");
}

static void create_record(void)
{
    scr_record = lv_obj_create(NULL);
    set_bg(scr_record, C_BG);
    lv_obj_set_style_pad_all(scr_record, 0, 0);

    /* 标题 */
    rec_step_label = new_label(scr_record, "录环境音", f16, C_TEXT_GG);
    lv_obj_set_pos(rec_step_label, 70, 28);

    /* 胶囊 1/2 */
    lv_obj_t *pill = lv_obj_create(scr_record);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    set_bg(pill, C_CARD);
    lv_obj_set_style_radius(pill, 999, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, C_STROKE, 0);
    lv_obj_set_style_pad_ver(pill, 2, 0);
    lv_obj_set_style_pad_hor(pill, 8, 0);
    lv_obj_set_pos(pill, 160, 24);
    rec_pill_label = new_label(pill, "1/2", f16, C_GREEN);

    /* 倒计时圆 */
    lv_obj_t *circle = lv_obj_create(scr_record);
    lv_obj_set_size(circle, 120, 120);
    set_bg(circle, C_INNER);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 2, 0);
    lv_obj_set_style_border_color(circle, C_GREEN, 0);
    /* 阴影已移除 */
    lv_obj_set_pos(circle, 60, 70);
    rec_timer_label = new_label(circle, "30", f22, C_GREEN);
    lv_obj_center(rec_timer_label);

    /* 开始/结束 按钮 */
    lv_obj_t *bStart = new_btn(scr_record, LV_SYMBOL_PLAY "  开始", rec_toggle_cb,
                               208, 48, C_GREEN, C_DKGREEN, f22, 8, true);
    lv_obj_set_pos(bStart, 16, 200);
    rec_start_label = lv_obj_get_child(bStart, 0);

    /* 取消 / 下一步 */
    lv_obj_t *bCancel = new_btn(scr_record, "取消", goto_menu, 100, 44, C_GRAYBTN, C_TEXT_LT, f16, 8, false);
    lv_obj_set_pos(bCancel, 16, 262);
    lv_obj_t *bNext = new_btn(scr_record, "下一步", rec_next_cb, 100, 44, C_DARKER, C_MUTED_G, f16, 8, false);
    lv_obj_set_style_border_width(bNext, 1, 0);
    lv_obj_set_style_border_color(bNext, C_STROKE, 0);
    lv_obj_set_pos(bNext, 124, 262);
}

/* ===================== 生成中 ===================== */
static void gen_timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_label_set_text(gen_status, "已生成");
    lv_obj_clear_flag(gen_btn, LV_OBJ_FLAG_HIDDEN);
    gen_timer = NULL;
}

static void create_gen(void)
{
    scr_gen = lv_obj_create(NULL);
    set_bg(scr_gen, C_BG);
    lv_obj_set_flex_flow(scr_gen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_gen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr_gen, 16, 0);

    /* 旋转动画 */
    lv_obj_t *sp = lv_spinner_create(scr_gen, 1000, 60);
    lv_obj_set_size(sp, 100, 100);
    lv_obj_set_style_border_width(sp, 0, 0);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_color(sp, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sp, 4, LV_PART_INDICATOR);
    /* 阴影已移除 */

    gen_status = new_label(scr_gen, "创作中...", f22, C_GREEN);
    new_label(scr_gen, "AI 正在生成音乐...", f16, C_TEXT_GG);

    /* 完成按钮:初始隐藏,生成结束后由定时器显示 */
    gen_btn = new_btn(scr_gen, "完成", goto_play, lv_pct(85), 48, C_GREEN, C_DKGREEN, f22, 8, true);
    lv_obj_add_flag(gen_btn, LV_OBJ_FLAG_HIDDEN);
}

/* ===================== 播放页 ===================== */
static void play_toggle_cb(lv_event_t *e)
{
    (void)e;
    static bool paused = false;
    paused = !paused;
    lv_label_set_text(play_name, paused ? "已暂停" : cur_song);
    if (play_btn_label)
        lv_label_set_text(play_btn_label, paused ? LV_SYMBOL_PLAY "  播放" : LV_SYMBOL_PAUSE "  暂停");
}

static void create_play(void)
{
    scr_play = lv_obj_create(NULL);
    set_bg(scr_play, C_BG);
    lv_obj_set_flex_flow(scr_play, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_play, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(scr_play, 12, 0);
    lv_obj_set_style_pad_bottom(scr_play, 16, 0);

    /* 标题 */
    new_label(scr_play, "正在播放", f16, C_GREEN);

    /* 唱片圆 */
    lv_obj_t *disc = lv_obj_create(scr_play);
    lv_obj_set_size(disc, 150, 150);
    set_bg(disc, C_CARD);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(disc, 4, 0);
    lv_obj_set_style_border_color(disc, C_CARD, 0);
    lv_obj_set_style_shadow_width(disc, 0, 0);
    lv_obj_t *center = lv_obj_create(disc);
    lv_obj_set_size(center, 40, 40);
    set_bg(center, C_GREEN_BR);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    /* 阴影已移除 */
    lv_obj_center(center);

    /* 歌名 */
    play_name = new_label(scr_play, "Midnight Drift", f22, C_TEXT_LT);

    /* 进度条 */
    play_bar = lv_bar_create(scr_play);
    lv_obj_set_size(play_bar, 200, 6);
    lv_bar_set_range(play_bar, 0, 100);
    lv_bar_set_value(play_bar, 25, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(play_bar, C_GRAYBTN, 0);
    lv_obj_set_style_bg_opa(play_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(play_bar, 999, 0);
    lv_obj_set_style_bg_color(play_bar, C_GREEN_FL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(play_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(play_bar, 999, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(play_bar, 0, LV_PART_INDICATOR);

    /* 时间行 */
    lv_obj_t *time_row = new_box(scr_play, 210, 16);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    new_label(time_row, "0:45", f16, C_TEXT_GG);
    new_label(time_row, "3:20", f16, C_TEXT_GG);

    /* 控制按钮 */
    lv_obj_t *ctrl = new_box(scr_play, lv_pct(100), 50);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl, 8, 0);
    lv_obj_t *bPlay = new_btn(ctrl, LV_SYMBOL_PAUSE "  暂停", play_toggle_cb, 100, 44, C_GREEN_BR, C_GREEN_DK, f16, 8, false);
    play_btn_label = lv_obj_get_child(bPlay, 0);
    new_btn(ctrl, "返回", goto_hist, 100, 44, C_GRAYBTN, C_TEXT_LT, f16, 8, false);
}

/* ===================== 历史页 ===================== */
static void hist_item_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_hist_count)
        cur_song = g_hist[idx].name;
    goto_play(e);
}

static void create_hist(void)
{
    scr_hist = lv_obj_create(NULL);
    set_bg(scr_hist, C_BG);
    lv_obj_set_flex_flow(scr_hist, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_hist, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr_hist, 12, 0);
    lv_obj_set_style_pad_top(scr_hist, 16, 0);
    lv_obj_set_style_pad_bottom(scr_hist, 16, 0);

    /* 标题 */
    new_label(scr_hist, "我的歌曲", f22, C_GREEN);

    /* 列表(初始空,goto_hist 时从 API 填充) */
    hist_list = lv_list_create(scr_hist);
    lv_obj_set_size(hist_list, lv_pct(92), 190);
    lv_obj_set_style_bg_color(hist_list, C_BG, 0);
    lv_obj_set_style_bg_opa(hist_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hist_list, 0, 0);
    lv_obj_set_style_pad_row(hist_list, 4, 0);

    /* 返回(胶囊) */
    new_btn(scr_hist, LV_SYMBOL_LEFT "  返回", goto_menu,
            lv_pct(85), 48, C_GRAYBTN, C_WHITE, f22, 999, false);
}

/* ===================== 页面跳转 ===================== */
static void goto_menu(lv_event_t *e)
{
    (void)e;
    if (rec_timer) { lv_timer_del(rec_timer); rec_timer = NULL; }
    rec_recording = false;
    if (rec_start_label) lv_label_set_text(rec_start_label, LV_SYMBOL_PLAY "  开始");
    lv_scr_load(scr_menu);
}

static void goto_record(lv_event_t *e)
{
    (void)e;
    if (rec_timer) { lv_timer_del(rec_timer); rec_timer = NULL; }
    rec_step = 1;
    rec_recording = false;
    if (rec_start_label) lv_label_set_text(rec_start_label, LV_SYMBOL_PLAY "  开始");
    update_record_page();
    lv_scr_load(scr_record);
}

static void goto_gen(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "-> gen");
    lv_label_set_text(gen_status, "创作中...");
    lv_obj_add_flag(gen_btn, LV_OBJ_FLAG_HIDDEN);
    if (gen_timer) { lv_timer_del(gen_timer); gen_timer = NULL; }
    gen_timer = lv_timer_create(gen_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(gen_timer, 1);
    lv_scr_load(scr_gen);
}

static void goto_play(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "-> play");
    lv_label_set_text(play_name, cur_song);
    lv_bar_set_value(play_bar, 25, LV_ANIM_OFF);
    lv_scr_load(scr_play);
}

static void fill_hist_list(void)
{
    lv_obj_clean(hist_list);
    char *json = network_fetch_history();
    if (!json) {
        lv_list_add_text(hist_list, "获取历史失败");
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    free(json);
    if (err) {
        lv_list_add_text(hist_list, "解析失败");
        return;
    }
    g_hist_count = 0;
    JsonArray songs = doc["songs"].as<JsonArray>();
    for (JsonObject song : songs) {
        if (g_hist_count >= MAX_HIST) break;
        const char *name = song["name"];
        const char *url = song["url"];
        if (!name || !url) continue;
        strncpy(g_hist[g_hist_count].name, name, 63);
        strncpy(g_hist[g_hist_count].url, url, 127);
        g_hist[g_hist_count].name[63] = 0;
        g_hist[g_hist_count].url[127] = 0;
        lv_obj_t *btn = lv_list_add_btn(hist_list, LV_SYMBOL_AUDIO, g_hist[g_hist_count].name);
        lv_obj_add_event_cb(btn, hist_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)g_hist_count);
        lv_obj_set_style_bg_color(btn, C_CARD, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn, C_WHITE, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_t *lab = lv_obj_get_child(btn, 0);
        lv_obj_set_style_text_font(lab, f16, 0);
        g_hist_count++;
    }
    if (g_hist_count == 0)
        lv_list_add_text(hist_list, "暂无歌曲");
}

static void goto_hist(lv_event_t *e)
{
    (void)e;
    lv_scr_load(scr_hist);
    fill_hist_list();
}

/* ===================== 入口 ===================== */
void ui_create(void)
{
    /* 中文符号回退到 montserrat_14(LVGL 内置,含 FontAwesome 符号) */
    lv_font_zh_16.fallback = &lv_font_montserrat_14;
    lv_font_zh_22.fallback = &lv_font_montserrat_14;

    create_menu();
    create_record();
    create_gen();
    create_play();
    create_hist();
    lv_scr_load(scr_menu);
}
