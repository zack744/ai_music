#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// =====================================================================
// 显示：Arduino_GFX 初始化（ST7789V I8080 并行）+ 三页菜单 UI 渲染
// 横屏 320x170。Stage 2 为静态 UI 框架，不接业务逻辑。
// 文字默认 ASCII（内置字体无中文）；中文需后续接入 GFX 字库。
// =====================================================================

enum class Page : uint8_t {
    MainMenu,       // 主菜单：生成歌曲 / 历史歌曲
    RecordSpeech,   // 生成流程①：录语音
    RecordEnv,      // 生成流程②：录环境音
    Generating,     // 上传 + 云端生成中
    Playing,        // 播放
    History,        // 历史列表
};

class Display {
public:
    // 必须在 setup() 最开头调用：官方示例要求尽早拉高外设电源和背光。
    // 独立于 Arduino_GFX 初始化，便于先验证 GPIO15/GPIO38 电源链路。
    void powerOnEarly();
    bool begin();

    // ---- 各页面渲染 ----
    void drawMainMenu(uint8_t highlight);                       // highlight: 0/1
    void drawRecordPage(const char *title, uint8_t step,
                        int countdownSec, const char *status);  // step: 1/2
    void drawGenerating(uint8_t animFrame, const char *status); // animFrame: 0..11
    void drawPlaying(const char *songName, uint8_t percent);
    void drawHistory(const char *const *names, uint8_t count,
                     uint8_t highlight, uint8_t scrollOffset);

    Arduino_GFX *gfx() { return _gfx; }

private:
    Arduino_GFX *_gfx = nullptr;
    void drawStatusBar(const char *title);
    void drawHint(const char *hint);
    void centerText(const char *s, int16_t y, uint8_t size,
                    uint16_t fg, uint16_t bg);
};
