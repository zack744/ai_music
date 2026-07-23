#include "ui.h"
#include "esp_log.h"
#include <stdio.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "lvgl_v8_port.h"
#include "net_helper.h"
#include "recorder.h"
#include "uploader.h"
#include "player.h"
#include "config.h"

static const char *TAG = "ui";

/* =====================================================================
 * ai_music UI (LVGL v8.4, 240x320 竖屏)
 * 按 Figma 设计稿实现:深色极光绿主题
 * 五页:主菜单 / 录音 / 生成中 / 播放 / 历史
 * ===================================================================== */

/* 生成字体在 LVGL 8 下是 const，位于 Flash 映射只读区。
 * fallback 属于字体描述符运行时字段，因此在 RAM 中保留可变副本。 */
static lv_font_t s_font_zh_16_runtime;
static lv_font_t s_font_zh_22_runtime;
static lv_font_t *f16 = &s_font_zh_16_runtime;
static lv_font_t *f22 = &s_font_zh_22_runtime;
static bool s_fonts_ready = false;

static void ensure_fonts_ready(void)
{
    if (s_fonts_ready) return;
    s_font_zh_16_runtime = lv_font_zh_16;
    s_font_zh_22_runtime = lv_font_zh_22;
    s_font_zh_16_runtime.fallback = &lv_font_montserrat_14;
    s_font_zh_22_runtime.fallback = &lv_font_montserrat_14;
    s_fonts_ready = true;
}

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
static lv_obj_t *scr_menu, *scr_record, *scr_gen, *scr_play, *scr_hist, *scr_settings;

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
static lv_obj_t *play_status, *play_time_cur, *play_time_tot = NULL;

/* ---- 演示数据 ---- */
static const char *cur_song = "Midnight Drift";
#define MAX_HIST 20
static struct { char name[64]; char url[128]; } g_hist[MAX_HIST];
static int g_hist_count = 0;
static lv_obj_t *hist_list = NULL;

/* ---- 录音/上传/播放数据 ---- */
static uint8_t *g_env_wav = NULL;
static size_t g_env_size = 0;
static uint8_t *g_speech_wav = NULL;
static size_t g_speech_size = 0;
static char g_output_url[300] = {0};
static bool g_gen_ok = false;
static TaskHandle_t s_bg_task = NULL;

/* ---- 播放目标(生成结果 或 历史选中) ---- */
static char g_play_url[320] = {0};

/* ---- 设置页运行时 ---- */
static lv_obj_t *set_ip_label = NULL;
static char set_ip_buf[40] = {0};
static const char *numpad_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    ".", "0", LV_SYMBOL_BACKSPACE, "\n",
    "Save", "Back", ""
};

/* ---- 前置声明 ---- */
static void goto_menu(lv_event_t *e);
static void goto_record(lv_event_t *e);
static void goto_gen(lv_event_t *e);
static void goto_play(lv_event_t *e);
static void goto_hist(lv_event_t *e);
static void goto_settings(lv_event_t *e);
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

    /* 右上角设置按钮 */
    lv_obj_t *bSet = new_btn(scr_menu, LV_SYMBOL_SETTINGS, goto_settings,
                             36, 36, C_DARKER, C_ICON_INA, f16, 8, false);
    lv_obj_set_pos(bSet, 196, 4);

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
static void rec_progress_cb(int sec)
{
    lvgl_port_lock(-1);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", sec);
    lv_label_set_text(rec_timer_label, buf);
    lvgl_port_unlock();
}

static void rec_task(void *arg)
{
    size_t wav_size = 0;
    uint8_t *wav = recorder_record(REC_DURATION_SEC, &wav_size, rec_progress_cb);

    lvgl_port_lock(-1);
    if (wav) {
        if (rec_step == 1) { if (g_env_wav) free(g_env_wav); g_env_wav = wav; g_env_size = wav_size; }
        else               { if (g_speech_wav) free(g_speech_wav); g_speech_wav = wav; g_speech_size = wav_size; }
        lv_label_set_text(rec_step_label, rec_step == 1 ? "环境音已录" : "心里话已录");
        lv_label_set_text(rec_start_label, "已录制");
    } else {
        lv_label_set_text(rec_step_label, "录音失败");
        lv_label_set_text(rec_start_label, LV_SYMBOL_PLAY "  开始");
    }
    rec_recording = false;
    lvgl_port_unlock();
    s_bg_task = NULL;
    vTaskDelete(NULL);
}

