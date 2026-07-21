// =====================================================================
// ai_music ESP32 固件入口 + 三页菜单状态机
// Stage 2：点亮屏 + 3键菜单 UI 框架（静态，不接业务）
//   操作：BOOT(GPIO0)=上/长按返回, User(GPIO14)=下, 外接(GPIO1)=确定
// =====================================================================
#include "config.h"
#include "display.h"
#include "input.h"

Display display;
Input   input;

// ---- UI 状态 ----
static Page    page        = Page::MainMenu;
static uint8_t menuIdx     = 0;          // 主菜单高亮 0/1
static uint8_t histIdx     = 0;          // 历史列表当前选中
static uint8_t histOffset  = 0;          // 历史列表滚动偏移
static uint8_t genFrame    = 0;          // 生成中动画帧
static uint32_t lastAnimMs = 0;
static const char *currentSong = "song_demo_01.mp3";
static uint8_t  playPercent  = 60;

// Stage 2 演示用历史列表（Stage 6 改为 GET /api/history 拉取）
static const char *const histNames[] = {
    "song_demo_01.mp3", "song_demo_02.mp3", "song_demo_03.mp3",
    "song_demo_04.mp3", "song_demo_05.mp3"
};
static const uint8_t histCount    = sizeof(histNames) / sizeof(histNames[0]);
static const uint8_t HIST_VISIBLE = 5;

static const char *pageName(Page p) {
    switch (p) {
        case Page::MainMenu:     return "MainMenu";
        case Page::RecordSpeech: return "RecordSpeech";
        case Page::RecordEnv:    return "RecordEnv";
        case Page::Generating:   return "Generating";
        case Page::Playing:      return "Playing";
        case Page::History:      return "History";
    }
    return "?";
}

static void enterPage(Page p) {
    page = p;
    Serial.printf("[UI] -> %s\n", pageName(p));
    switch (p) {
        case Page::MainMenu:
            display.drawMainMenu(menuIdx);
            break;
        case Page::RecordSpeech:
            display.drawRecordPage("RECORD VOICE", 1, -1, "Press OK to start");
            break;
        case Page::RecordEnv:
            display.drawRecordPage("RECORD AMBIENT", 2, -1, "Press OK to start");
            break;
        case Page::Generating:
            genFrame = 0;
            lastAnimMs = millis();
            display.drawGenerating(0, "Uploading...");
            break;
        case Page::Playing:
            display.drawPlaying(currentSong, playPercent);
            break;
        case Page::History:
            display.drawHistory(histNames, histCount, histIdx, histOffset);
            break;
    }
}

void setup() {
    // LilyGO 官方要求 setup() 开头立即打开外设电源。必须放在 USB CDC
    // 的 3 秒等待之前，否则上电后的 LCD/背光会长期保持关闭状态。
    display.powerOnEarly();

    Serial.begin(115200);
    delay(3000);   // 等 USB CDC / monitor 连接(调试用,验收后可缩短)

    Serial.println("\n== ai_music ESP32 Stage 2 ==");
    Serial.println("LilyGO T-Display-S3 | ST7789V 320x170");

    Serial.printf("[POWER] GPIO%d(PWR_EN)=%d GPIO%d(BL)=%d\n",
                  TFT_PWR_EN, digitalRead(TFT_PWR_EN),
                  TFT_BL, digitalRead(TFT_BL));
#if USE_HW_I8080_BUS
    Serial.println("[DISPLAY] bus=Arduino_ESP32LCD8 (hardware I8080)");
#else
    Serial.println("[DISPLAY] bus=Arduino_ESP32PAR8Q (official software parallel)");
#endif

    Serial.println("[INIT] input begin...");
    input.begin();
    Serial.println("[INIT] display begin...");
    if (display.begin()) {
        Serial.printf("[INIT] display OK, size=%dx%d, PWR_EN=%d, BL=%d\n",
                      display.gfx()->width(), display.gfx()->height(),
                      digitalRead(TFT_PWR_EN), digitalRead(TFT_BL));
        enterPage(Page::MainMenu);
    } else {
        Serial.println("[INIT] display FAIL: Arduino_GFX begin() returned false");
    }
}

void loop() {
    InputEvent e = input.poll();

    // 生成中：旋转动画
    if (page == Page::Generating && (millis() - lastAnimMs) >= 120) {
        lastAnimMs = millis();
        genFrame = (genFrame + 1) % 12;
        display.drawGenerating(genFrame, genFrame < 6 ? "Uploading..." : "Composing...");
    }

    if (e == InputEvent::None) return;
    Serial.printf("[KEY] event=%d page=%s\n", (int)e, pageName(page));

    switch (page) {
        case Page::MainMenu:
            if (e == InputEvent::Up || e == InputEvent::Down) {
                menuIdx = (menuIdx + 1) % 2;            // 两项循环
                display.drawMainMenu(menuIdx);
            } else if (e == InputEvent::Ok) {
                enterPage(menuIdx == 0 ? Page::RecordSpeech : Page::History);
            }
            // Back 在主菜单无操作
            break;

        case Page::RecordSpeech:
            if (e == InputEvent::Ok)        enterPage(Page::RecordEnv);
            else if (e == InputEvent::Back) enterPage(Page::MainMenu);
            break;

        case Page::RecordEnv:
            if (e == InputEvent::Ok)        enterPage(Page::Generating);
            else if (e == InputEvent::Back) enterPage(Page::RecordSpeech);
            break;

        case Page::Generating:
            // Stage 2 静态：OK 模拟生成完成进入播放
            if (e == InputEvent::Ok) {
                currentSong  = "song_demo_01.mp3";
                playPercent  = 60;
                enterPage(Page::Playing);
            } else if (e == InputEvent::Back) {
                enterPage(Page::MainMenu);
            }
            break;

        case Page::Playing:
            if (e == InputEvent::Ok) {
                // 占位：切换播放/暂停（静态演示）
                playPercent = (playPercent == 60) ? 90 : 60;
                display.drawPlaying(currentSong, playPercent);
            } else if (e == InputEvent::Back) {
                enterPage(Page::MainMenu);
            }
            break;

        case Page::History:
            if (e == InputEvent::Up) {
                histIdx = (histIdx + histCount - 1) % histCount;
                if (histIdx < histOffset) histOffset = histIdx;
                display.drawHistory(histNames, histCount, histIdx, histOffset);
            } else if (e == InputEvent::Down) {
                histIdx = (histIdx + 1) % histCount;
                if (histIdx >= histOffset + HIST_VISIBLE)
                    histOffset = histIdx - HIST_VISIBLE + 1;
                display.drawHistory(histNames, histCount, histIdx, histOffset);
            } else if (e == InputEvent::Ok) {
                currentSong = histNames[histIdx];
                playPercent = 0;
                enterPage(Page::Playing);
            } else if (e == InputEvent::Back) {
                enterPage(Page::MainMenu);
            }
            break;
    }
}
