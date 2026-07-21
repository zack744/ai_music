#include "display.h"
#include "config.h"
#include <math.h>

// ---- 调色板 (RGB565) ----
static const uint16_t C_BG     = 0x0000;  // 黑底
static const uint16_t C_PANEL  = 0x2124;  // 深灰蓝（状态栏/提示栏）
static const uint16_t C_ACCENT = 0x07FF;  // 青（高亮）
static const uint16_t C_OK     = 0x07E0;  // 绿（播放/确认）
static const uint16_t C_AMBER  = 0xFD20;  // 琥珀（录音/倒计时）
static const uint16_t C_TEXT   = 0xFFFF;  // 白
static const uint16_t C_MUTED  = 0x8410;  // 灰

// Arduino_GFX 无 textWidth()，用 getTextBounds 取文本宽度
static int16_t gfxTextWidth(Arduino_GFX *g, const char *s) {
    int16_t x1, y1; uint16_t w, h;
    g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    return (int16_t)w;
}

void Display::powerOnEarly() {
    // 与 LilyGO 官方 Arduino_GFXDemo 的 GFX_EXTRA_PRE_INIT 顺序一致：
    // setup() 一进入就先打开外设电源，再拉高背光，不能被 USB CDC 等待延后。
    pinMode(TFT_PWR_EN, OUTPUT);
    digitalWrite(TFT_PWR_EN, HIGH);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}

bool Display::begin() {
    // 防御性重申电源状态；powerOnEarly() 应已在 setup() 最开头调用。
    powerOnEarly();
    delay(20);  // 给 LCD 供电轨一个短暂稳定时间后再复位/发送初始化命令

#if USE_HW_I8080_BUS
    // 硬件 I8080 8位并行（esp_lcd i80 驱动，DMA，更快）
    Arduino_DataBus *bus = new Arduino_ESP32LCD8(
        TFT_DC, TFT_CS, TFT_WR, TFT_RD,
        TFT_D0, TFT_D1, TFT_D2, TFT_D3,
        TFT_D4, TFT_D5, TFT_D6, TFT_D7);
#else
    // 软件并行（LilyGO 官方示例同款，已知可点亮）
    Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
        TFT_DC, TFT_CS, TFT_WR, TFT_RD,
        TFT_D0, TFT_D1, TFT_D2, TFT_D3,
        TFT_D4, TFT_D5, TFT_D6, TFT_D7);
#endif
    _gfx = new Arduino_ST7789(bus, TFT_RST, 0 /* rotation */, true /* IPS */,
                              TFT_WIDTH, TFT_HEIGHT,
                              35 /* col_offset1 */, 0 /* row_offset1 */,
                              35 /* col_offset2 */, 0 /* row_offset2 */);
    const bool ok = _gfx->begin();
    if (!ok) {
        return false;
    }

    _gfx->setRotation(TFT_ROTATION);   // 横屏 320x170
    _gfx->fillScreen(C_BG);
    _gfx->setTextWrap(false);

    // 某些外设/库若改写过 GPIO 配置，这里再次确保电源和背光保持高电平。
    digitalWrite(TFT_PWR_EN, HIGH);
    digitalWrite(TFT_BL, HIGH);
    return true;
}

void Display::centerText(const char *s, int16_t y, uint8_t size,
                         uint16_t fg, uint16_t bg) {
    _gfx->setTextSize(size);
    _gfx->setTextColor(fg, bg);
    int16_t w = gfxTextWidth(_gfx, s);
    _gfx->setCursor((_gfx->width() - w) / 2, y);
    _gfx->print(s);
}

void Display::drawStatusBar(const char *title) {
    _gfx->fillRect(0, 0, _gfx->width(), 22, C_PANEL);
    _gfx->drawFastHLine(0, 22, _gfx->width(), C_MUTED);
    centerText(title, 4, 2, C_ACCENT, C_PANEL);   // size2=16px，居中于22px栏
}

void Display::drawHint(const char *hint) {
    int16_t y = _gfx->height() - 18;              // 152
    _gfx->fillRect(0, y, _gfx->width(), 18, C_PANEL);
    _gfx->drawFastHLine(0, y, _gfx->width(), C_MUTED);
    _gfx->setTextSize(1);
    _gfx->setTextColor(C_MUTED, C_PANEL);
    _gfx->setCursor(6, y + 5);
    _gfx->print(hint);
}