static void rec_toggle_cb(lv_event_t *e)
{
    (void)e;
    if (rec_recording) {
        recorder_stop();
        return;
    }
    if (s_bg_task) return;
    rec_recording = true;
    lv_label_set_text(rec_step_label, "录音中...");
    lv_label_set_text(rec_start_label, LV_SYMBOL_STOP "  停");
    lv_label_set_text(rec_timer_label, "0");
    recorder_init();
    xTaskCreatePinnedToCore(rec_task, "rec", 8192, NULL, 1, &s_bg_task, 0);
}

static void rec_next_cb(lv_event_t *e)
{
    if (rec_recording || s_bg_task) return;
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
    lv_label_set_text(rec_timer_label, "0");
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
    rec_timer_label = new_label(circle, "0", f22, C_GREEN);
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
static void gen_finish_cb(lv_event_t *e)
{
    if (g_gen_ok) {
        strncpy(g_play_url, g_output_url, sizeof(g_play_url) - 1);
        g_play_url[sizeof(g_play_url) - 1] = 0;
        cur_song = "New Song";
        goto_play(e);
    } else {
        goto_menu(e);
    }
}

static void upload_task(void *arg)
{
    bool ok = uploader_upload(g_env_wav, g_env_size,
                              g_speech_wav, g_speech_size,
                              NULL, 30, g_output_url, sizeof(g_output_url));
    lvgl_port_lock(-1);
    g_gen_ok = ok;
    lv_label_set_text(gen_status, ok ? "已生成" : "生成失败");
    lv_obj_clear_flag(gen_btn, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    s_bg_task = NULL;
    vTaskDelete(NULL);
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
    gen_btn = new_btn(scr_gen, "完成", gen_finish_cb, lv_pct(85), 48, C_GREEN, C_DKGREEN, f22, 8, true);
    lv_obj_add_flag(gen_btn, LV_OBJ_FLAG_HIDDEN);
}

/* ===================== 播放页 ===================== */
static void fmt_time(char *buf, size_t n, int ms)
{
    if (ms < 0) ms = 0;
    int sec = ms / 1000;
    snprintf(buf, n, "%d:%02d", sec / 60, sec % 60);
}

/* 下载进度回调(后台任务上下文) */
static void dl_progress_cb(int pct)
{
    lvgl_port_lock(-1);
    char buf[24];
    snprintf(buf, sizeof(buf), "下载中 %d%%", pct);
    lv_label_set_text(play_status, buf);
    lv_bar_set_value(play_bar, pct, LV_ANIM_OFF);
    lvgl_port_unlock();
}

static void play_ui_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_scr_act() != scr_play) return;
    player_state_t st = player_state();
    int el = player_elapsed_ms();
    int du = player_duration_ms();
    char buf[16];

    if (st == PLAYER_PLAYING || st == PLAYER_PAUSED || st == PLAYER_FINISHED) {
        int pct = (du > 0) ? el * 100 / du : 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(play_bar, pct, LV_ANIM_OFF);
        fmt_time(buf, sizeof(buf), el);
        lv_label_set_text(play_time_cur, buf);
        fmt_time(buf, sizeof(buf), du);
        lv_label_set_text(play_time_tot, buf);
    }
    if (st == PLAYER_DOWNLOADING) {
        lv_label_set_text(play_status, "下载中...");
    } else if (st == PLAYER_PLAYING) {
        lv_label_set_text(play_status, "");
        lv_label_set_text(play_btn_label, LV_SYMBOL_PAUSE "  暂停");
    } else if (st == PLAYER_PAUSED) {
        lv_label_set_text(play_status, "已暂停");
        lv_label_set_text(play_btn_label, LV_SYMBOL_PLAY "  播放");
    } else if (st == PLAYER_FINISHED) {
        lv_label_set_text(play_status, "播放完成");
        lv_label_set_text(play_btn_label, LV_SYMBOL_REFRESH "  重播");
    } else if (st == PLAYER_ERROR) {
        lv_label_set_text(play_status, "播放失败");
        lv_label_set_text(play_btn_label, LV_SYMBOL_REFRESH "  重试");
    } else if (st == PLAYER_DOWNLOADING) {
        lv_label_set_text(play_status, "下载中...");
    }
}

