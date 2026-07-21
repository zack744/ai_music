#pragma once
#include <Arduino.h>
#include "config.h"

// =====================================================================
// 3 键输入状态机
//   板载 BOOT(GPIO0) = 上 / 长按返回
//   板载 User(GPIO14) = 下
//   外接 (GPIO1) = 确定 / 生成
// 非阻塞轮询：在 loop() 中调用 poll()，每次至多返回一个事件。
// =====================================================================

enum class InputEvent : uint8_t {
    None = 0,
    Up,     // BOOT 短按
    Down,   // User 短按
    Ok,     // 外接键短按
    Back,   // BOOT 长按
};

class Input {
public:
    void begin();
    // 在 loop 中调用；返回当前事件，无事件返回 None
    InputEvent poll();

private:
    struct BtnState {
        uint8_t  pin;
        bool     stable;        // 去抖后稳态（true=按下）
        bool     lastReading;
        uint32_t lastChangeMs;
        uint32_t pressStartMs;
        bool     longFired;     // 长按事件是否已发出
    };
    BtnState _up   { BTN_UP_PIN,   false, false, 0, 0, false };
    BtnState _down { BTN_DOWN_PIN, false, false, 0, 0, false };
    BtnState _ok   { BTN_OK_PIN,   false, false, 0, 0, false };

    // longPressBack: 该键长按是否产生 Back 事件
    InputEvent update(BtnState &b, bool longPressBack);
};