void Display::drawMainMenu(uint8_t highlight) {
    _gfx->fillScreen(C_BG);
    drawStatusBar("AI MUSIC");

    const char *items[] = {"1  GENERATE", "2  HISTORY"};
    const uint8_t n = 2;
    const int16_t itemH = 46, gap = 12, x = 28, w = _gfx->width() - 56;
    const int16_t y0 = 40;
    for (uint8_t i = 0; i < n; i++) {
        int16_t y = y0 + i * (itemH + gap);
        bool sel = (i == highlight);
        if (sel) {
            _gfx->fillRoundRect(x, y, w, itemH, 8, C_ACCENT);
            _gfx->setTextColor(C_BG, C_ACCENT);
        } else {
            _gfx->fillRoundRect(x, y, w, itemH, 8, C_PANEL);
            _gfx->drawRoundRect(x, y, w, itemH, 8, C_MUTED);
            _gfx->setTextColor(C_TEXT, C_PANEL);
        }
        _gfx->setTextSize(3);                    // 24px
        int16_t tw = gfxTextWidth(_gfx, items[i]);
        _gfx->setCursor(x + (w - tw) / 2, y + (itemH - 24) / 2);
        _gfx->print(items[i]);
    }
    drawHint("OK: select    UP-long: back");
}

void Display::drawRecordPage(const char *title, uint8_t step,
                             int countdownSec, const char *status) {
    _gfx->fillScreen(C_BG);
    drawStatusBar(title);

    // 步骤指示
    _gfx->setTextSize(1);
    _gfx->setTextColor(C_MUTED, C_BG);
    _gfx->setCursor(6, 28);
    char stepBuf[16];
    snprintf(stepBuf, sizeof(stepBuf), "Step %d/2", step);
    _gfx->print(stepBuf);

    // 倒计时大字
    char buf[8];
    if (countdownSec >= 0) snprintf(buf, sizeof(buf), "%d", countdownSec);
    else                   snprintf(buf, sizeof(buf), "--");
    centerText(buf, 48, 8, C_AMBER, C_BG);        // size8=64px

    // 录制状态点
    if (countdownSec >= 0) {
        _gfx->fillCircle(_gfx->width() / 2 - 60, 80, 5, C_AMBER);
    }

    centerText(status, 120, 2, C_TEXT, C_BG);
    drawHint("OK: start/next    UP-long: back");
}

void Display::drawGenerating(uint8_t animFrame, const char *status) {
    // 仅刷内容区 + 顶/底栏，避免整屏闪烁
    _gfx->fillRect(0, 23, _gfx->width(), 129, C_BG);
    drawStatusBar("GENERATING");

    // 旋转点环动画（12 点，当前帧高亮）
    int16_t cx = _gfx->width() / 2, cy = 74, r = 22;
    for (uint8_t i = 0; i < 12; i++) {
        float a = (i * 30.0f) * PI / 180.0f;
        int16_t px = cx + (int16_t)(r * cosf(a));
        int16_t py = cy + (int16_t)(r * sinf(a));
        if (i == animFrame) {
            _gfx->fillCircle(px, py, 4, C_ACCENT);
        } else {
            _gfx->fillCircle(px, py, 2, C_MUTED);
        }
    }
    centerText(status, 112, 2, C_TEXT, C_BG);
    drawHint("Composing on cloud... please wait");
}

void Display::drawPlaying(const char *songName, uint8_t percent) {
    _gfx->fillScreen(C_BG);
    drawStatusBar("NOW PLAYING");

    centerText(">>", 32, 4, C_OK, C_BG);          // 播放图标

    // 歌名（居中）
    _gfx->setTextSize(2);
    _gfx->setTextColor(C_TEXT, C_BG);
    int16_t tw = gfxTextWidth(_gfx, songName);
    int16_t cx = (_gfx->width() - tw) / 2;
    if (cx < 4) cx = 4;
    _gfx->setCursor(cx, 78);
    _gfx->print(songName);

    // 进度条
    int16_t bx = 28, by = 118, bw = _gfx->width() - 56, bh = 10;
    _gfx->drawRoundRect(bx, by, bw, bh, 4, C_MUTED);
    _gfx->fillRoundRect(bx, by, (bw * percent) / 100, bh, 4, C_OK);

    // 百分比
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    centerText(buf, 134, 1, C_MUTED, C_BG);

    drawHint("OK: toggle    UP-long: back");
}

void Display::drawHistory(const char *const *names, uint8_t count,
                          uint8_t highlight, uint8_t scrollOffset) {
    _gfx->fillScreen(C_BG);
    drawStatusBar("HISTORY");

    const int16_t rowH = 24, x = 6, w = _gfx->width() - 12, y0 = 26;
    const uint8_t visible = 5;                     // 26..146，提示栏前
    for (uint8_t i = 0; i < visible; i++) {
        uint8_t idx = scrollOffset + i;
        if (idx >= count) break;
        int16_t y = y0 + i * rowH;
        bool sel = (idx == highlight);
        if (sel) _gfx->fillRect(x, y, w, rowH - 2, C_ACCENT);
        _gfx->setTextSize(1);
        _gfx->setTextColor(sel ? C_BG : C_TEXT, sel ? C_ACCENT : C_BG);
        _gfx->setCursor(x + 6, y + 8);
        _gfx->print(sel ? "> " : "  ");
        _gfx->print(names[idx]);
    }
    drawHint("UP/DN: select    OK: play    UP-long: back");
}