/* 后台任务: 下载 mp3 并开始播放 */
static void play_task(void *arg)
{
    (void)arg;
    char url[320];
    strncpy(url, g_play_url, sizeof(url) - 1);
    url[sizeof(url) - 1] = 0;
    bool ok = player_play(url, dl_progress_cb);
    lvgl_port_lock(-1);
    if (!ok) lv_label_set_text(play_status, "播放失败");
    s_bg_task = NULL;
    lvgl_port_unlock();
    vTaskDelete(NULL);
}

static void start_playback(void)
{
    if (s_bg_task) return;
    lv_label_set_text(play_status, "下载中...");
    lv_bar_set_value(play_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(play_time_cur, "0:00");
    lv_label_set_text(play_time_tot, "0:00");
    xTaskCreatePinnedToCore(play_task, "play", 12288, NULL, 1, &s_bg_task, 0);
}

static void play_toggle_cb(lv_event_t *e)
{
    (void)e;
    player_state_t st = player_state();
    if (st == PLAYER_PLAYING) {
        player_pause();
    } else if (st == PLAYER_PAUSED) {
        player_resume();
    } else if ((st == PLAYER_FINISHED || st == PLAYER_ERROR) && g_play_url[0]) {
        player_stop();
        start_playback();
    }
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
    lv_obj_set_size(disc, 130, 130);
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
    play_name = new_label(scr_play, "-", f22, C_TEXT_LT);

    /* 状态行(下载进度/暂停/完成/失败) */
    play_status = new_label(scr_play, "", f16, C_TEXT_GG);

    /* 进度条 */
    play_bar = lv_bar_create(scr_play);
    lv_obj_set_size(play_bar, 200, 6);
    lv_bar_set_range(play_bar, 0, 100);
    lv_bar_set_value(play_bar, 0, LV_ANIM_OFF);
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
    play_time_cur = new_label(time_row, "0:00", f16, C_TEXT_GG);
    play_time_tot = new_label(time_row, "0:00", f16, C_TEXT_GG);

    /* 控制按钮 */
    lv_obj_t *ctrl = new_box(scr_play, lv_pct(100), 50);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl, 8, 0);
    lv_obj_t *bPlay = new_btn(ctrl, LV_SYMBOL_PAUSE "  暂停", play_toggle_cb, 100, 44, C_GREEN_BR, C_GREEN_DK, f16, 8, false);
    play_btn_label = lv_obj_get_child(bPlay, 0);
    new_btn(ctrl, "返回", goto_hist, 100, 44, C_GRAYBTN, C_TEXT_LT, f16, 8, false);

    /* UI 刷新定时器(250ms, 只在播放页激活时生效) */
    lv_timer_create(play_ui_timer_cb, 250, NULL);
}

/* ===================== 历史页 ===================== */
static void hist_item_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_hist_count) {
        cur_song = g_hist[idx].name;
        /* 后端可能返回相对路径(/outputs/x.mp3), 补成绝对 URL */
        const char *u = g_hist[idx].url;
        if (u[0] == '/') {
            snprintf(g_play_url, sizeof(g_play_url), "http://%s:5000%s",
                     network_server_ip(), u);
        } else {
            strncpy(g_play_url, u, sizeof(g_play_url) - 1);
            g_play_url[sizeof(g_play_url) - 1] = 0;
        }
    }
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

/* ===================== 设置页 ===================== */
static void set_numpad_cb(lv_event_t *e)
{
    lv_obj_t *btnm = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(btnm);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char *txt = lv_btnmatrix_get_btn_text(btnm, id);
    if (!txt) return;

    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        size_t len = strlen(set_ip_buf);
        if (len > 0) set_ip_buf[len - 1] = 0;
    } else if (strcmp(txt, "Save") == 0) {
        if (strlen(set_ip_buf) > 0) {
            network_set_server_ip(set_ip_buf);
            ESP_LOGI(TAG, "server IP saved: %s", set_ip_buf);
        }
        goto_menu(e);
        return;
    } else if (strcmp(txt, "Back") == 0) {
        goto_menu(e);
        return;
    } else {
        size_t len = strlen(set_ip_buf);
        if (len < sizeof(set_ip_buf) - 1) {
            set_ip_buf[len] = txt[0];
            set_ip_buf[len + 1] = 0;
        }
    }
    lv_label_set_text(set_ip_label, set_ip_buf);
}

static void __attribute__((noinline, optimize("O2"))) create_settings(void)
{
    scr_settings = lv_obj_create(NULL);
    set_bg(scr_settings, C_BG);
    lv_obj_set_style_pad_all(scr_settings, 0, 0);

    /* 标题 */
    lv_obj_t *title = new_label(scr_settings, "Server IP", f22, C_GREEN);
    lv_obj_set_pos(title, 16, 10);

    /* IP 显示框 */
    lv_obj_t *box = new_box(scr_settings, 208, 44);
    lv_obj_set_pos(box, 16, 42);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, C_GREEN, 0);
    lv_obj_set_style_radius(box, 8, 0);
    set_ip_label = new_label(box, "", f22, C_TEXT_LT);
    lv_obj_align(set_ip_label, LV_ALIGN_LEFT_MID, 8, 0);

    /* 数字键盘 */
    lv_obj_t *btnm = lv_btnmatrix_create(scr_settings);
    lv_obj_set_size(btnm, 208, 210);
    lv_obj_set_pos(btnm, 16, 96);
    lv_btnmatrix_set_map(btnm, numpad_map);
    lv_btnmatrix_set_btn_width(btnm, 12, 2);  /* Save 宽 2 */
    lv_btnmatrix_set_btn_width(btnm, 13, 2);  /* Back 宽 2 */
    lv_obj_add_event_cb(btnm, set_numpad_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 键盘样式 */
    lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnm, 0, 0);
    lv_obj_set_style_pad_all(btnm, 0, 0);
    lv_obj_set_style_pad_gap(btnm, 4, 0);
    lv_obj_set_style_bg_color(btnm, C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, C_GREEN, LV_PART_ITEMS);
    lv_obj_set_style_text_font(btnm, f22, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(btnm, 8, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(btnm, 0, LV_PART_ITEMS);
}

/* ===================== 页面跳转 ===================== */
static void goto_menu(lv_event_t *e)
{
    (void)e;
    if (rec_recording || s_bg_task) return;  /* 录音/上传/下载中不允许返回 */
    player_stop_async();
    rec_recording = false;
    if (rec_start_label) lv_label_set_text(rec_start_label, LV_SYMBOL_PLAY "  开始");
    lv_scr_load(scr_menu);
}

static void goto_record(lv_event_t *e)
{
    (void)e;
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
    lv_label_set_text(gen_status, "上传生成中...");
    lv_obj_add_flag(gen_btn, LV_OBJ_FLAG_HIDDEN);
    lv_scr_load(scr_gen);
    player_init();
    xTaskCreatePinnedToCore(upload_task, "upload", 16384, NULL, 1, &s_bg_task, 0);
}

static void goto_play(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "-> play");
    /* play 在后台任务里会 player_stop() 同步等待异步清理完成 */
    player_stop_async();
    lv_label_set_text(play_name, cur_song);
    lv_label_set_text(play_status, g_play_url[0] ? "下载中..." : "无歌曲");
    lv_label_set_text(play_btn_label, LV_SYMBOL_PAUSE "  暂停");
    lv_bar_set_value(play_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(play_time_cur, "0:00");
    lv_label_set_text(play_time_tot, "0:00");
    lv_scr_load(scr_play);
    if (g_play_url[0]) start_playback();
}

/* 用已解析的 g_hist 填充列表（须在 LVGL 锁内调用） */
static void apply_hist_list(void)
{
    lv_obj_clean(hist_list);
    if (g_hist_count == 0) {
        lv_list_add_text(hist_list, "暂无歌曲");
        return;
    }
    for (int i = 0; i < g_hist_count; i++) {
        lv_obj_t *btn = lv_list_add_btn(hist_list, LV_SYMBOL_AUDIO, g_hist[i].name);
        lv_obj_add_event_cb(btn, hist_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_style_bg_color(btn, C_CARD, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn, C_WHITE, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_t *lab = lv_obj_get_child(btn, 0);
        lv_obj_set_style_text_font(lab, f16, 0);
    }
}

/* 后台拉历史，避免在 LVGL 事件里阻塞 HTTP + free 大缓冲后立刻建表 */
static void hist_fetch_task(void *arg)
{
    (void)arg;
    char *json = network_fetch_history();
    int count = 0;
    const char *err_msg = NULL;

    if (!json) {
        err_msg = "获取历史失败";
    } else {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json);
        free(json);
        if (err) {
            err_msg = "解析失败";
        } else {
            JsonArray songs = doc["songs"].as<JsonArray>();
            for (JsonObject song : songs) {
                if (count >= MAX_HIST) break;
                const char *name = song["name"];
                const char *url = song["url"];
                if (!name || !url) continue;
                strncpy(g_hist[count].name, name, 63);
                strncpy(g_hist[count].url, url, 127);
                g_hist[count].name[63] = 0;
                g_hist[count].url[127] = 0;
                count++;
            }
        }
    }

    lvgl_port_lock(-1);
    g_hist_count = count;
    if (err_msg) {
        lv_obj_clean(hist_list);
        lv_list_add_text(hist_list, err_msg);
    } else {
        apply_hist_list();
    }
    s_bg_task = NULL;
    lvgl_port_unlock();
    vTaskDelete(NULL);
}

static void goto_hist(lv_event_t *e)
{
    (void)e;
    if (s_bg_task) return;  /* 下载/上传/拉历史中不允许离开 */
    player_stop_async();  /* 不堵 UI：停声+后台 free，mutex/软停仍在 */
    lv_obj_clean(hist_list);
    lv_list_add_text(hist_list, "加载中...");
    lv_scr_load(scr_hist);
    xTaskCreatePinnedToCore(hist_fetch_task, "hist", 16384, NULL, 1, &s_bg_task, 0);
}

static void goto_settings(lv_event_t *e)
{
    (void)e;
    const char *cur = network_server_ip();
    strncpy(set_ip_buf, cur, sizeof(set_ip_buf) - 1);
    set_ip_buf[sizeof(set_ip_buf) - 1] = 0;
    if (set_ip_label) lv_label_set_text(set_ip_label, set_ip_buf);
    lv_scr_load(scr_settings);
}

/* ===================== 启动状态显示 ===================== */
void __attribute__((noinline, optimize("O2"))) ui_show_boot_msg(const char *msg)
{
    ensure_fonts_ready();
    lv_obj_t *scr = lv_scr_act();
    set_bg(scr, C_BG);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clean(scr);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, msg);
    lv_obj_set_style_text_font(label, f22, 0);
    lv_obj_set_style_text_color(label, C_GREEN, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

/* ===================== 入口 ===================== */
void ui_create(void)
{
    /* 启动状态页可能先于正式 UI 使用字体。 */
    ensure_fonts_ready();

    create_menu();
    create_record();
    create_gen();
    create_play();
    create_hist();
    create_settings();
    lv_scr_load(scr_menu);
}
